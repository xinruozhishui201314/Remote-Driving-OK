#include "vehiclecontrolservice.h"
#include "safetymonitorservice.h"
#include "../mqttcontroller.h"
#include "../vehiclemanager.h"
#include "../webrtcstreammanager.h"
#include "../infrastructure/itransportmanager.h"
#include "../utils/TimeUtils.h"
#include "../core/eventbus.h"
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <cmath>

VehicleControlService::VehicleControlService(MqttController* mqtt,
                                              VehicleManager* vehicles,
                                              WebRtcStreamManager* webrtc,
                                              QObject* parent)
    : QObject(parent)
    , m_mqtt(mqtt)
    , m_vehicles(vehicles)
    , m_wsm(webrtc)
{
    m_controlTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_controlTimer, &QTimer::timeout,
            this, &VehicleControlService::controlTick);
}

VehicleControlService::~VehicleControlService()
{
    stop();
}

void VehicleControlService::setControlConfig(const ControlConfig& cfg)
{
    m_config = cfg;
    m_controlTimer.setInterval(1000 / cfg.controlRateHz);
    if (m_predictor) m_predictor->reset();
}

bool VehicleControlService::initialize()
{
    m_predictor = std::make_unique<LatencyCompensator>();
    m_controlTimer.setInterval(1000 / m_config.controlRateHz);
    m_secondStart = TimeUtils::nowMs();
    qInfo() << "[Client][VehicleControlService] initialized at"
            << m_config.controlRateHz << "Hz";
    return true;
}

void VehicleControlService::start()
{
    if (m_running) return;
    m_running = true;
    m_controlTimer.start();
    qInfo() << "[Client][VehicleControlService] control loop started at"
            << m_config.controlRateHz << "Hz";
}

void VehicleControlService::stop()
{
    if (!m_running) return;
    m_running = false;
    m_controlTimer.stop();
    sendNeutralCommand();
    qInfo() << "[Client][VehicleControlService] control loop stopped";
}

void VehicleControlService::updateInput(const IInputDevice::InputState& input)
{
    m_latestInput.store(input);
}

// ─── 100Hz Control Tick ────────────────────────────────────────────────────────

void VehicleControlService::controlTick()
{
    const int64_t tickStart = TimeUtils::nowUs();

    // 1. 读取最新输入（无锁原子读）
    IInputDevice::InputState input = m_latestInput.load();

    // 急停检查
    if (input.emergencyStop) {
        requestEmergencyStop();
        return;
    }

    // 2. 输入处理（非线性曲线/死区）
    auto processed = processInput(input);

    // 3. 延迟补偿预测
    ControlCommand cmd;
    if (m_config.enablePrediction && m_predictor) {
        auto pred = m_predictor->predict(processed, m_currentRTTMs.load());
        cmd.predictedSteeringAngle = pred.predictedSteeringAngle;
        cmd.predictionHorizonMs    = pred.predictionHorizonMs;
    } else {
        cmd.predictedSteeringAngle = processed.steeringAngle;
    }

    // 4. 速率限制
    const double dt = 1.0 / m_config.controlRateHz;
    const double maxSteeringDelta = m_config.maxSteeringRateRadPerSec * dt;
    const double steeringDelta = cmd.predictedSteeringAngle - m_lastCommand.steeringAngle;
    if (std::abs(steeringDelta) > maxSteeringDelta) {
        cmd.steeringAngle = m_lastCommand.steeringAngle
            + std::copysign(maxSteeringDelta, steeringDelta);
    } else {
        cmd.steeringAngle = cmd.predictedSteeringAngle;
    }

    cmd.throttle = processed.throttle;
    cmd.brake    = processed.brake;
    cmd.gear     = processed.gear;
    cmd.emergencyStop = false;
    cmd.timestamp      = TimeUtils::wallClockMs();
    cmd.sequenceNumber = m_seqCounter.fetch_add(1, std::memory_order_relaxed);

    // 5. 发送
    sendCommand(cmd);
    m_lastCommand = cmd;

    // 6. 每秒统计
    ++m_ticksThisSecond;
    const int64_t now = TimeUtils::nowMs();
    if (now - m_secondStart >= 1000) {
        m_commandsPerSec.store(m_ticksThisSecond);
        m_ticksThisSecond = 0;
        m_secondStart = now;
    }

    // 7. 控制环延迟告警
    const int64_t tickDuration = TimeUtils::nowUs() - tickStart;
    if (tickDuration > 5000) { // > 5ms
        qWarning() << "[Client][VehicleControlService] tick P99 too high:"
                   << tickDuration / 1000.0 << "ms";
    }
}

IInputDevice::InputState VehicleControlService::processInput(
    const IInputDevice::InputState& raw)
{
    IInputDevice::InputState out = raw;
    out.steeringAngle = applyCurve(
        applyDeadzone(raw.steeringAngle, 0.02), m_config.steeringCurve);
    out.throttle = applyCurve(
        applyDeadzone(raw.throttle, m_config.throttleDeadzone), 1.0);
    out.brake = applyDeadzone(raw.brake, m_config.brakeDeadzone);
    return out;
}

