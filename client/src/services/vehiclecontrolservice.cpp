#include "vehiclecontrolservice.h"

#include "../core/eventbus.h"
#include "../core/tracing.h"
#include "../infrastructure/itransportmanager.h"
#include "../mqttcontroller.h"
#include "../utils/MqttControlEnvelope.h"
#include "../utils/TimeUtils.h"
#include "../vehiclemanager.h"
#include "../webrtcstreammanager.h"

#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QStringList>
#include <QThread>
#include <QtGlobal>

#include <algorithm>
#include <cmath>

namespace {
/** 调试阶段默认开启；显式 CLIENT_TELEOP_TRACE=0 一键关闭。 */
bool teleopTraceEnvEnabled() {
  const QByteArray v = qgetenv("CLIENT_TELEOP_TRACE");
  if (v.isEmpty())
    return true;
  return qEnvironmentVariableIntValue("CLIENT_TELEOP_TRACE") != 0;
}
}  // namespace

VehicleControlService::VehicleControlService(MqttController* mqtt, VehicleManager* vehicles,
                                             WebRtcStreamManager* webrtc, QObject* parent)
    : QObject(parent), m_mqtt(mqtt), m_vehicles(vehicles), m_wsm(webrtc) {
  m_controlTimer.setTimerType(Qt::PreciseTimer);
  connect(&m_controlTimer, &QTimer::timeout, this, &VehicleControlService::controlTick);
}

VehicleControlService::~VehicleControlService() { stop(); }

void VehicleControlService::setControlConfig(const ControlConfig& cfg) {
  m_config = cfg;
  if (m_config.controlRateHz < 1) {
    qWarning() << "[Client][VehicleControlService] controlRateHz < 1 非法，已夹紧为 1";
    m_config.controlRateHz = 1;
  }
  m_controlTimer.setInterval(1000 / m_config.controlRateHz);
  if (m_predictor)
    m_predictor->reset();
}

bool VehicleControlService::initialize() {
  m_predictor = std::make_unique<LatencyCompensator>();
  if (m_config.controlRateHz < 1) {
    qWarning() << "[Client][VehicleControlService] controlRateHz < 1 非法，已夹紧为 1";
    m_config.controlRateHz = 1;
  }
  m_controlTimer.setInterval(1000 / m_config.controlRateHz);
  m_secondStart = TimeUtils::nowMs();
  qInfo() << "[Client][VehicleControlService] initialized at" << m_config.controlRateHz << "Hz";
  if (teleopTraceEnvEnabled()) {
    qInfo().noquote() << "[Client][Teleop][TRACE] 遥操作/控制环详细日志已开启（调试阶段默认开）；"
                         "关闭: CLIENT_TELEOP_TRACE=0";
  } else {
    qInfo().noquote() << "[Client][Teleop][TRACE] CLIENT_TELEOP_TRACE=0 — 遥操作详细日志已关闭";
  }
  return true;
}

void VehicleControlService::start() {
  if (m_running)
    return;
  m_running = true;
  m_controlTimer.start();
  qInfo() << "[Client][VehicleControlService] control loop started at" << m_config.controlRateHz
          << "Hz";
}

void VehicleControlService::stop() {
  if (!m_running)
    return;
  m_running = false;
  m_controlTimer.stop();
  sendNeutralCommand();
  qInfo() << "[Client][VehicleControlService] control loop stopped";
}

void VehicleControlService::updateInput(const IInputDevice::InputState& input) {
  m_latestInput.store(input);
}

// ─── 100Hz Control Tick ────────────────────────────────────────────────────────

void VehicleControlService::controlTick() {
  const int64_t tickStart = TimeUtils::nowUs();

  // 1. 读取最新输入（无锁原子读）
  IInputDevice::InputState input = m_latestInput.load();

  if (teleopTraceEnvEnabled()) {
    static qint64 s_lastTraceMs = 0;
    const qint64 wallMs = TimeUtils::nowMs();
    if (wallMs - s_lastTraceMs >= 250) {
      s_lastTraceMs = wallMs;
      qInfo().noquote() << "[Client][Teleop][TRACE][tickIn] steer=" << input.steeringAngle
                        << " thr=" << input.throttle << " brk=" << input.brake << " gear=" << input.gear
                        << " estop=" << (input.emergencyStop ? 1 : 0);
    }
  }

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
    cmd.predictionHorizonMs = pred.predictionHorizonMs;
  } else {
    cmd.predictedSteeringAngle = processed.steeringAngle;
  }

  // 4. 速率限制
  const double dt = 1.0 / m_config.controlRateHz;
  const double maxSteeringDelta = m_config.maxSteeringRateRadPerSec * dt;
  const double steeringDelta = cmd.predictedSteeringAngle - m_lastCommand.steeringAngle;
  if (std::abs(steeringDelta) > maxSteeringDelta) {
    cmd.steeringAngle =
        m_lastCommand.steeringAngle + std::copysign(maxSteeringDelta, steeringDelta);
  } else {
    cmd.steeringAngle = cmd.predictedSteeringAngle;
  }

  cmd.throttle = processed.throttle;
  cmd.brake = processed.brake;
  cmd.gear = processed.gear;
  cmd.emergencyStop = false;
  cmd.timestamp = TimeUtils::wallClockMs();
  cmd.sequenceNumber = m_seqCounter.fetch_add(1, std::memory_order_relaxed);

  // 5. 发送
  sendCommand(cmd);
  m_lastCommand = cmd;

  // 6. 每秒统计
  m_ticksThisSecond.fetch_add(1, std::memory_order_relaxed);
  const int64_t now = TimeUtils::nowMs();
  if (now - m_secondStart >= 1000) {
    m_commandsPerSec.store(m_ticksThisSecond.load(std::memory_order_relaxed));
    m_ticksThisSecond.store(0, std::memory_order_relaxed);
    m_secondStart = now;
  }

  // 7. 控制环延迟告警
  const int64_t tickDuration = TimeUtils::nowUs() - tickStart;
  if (tickDuration > 5000) {  // > 5ms
    qWarning() << "[Client][VehicleControlService] tick P99 too high:" << tickDuration / 1000.0
               << "ms";
  }
}

IInputDevice::InputState VehicleControlService::processInput(const IInputDevice::InputState& raw) {
  IInputDevice::InputState out = raw;
  out.steeringAngle = applyCurve(applyDeadzone(raw.steeringAngle, 0.02), m_config.steeringCurve);
  out.throttle = applyCurve(applyDeadzone(raw.throttle, m_config.throttleDeadzone), 1.0);
  out.brake = applyDeadzone(raw.brake, m_config.brakeDeadzone);
  return out;
}

void VehicleControlService::setSessionCredentials(const QString& vin, const QString& sessionId,
                                                  const QString& token) {
  m_currentVin = vin;
  m_sessionId = sessionId;
  m_signer.setCredentials(vin, sessionId, token);
  qInfo().noquote() << "[Client][VehicleControlService] session credentials set vin=" << vin
                    << "sessionId=" << sessionId.left(12) << " tokenLen=" << token.size();
}

void VehicleControlService::clearSessionCredentials() {
  m_currentVin.clear();
  m_sessionId.clear();
  m_signer.clearCredentials();
  qInfo() << "[Client][VehicleControlService] session credentials cleared";
}