void VehicleControlService::setSessionCredentials(const QString& vin,
                                                    const QString& sessionId,
                                                    const QString& token)
{
    m_currentVin = vin;
    m_sessionId  = sessionId;
    m_signer.setCredentials(vin, sessionId, token);
    qInfo() << "[Client][VehicleControlService] session credentials set vin=" << vin;
}

void VehicleControlService::sendCommand(const ControlCommand& cmd)
{
    auto buildJson = [&]() -> QJsonObject {
        QJsonObject json;
        json["steering"]       = cmd.steeringAngle;
        json["throttle"]       = cmd.throttle;
        json["brake"]          = cmd.brake;
        json["gear"]           = cmd.gear;
        json["emergency_stop"] = cmd.emergencyStop;
        json["timestamp"]      = static_cast<qint64>(cmd.timestamp);
        json["seq"]            = static_cast<qint64>(cmd.sequenceNumber);
        // 会话上下文（参与签名，服务端可验证归属）
        if (!m_currentVin.isEmpty())  json["vin"]        = m_currentVin;
        if (!m_sessionId.isEmpty())   json["session_id"] = m_sessionId;
        if (m_config.enablePrediction) {
            json["predicted_steering"]     = cmd.predictedSteeringAngle;
            json["prediction_horizon_ms"]  = cmd.predictionHorizonMs;
        }
        // HMAC 签名（包含上面所有已知字段）
        if (m_signer.isReady()) {
            m_signer.sign(json);
        }
        return json;
    };

    if (m_transport) {
        QJsonObject json = buildJson();
        const QByteArray payload = QJsonDocument(json).toJson(QJsonDocument::Compact);
        m_transport->sendControlJson(payload);
    } else if (m_mqtt) {
        m_mqtt->sendControlCommand(buildJson());
    }

    emit commandSent(cmd);
    pingSafety();
}

void VehicleControlService::sendNeutralCommand()
{
    ControlCommand neutral{};
    neutral.timestamp = TimeUtils::wallClockMs();
    neutral.sequenceNumber = m_seqCounter.fetch_add(1);
    sendCommand(neutral);
}

// ─── QML / UI 接口（向后兼容）────────────────────────────────────────────────

void VehicleControlService::sendUiCommand(const QString& type, const QVariantMap& payload)
{
    qDebug() << "[Client][VehicleControlService] UI command type=" << type;
    if (type == "emergency_stop") {
        requestEmergencyStop();
        return;
    }

    QJsonObject json = QJsonObject::fromVariantMap(payload);
    json["type"] = type;
    json["timestamp"] = static_cast<qint64>(TimeUtils::wallClockMs());
    sendRawControlJson(json);
}

void VehicleControlService::sendDriveCommand(double steering, double throttle, double brake)
{
    IInputDevice::InputState input{};
    input.steeringAngle = steering;
    input.throttle = throttle;
    input.brake    = brake;
    input.timestamp = TimeUtils::nowUs();
    m_latestInput.store(input);
    pingSafety();
}

void VehicleControlService::requestEmergencyStop()
{
    ControlCommand emg{};
    emg.emergencyStop  = true;
    emg.brake          = 1.0;
    emg.throttle       = 0.0;
    emg.timestamp      = TimeUtils::wallClockMs();
    emg.sequenceNumber = m_seqCounter.fetch_add(1);
    sendCommand(emg);

    qCritical() << "[Client][VehicleControlService] EMERGENCY STOP sent";
    emit emergencyStopActivated("User requested emergency stop");

    EmergencyStopEvent evt;
    evt.reason = "User emergency stop";
    evt.source = EmergencyStopEvent::Source::USER;
    EventBus::instance().publish(evt);
}

void VehicleControlService::sendRawControlJson(const QJsonObject& obj)
{
    const QByteArray payload = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    if (m_transport) {
        m_transport->sendControlJson(payload);
    } else if (m_mqtt) {
        m_mqtt->sendControlCommand(obj);
    }
    pingSafety();
}

void VehicleControlService::pingSafety()
{
    if (m_safety) {
        QMetaObject::invokeMethod(m_safety, "onOperatorActivity",
                                   Qt::QueuedConnection);
    }
}

double VehicleControlService::applyCurve(double value, double exponent)
{
    if (value == 0.0) return 0.0;
    const double sign = value > 0 ? 1.0 : -1.0;
    return sign * std::pow(std::abs(value), exponent);
}

double VehicleControlService::applyDeadzone(double value, double deadzone)
{
    if (std::abs(value) < deadzone) return 0.0;
    const double sign = value > 0 ? 1.0 : -1.0;
    return sign * (std::abs(value) - deadzone) / (1.0 - deadzone);
}