void VehicleControlService::sendCommand(const ControlCommand& cmd) {
  if (m_currentVin.isEmpty()) {
    qWarning() << "[Client][VehicleControlService] sendCommand skipped: VIN "
                  "未绑定（会话未建立或凭证已清除）";
    return;
  }
  if (Tracing::instance().currentTraceId().isEmpty()) {
    Tracing::instance().setCurrentTraceId(Tracing::generateTraceId());
  }
  auto buildJson = [&]() -> QJsonObject {
    QJsonObject json;
    json[QStringLiteral("type")] = QStringLiteral("drive");
    json[QStringLiteral("schemaVersion")] = MqttControlEnvelope::controlSchemaVersionString();
    json["steering"] = cmd.steeringAngle;
    json["throttle"] = cmd.throttle;
    json["brake"] = cmd.brake;
    json["gear"] = cmd.gear;
    json["emergency_stop"] = cmd.emergencyStop;
    json[QStringLiteral("timestampMs")] = static_cast<qint64>(cmd.timestamp);
    json["seq"] = static_cast<qint64>(cmd.sequenceNumber);
    // 会话上下文（参与签名，服务端可验证归属；字段名与 Vehicle-side control_protocol sessionId 一致）
    if (!m_currentVin.isEmpty())
      json["vin"] = m_currentVin;
    if (!m_sessionId.isEmpty())
      json[QStringLiteral("sessionId")] = m_sessionId;
    json[QStringLiteral("trace_id")] = Tracing::instance().currentTraceId();
    if (m_config.enablePrediction) {
      json["predicted_steering"] = cmd.predictedSteeringAngle;
      json["prediction_horizon_ms"] = cmd.predictionHorizonMs;
    }
    // HMAC 签名（包含上面所有已知字段）
    if (m_signer.isReady()) {
      m_signer.sign(json);
    }
    return json;
  };

  static qint64 s_lastDriveLogMs = 0;
  const qint64 wallMs = TimeUtils::nowMs();

  if (m_transport) {
    QJsonObject json = buildJson();
    const QByteArray payload = QJsonDocument(json).toJson(QJsonDocument::Compact);
    m_transport->sendControlJson(payload);
    if (wallMs - s_lastDriveLogMs >= 1000) {
      s_lastDriveLogMs = wallMs;
      qInfo().noquote() << "[Client][Teleop][DRIVE_1Hz] ch=transport vin=" << m_currentVin
                        << "steer=" << cmd.steeringAngle << "thr=" << cmd.throttle << "brk=" << cmd.brake
                        << "gear=" << cmd.gear << "seq=" << cmd.sequenceNumber;
    }
  } else if (m_mqtt) {
    if (!m_mqtt->mqttBrokerConnected()) {
      static int s_mqttSkip = 0;
      if (++s_mqttSkip == 1 || s_mqttSkip % 400 == 0) {
        qDebug() << "[Client][VehicleControlService][CHAIN] skip sendCommand: MQTT 未连接 throttled="
                 << s_mqttSkip << " | 先连 Broker；grep [CLIENT][MQTT][CHAIN]";
      }
    } else {
#ifndef VEHICLE_CONTROL_SERVICE_UNIT_TEST
      m_mqtt->sendControlCommand(buildJson());
      if (wallMs - s_lastDriveLogMs >= 1000) {
        s_lastDriveLogMs = wallMs;
        qInfo().noquote() << "[Client][Teleop][DRIVE_1Hz] ch=mqtt vin=" << m_currentVin
                          << "steer=" << cmd.steeringAngle << "thr=" << cmd.throttle << "brk=" << cmd.brake
                          << "gear=" << cmd.gear << "seq=" << cmd.sequenceNumber;
      }
#else
      // 单测目标不链接 MqttController.cpp；此处仅在 m_mqtt!=nullptr 时需要完整客户端
      Q_UNUSED(m_mqtt);
#endif
    }
  }

  emit commandSent(cmd);
  pingSafety();
}

void VehicleControlService::sendNeutralCommand() {
  ControlCommand neutral{};
  neutral.timestamp = TimeUtils::wallClockMs();
  neutral.sequenceNumber = m_seqCounter.fetch_add(1);
  sendCommand(neutral);
}

// ─── QML / UI 接口（向后兼容）────────────────────────────────────────────────

void VehicleControlService::sendUiCommand(const QString& type, const QVariantMap& payload) {
  if (Tracing::instance().currentTraceId().isEmpty()) {
    Tracing::instance().setCurrentTraceId(Tracing::generateTraceId());
  }
  const QString traceId = Tracing::instance().currentTraceId();

  QStringList payloadKeyList;
  for (auto it = payload.constBegin(); it != payload.constEnd(); ++it)
    payloadKeyList.append(it.key());

  qInfo().noquote() << "[Client][Control][UI] type=" << type << "traceId=" << traceId.left(16)
                    << "vin=" << (m_currentVin.isEmpty() && m_vehicles ? m_vehicles->currentVin() : m_currentVin)
                    << "sessionId=" << m_sessionId.left(12)
                    << "payload=" << QJsonDocument(QJsonObject::fromVariantMap(payload)).toJson(QJsonDocument::Compact);

  if (type == QLatin1String("emergency_stop")) {
    requestEmergencyStop();
    return;
  }

  QString vin = m_currentVin;
  if (vin.isEmpty() && m_vehicles) {
    vin = m_vehicles->currentVin();
  }

  const qint64 seq = static_cast<qint64>(m_seqCounter.fetch_add(1, std::memory_order_relaxed));
  
  // ★ 核心修复：同步 UI 指令状态到 100Hz 控制环路输入（m_latestInput）
  // 否则控制环路会立即用旧状态（如 gear=0/N）覆盖 UI 发出的新指令。
  if (type == QLatin1String("gear")) {
    int g = payload.value(QStringLiteral("value")).toInt();
    IInputDevice::InputState input = m_latestInput.load();
    input.gear = g;
    m_latestInput.store(input);
    qInfo() << "[Client][Control][UI] gear updated in state machine to" << g;
  }

  const QJsonObject json = MqttControlEnvelope::buildUiCommandEnvelope(
      type, QJsonObject::fromVariantMap(payload), vin, m_sessionId,
      static_cast<qint64>(TimeUtils::wallClockMs()), seq, traceId);
  sendRawControlJson(json);
}

void VehicleControlService::sendDriveCommand(double steering, double throttle, double brake) {
  IInputDevice::InputState input = m_latestInput.load();
  input.steeringAngle = steering;
  input.throttle = throttle;
  input.brake = brake;
  input.timestamp = TimeUtils::nowUs();
  m_latestInput.store(input);
  if (teleopTraceEnvEnabled()) {
    static qint64 s_lastDriveTraceMs = 0;
    const qint64 wallMs = TimeUtils::nowMs();
    if (wallMs - s_lastDriveTraceMs >= 100) {
      s_lastDriveTraceMs = wallMs;
      qInfo().noquote() << "[Client][Teleop][TRACE][sendDriveCommand] steer=" << steering
                        << " thr=" << throttle << " brk=" << brake;
    }
  }
  pingSafety();
}

void VehicleControlService::requestEmergencyStop() {
  ControlCommand emg{};
  emg.emergencyStop = true;
  emg.brake = 1.0;
  emg.throttle = 0.0;
  emg.timestamp = TimeUtils::wallClockMs();
  emg.sequenceNumber = m_seqCounter.fetch_add(1);
  sendCommand(emg);

  qCritical() << "[Client][VehicleControlService] EMERGENCY STOP sent";
  emit emergencyStopActivated("User requested emergency stop");

  EmergencyStopEvent evt;
  evt.reason = "User emergency stop";
  evt.source = EmergencyStopEvent::Source::USER;
  EventBus::instance().publish(evt);
}

void VehicleControlService::sendRawControlJson(const QJsonObject& obj) {
  const QByteArray payload = QJsonDocument(obj).toJson(QJsonDocument::Compact);
  if (m_transport) {
    m_transport->sendControlJson(payload);
  } else if (m_mqtt) {
    if (!m_mqtt->mqttBrokerConnected()) {
      static int s_raw = 0;
      if (++s_raw % 200 == 1) {
        qDebug() << "[Client][VehicleControlService][CHAIN] skip sendRawControlJson: MQTT 未连接 "
                    "throttled="
                 << s_raw;
      }
    } else {
#ifndef VEHICLE_CONTROL_SERVICE_UNIT_TEST
      m_mqtt->sendControlCommand(obj);
#else
      Q_UNUSED(m_mqtt);
#endif
    }
  }
  pingSafety();
}

void VehicleControlService::pingSafety() {
  SafetyMonitorService *s = m_safety.data();
  if (!s)
    return;
  // 避免字符串 invokeMethod；同线程直接调用，异线程用成员指针排队（Qt 文档推荐）。
  if (QThread::currentThread() == s->thread()) {
    s->onOperatorActivity();
  } else {
    QMetaObject::invokeMethod(s, &SafetyMonitorService::onOperatorActivity, Qt::QueuedConnection);
  }
}

double VehicleControlService::applyCurve(double value, double exponent) {
  if (value == 0.0)
    return 0.0;
  const double sign = value > 0 ? 1.0 : -1.0;
  return sign * std::pow(std::abs(value), exponent);
}

double VehicleControlService::applyDeadzone(double value, double deadzone) {
  if (deadzone <= 0.0)
    return value;
  if (deadzone >= 1.0) {
    qWarning() << "[Client][VehicleControlService] applyDeadzone: deadzone>=1 无意义，返回 0";
    return 0.0;
  }
  if (std::abs(value) < deadzone)
    return 0.0;
  const double sign = value > 0 ? 1.0 : -1.0;
  return sign * (std::abs(value) - deadzone) / (1.0 - deadzone);
}
