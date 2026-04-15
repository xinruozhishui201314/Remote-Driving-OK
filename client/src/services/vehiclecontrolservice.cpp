#include "vehiclecontrolservice.h"

#include "../core/eventbus.h"
#include "../core/tracing.h"
#include "../infrastructure/itransportmanager.h"
#include "../mqttcontroller.h"
#include "../utils/MqttControlEnvelope.h"
#include "../utils/TimeUtils.h"
#include "../vehiclemanager.h"
#include "../webrtcstreammanager.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Weffc++"
#include "message_types_generated.h" // 由 FlatBuffers 生成
#pragma GCC diagnostic pop

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
bool teleopTraceEnvEnabled() {
  const QByteArray v = qgetenv("CLIENT_TELEOP_TRACE");
  if (v.isEmpty()) return true;
  return qEnvironmentVariableIntValue("CLIENT_TELEOP_TRACE") != 0;
}
}  // namespace

class VehicleControlWorker : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY(VehicleControlWorker)
 public:
  explicit VehicleControlWorker(VehicleControlService* svc)
      : QObject(nullptr), m_svc(svc), m_timer(this) {
    m_timer.setTimerType(Qt::PreciseTimer);
    m_timer.setInterval(1000 / m_svc->m_config.controlRateHz);
    connect(&m_timer, &QTimer::timeout, this, &VehicleControlWorker::controlTick);
  }

 public slots:
  void start() {
    m_timer.start();
  }
  void stop() {
    m_timer.stop();
  }

  void controlTick() {
    if (!m_svc->m_running.load()) return;

    Q_UNUSED(TimeUtils::nowUs());

    // 1. 从无锁队列读取输入（2025/2026 规范要求）
    IInputDevice::InputState input;
    bool hasNewInput = false;
    if (m_svc->m_sampler) {
      // 尽可能消费所有新输入，保留最新的
      while (m_svc->m_sampler->queue().pop(input)) {
        hasNewInput = true;
      }
    }
    
    if (!hasNewInput) {
      input = m_svc->m_latestInput.load(); // 回退到原子读
    } else {
      m_svc->m_latestInput.store(input); // 更新缓存
    }

    static std::atomic<int64_t> s_tickCount{0};
    Q_UNUSED(s_tickCount.fetch_add(1, std::memory_order_relaxed));

    if (input.emergencyStop) {
      m_svc->requestEmergencyStop();
      return;
    }

    // 2. 输入处理
    auto processed = m_svc->processInput(input);

    // 3. 延迟补偿预测
    VehicleControlService::ControlCommand cmd;
    if (m_svc->m_config.enablePrediction && m_svc->m_predictor) {
      auto pred = m_svc->m_predictor->predict(processed, m_svc->m_currentRTTMs.load());
      cmd.predictedSteeringAngle = pred.predictedSteeringAngle;
      cmd.predictionHorizonMs = pred.predictionHorizonMs;
    } else {
      cmd.predictedSteeringAngle = processed.steeringAngle;
    }

    // 4. 速率限制（无锁，因为只在此线程运行）
    const double dt = 1.0 / m_svc->m_config.controlRateHz;
    const double maxSteeringDelta = m_svc->m_config.maxSteeringRateRadPerSec * dt;
    const double steeringDelta = cmd.predictedSteeringAngle - m_svc->m_lastCommand.steeringAngle;
    if (std::abs(steeringDelta) > maxSteeringDelta) {
      cmd.steeringAngle =
          m_svc->m_lastCommand.steeringAngle + std::copysign(maxSteeringDelta, steeringDelta);
    } else {
      cmd.steeringAngle = cmd.predictedSteeringAngle;
    }

    cmd.throttle = processed.throttle;
    cmd.brake = processed.brake;
    cmd.gear = processed.gear;
    cmd.emergencyStop = false;
    cmd.timestamp = TimeUtils::wallClockMs();
    cmd.sequenceNumber = m_svc->nextSequenceNumber();

    m_svc->m_lastCommand = cmd;

    // 5. 发送
    if (m_svc->m_safety && !m_svc->m_safety->allSystemsGo()) {
      m_svc->sendNeutralCommand();
    } else {
      m_svc->sendCommand(cmd);
    }

    // 6. 统计
    m_svc->m_ticksThisSecond.fetch_add(1, std::memory_order_relaxed);
    const int64_t now = TimeUtils::nowMs();
    if (now - m_svc->m_secondStart.load() >= 1000) {
      m_svc->m_commandsPerSec.store(m_svc->m_ticksThisSecond.load(std::memory_order_relaxed));
      m_svc->m_ticksThisSecond.store(0, std::memory_order_relaxed);
      m_svc->m_secondStart.store(now);
    }

    m_svc->pingSafety();
  }

 private:
  VehicleControlService* m_svc;
  QTimer m_timer;
};

VehicleControlService::VehicleControlService(MqttController* mqtt, VehicleManager* vehicles,
                                             WebRtcStreamManager* webrtc, InputSampler* sampler,
                                             QObject* parent)
    : QObject(parent),
      m_mqtt(mqtt),
      m_vehicles(vehicles),
      m_wsm(webrtc),
      m_sampler(sampler),
      m_config(),
      m_predictor(nullptr),
      m_signer(),
      m_currentVin(),
      m_sessionId(),
      m_credentialsMutex(),
      m_latestInput(),
      m_lastCommand(),
      m_worker(nullptr),
      m_seqCounter(0),
      m_commandsPerSec(0),
      m_ticksThisSecond(0),
      m_secondStart(0),
      m_currentRTTMs(50.0),
      m_running(false),
      m_safety(nullptr),
      m_transport(nullptr) {
  m_worker = std::make_unique<VehicleControlWorker>(this);
}

VehicleControlService::~VehicleControlService() { stop(); }

uint32_t VehicleControlService::nextSequenceNumber() {
#ifndef VEHICLE_CONTROL_SERVICE_UNIT_TEST
  if (m_mqtt) return m_mqtt->nextSequenceNumber();
#endif
  return m_seqCounter.fetch_add(1, std::memory_order_relaxed);
}

void VehicleControlService::setControlConfig(const ControlConfig& cfg) {
  m_config = cfg;
  if (m_config.controlRateHz < 1) {
    qWarning() << "[Client][VehicleControlService] controlRateHz < 1 非法，已夹紧为 1";
    m_config.controlRateHz = 1;
  }
  // TODO: 如果频率变了需要通知 Worker 更新 Timer
  if (m_predictor) m_predictor->reset();
}

bool VehicleControlService::initialize() {
  m_predictor = std::make_unique<LatencyCompensator>();
  if (m_config.controlRateHz < 1) {
    qWarning() << "[Client][VehicleControlService] controlRateHz < 1 非法，已夹紧为 1";
    m_config.controlRateHz = 1;
  }
  m_secondStart.store(TimeUtils::nowMs());
  qInfo() << "[Client][VehicleControlService] initialized at" << m_config.controlRateHz << "Hz";
  if (teleopTraceEnvEnabled()) {
    qInfo().noquote() << "[Client][Teleop][TRACE] 遥操作/控制环详细日志已开启（调试阶段默认开）；"
                         "关闭: CLIENT_TELEOP_TRACE=0";
  } else {
    qInfo().noquote() << "[Client][Teleop][TRACE] CLIENT_TELEOP_TRACE=0 — 遥操作详细日志已关闭";
  }
  return true;
}

void VehicleControlService::attachToSafetyThread(QThread* thread) {
  if (thread && m_worker) {
    m_worker->moveToThread(thread);
    qInfo() << "[Client][VehicleControlService] worker moved to safety thread";
  }
}

void VehicleControlService::start() {
  if (m_running.exchange(true)) return;
  if (!QMetaObject::invokeMethod(m_worker.get(), "start", Qt::QueuedConnection)) {
    qCritical() << "[Client][VehicleControlService] FAILED to invoke worker start()";
    m_running.store(false);
    return;
  }
  qInfo() << "[Client][VehicleControlService] control loop started via worker at"
          << m_config.controlRateHz << "Hz";
}

void VehicleControlService::stop() {
  if (!m_running.exchange(false)) return;
  if (!QMetaObject::invokeMethod(m_worker.get(), "stop", Qt::QueuedConnection)) {
    qCritical() << "[Client][VehicleControlService] FAILED to invoke worker stop()";
  }
  sendNeutralCommand();
  qInfo() << "[Client][VehicleControlService] control loop stopped";
}

void VehicleControlService::updateInput(const IInputDevice::InputState& input) {
  m_latestInput.store(input);
  if (teleopTraceEnvEnabled()) {
    static std::atomic<int> s_updateCount{0};
    const int n = s_updateCount.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n % 100 == 1) {
      qInfo().noquote() << "[Client][Teleop][TRACE][updateInput] count=" << n
                        << " steer=" << input.steeringAngle << " thr=" << input.throttle
                        << " brk=" << input.brake;
    }
  }
}

// ─── 100Hz Control Tick (moved to Worker) ───────────────────────────────────

IInputDevice::InputState VehicleControlService::processInput(const IInputDevice::InputState& raw) {
  IInputDevice::InputState out = raw;
  // ★ 核心修复：死区从 0.02 调小至 0.005，提高键盘微调灵敏度
  out.steeringAngle = applyCurve(applyDeadzone(raw.steeringAngle, 0.005), m_config.steeringCurve);
  out.throttle = applyCurve(applyDeadzone(raw.throttle, m_config.throttleDeadzone), 1.0);
  out.brake = applyDeadzone(raw.brake, m_config.brakeDeadzone);
  return out;
}

void VehicleControlService::setSessionCredentials(const QString& vin, const QString& sessionId,
                                                  const QString& token) {
  QMutexLocker lock(&m_credentialsMutex);
  m_currentVin = vin;
  m_sessionId = sessionId;
  m_signer.setCredentials(vin, sessionId, token);
  qInfo().noquote() << "[Client][VehicleControlService] session credentials set vin=" << vin
                    << "sessionId=" << sessionId.left(12) << " tokenLen=" << token.size();
}

void VehicleControlService::clearSessionCredentials() {
  QMutexLocker lock(&m_credentialsMutex);
  m_currentVin.clear();
  m_sessionId.clear();
  m_signer.clearCredentials();
  qInfo() << "[Client][VehicleControlService] session credentials cleared";
}

void VehicleControlService::sendCommand(const ControlCommand& cmd) {
  QString vin;
  QString sessionId;
  {
    QMutexLocker lock(&m_credentialsMutex);
    vin = m_currentVin;
    sessionId = m_sessionId;
  }

  if (vin.isEmpty()) {
    return;
  }

  if (Tracing::instance().currentTraceId().isEmpty()) {
    Tracing::instance().setCurrentTraceId(Tracing::generateTraceId());
  }
  const QString traceId = Tracing::instance().currentTraceId();

  // 2025/2026 规范：使用 FlatBuffers 进行契约优先的高性能序列化
  flatbuffers::FlatBufferBuilder builder(1024);
  
  auto vinOffset = builder.CreateString(vin.toStdString());
  
  teleop::protocol::MessageHeaderTableBuilder headerBuilder(builder);
  headerBuilder.add_vin(vinOffset);
  headerBuilder.add_sessionId(m_sessionId.toUInt()); // 假设 sessionId 可以转 uint
  headerBuilder.add_seq(cmd.sequenceNumber);
  headerBuilder.add_timestampMs(cmd.timestamp);
  auto headerOffset = headerBuilder.Finish();
  
  teleop::protocol::DriveCommandBuilder cmdBuilder(builder);
  cmdBuilder.add_header(headerOffset);
  cmdBuilder.add_throttle(static_cast<float>(cmd.throttle));
  cmdBuilder.add_brake(static_cast<float>(cmd.brake));
  cmdBuilder.add_steering(static_cast<float>(cmd.steeringAngle));
  cmdBuilder.add_gear(static_cast<int8_t>(cmd.gear));
  cmdBuilder.add_deadman(true); // 示例：始终为 true，实际应根据输入
  cmdBuilder.add_emergency(cmd.emergencyStop);
  
  auto driveCmdOffset = cmdBuilder.Finish();
  builder.Finish(driveCmdOffset);
  
  const QByteArray payload(reinterpret_cast<const char*>(builder.GetBufferPointer()), 
                           static_cast<int>(builder.GetSize()));

  // 核心路径：异步发送，无阻塞
  emit requestDataChannelSend(payload);

  // MQTT 备份链路（根据规范：双链路冗余）
#ifndef VEHICLE_CONTROL_SERVICE_UNIT_TEST
  if (m_mqtt && m_mqtt->mqttBrokerConnected()) {
    // 暂时保持 JSON 以兼容旧版后端，生产环境应全部切为二进制
    QJsonObject json;
    json[QStringLiteral("type")] = QStringLiteral("drive");
    json["steering"] = cmd.steeringAngle;
    json["throttle"] = cmd.throttle;
    json["brake"] = cmd.brake;
    json["gear"] = cmd.gear;
    json["seq"] = static_cast<qint64>(cmd.sequenceNumber);
    m_mqtt->sendControlCommand(json);
  }
#endif

  notifyCommandSent(cmd);
}

void VehicleControlService::sendNeutralCommand() {
  ControlCommand neutral{};
  neutral.timestamp = TimeUtils::wallClockMs();
  neutral.sequenceNumber = nextSequenceNumber();

  // ★ 安全增强：如果是因为紧急停车导致的 allSystemsGo=false，中立命令应保持满刹车且打上紧急停车标记
  if (m_safety && m_safety->emergencyActive()) {
    neutral.brake = 1.0;
    neutral.emergencyStop = true;
  }

  sendCommand(neutral);
}

// ─── QML / UI 接口（向后兼容）────────────────────────────────────────────────

void VehicleControlService::sendUiCommand(const QString& type, const QVariantMap& payload) {
  if (Tracing::instance().currentTraceId().isEmpty()) {
    Tracing::instance().setCurrentTraceId(Tracing::generateTraceId());
  }
  const QString traceId = Tracing::instance().currentTraceId();

  QString vin;
  QString sessionId;
  {
    QMutexLocker lock(&m_credentialsMutex);
    vin = m_currentVin;
    sessionId = m_sessionId;
  }

  qInfo().noquote() << "[Client][Control][UI] type=" << type << "traceId=" << traceId.left(16)
                    << "vin=" << (vin.isEmpty() && m_vehicles ? m_vehicles->currentVin() : vin)
                    << "sessionId=" << sessionId.left(12)
                    << "payload="
                    << QJsonDocument(QJsonObject::fromVariantMap(payload))
                           .toJson(QJsonDocument::Compact);

  if (type == QLatin1String("emergency_stop")) {
    requestEmergencyStop();
    return;
  }

  if (vin.isEmpty() && m_vehicles) {
    vin = m_vehicles->currentVin();
  }

  const uint32_t seq = nextSequenceNumber();

  if (type == QLatin1String("gear")) {
    int g = payload.value(QStringLiteral("value")).toInt();
    setGear(g);
    qInfo() << "[Client][Control][UI] gear updated in input state via UI command to" << g;
  }

  QJsonObject json = MqttControlEnvelope::buildUiCommandEnvelope(
      type, QJsonObject::fromVariantMap(payload), vin, sessionId,
      static_cast<qint64>(TimeUtils::wallClockMs()), seq, traceId);

  // ★ 安全增强：对 UI 发起的遥操作指令进行签名
  {
    QMutexLocker lock(&m_credentialsMutex);
    if (m_signer.isReady()) {
      m_signer.sign(json);
    }
  }
  sendRawControlJson(json);
}

void VehicleControlService::setGear(int gear) {
  IInputDevice::InputState input = m_latestInput.load();
  if (input.gear != gear) {
    input.gear = gear;
    input.timestamp = TimeUtils::nowUs();
    m_latestInput.store(input);
    qDebug() << "[Client][VehicleControlService] gear updated in input state:" << gear;
  }
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
  emg.sequenceNumber = nextSequenceNumber();
  sendCommand(emg);

  qCritical() << "[Client][VehicleControlService] EMERGENCY STOP sent";
  notifyEmergencyStopActivated("User requested emergency stop");

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

void VehicleControlService::notifyCommandSent(const ControlCommand& cmd) {
  if (QThread::currentThread() != this->thread()) {
    QMetaObject::invokeMethod(this, [this, cmd]() { notifyCommandSent(cmd); },
                              Qt::QueuedConnection);
    return;
  }
  emit commandSent(cmd);
}

void VehicleControlService::notifySafetyWarning(const QString& reason) {
  if (QThread::currentThread() != this->thread()) {
    QMetaObject::invokeMethod(this, [this, reason]() { notifySafetyWarning(reason); },
                              Qt::QueuedConnection);
    return;
  }
  emit safetyWarning(reason);
}

void VehicleControlService::notifyEmergencyStopActivated(const QString& reason) {
  if (QThread::currentThread() != this->thread()) {
    QMetaObject::invokeMethod(this, [this, reason]() { notifyEmergencyStopActivated(reason); },
                              Qt::QueuedConnection);
    return;
  }
  emit emergencyStopActivated(reason);
}

void VehicleControlService::pingSafety() {
  SafetyMonitorService* s = m_safety.data();
  if (!s) return;
  // 避免字符串 invokeMethod；同线程直接调用，异线程用成员指针排队（Qt 文档推荐）。
  if (QThread::currentThread() == s->thread()) {
    s->onOperatorActivity();
  } else {
    QMetaObject::invokeMethod(s, &SafetyMonitorService::onOperatorActivity, Qt::QueuedConnection);
  }
}

double VehicleControlService::applyCurve(double value, double exponent) {
  if (value == 0.0) return 0.0;
  const double sign = value > 0 ? 1.0 : -1.0;
  return sign * std::pow(std::abs(value), exponent);
}

double VehicleControlService::applyDeadzone(double value, double deadzone) {
  if (deadzone <= 0.0) return value;
  if (deadzone >= 1.0) {
    qWarning() << "[Client][VehicleControlService] applyDeadzone: deadzone>=1 无意义，返回 0";
    return 0.0;
  }
  if (std::abs(value) < deadzone) return 0.0;
  const double sign = value > 0 ? 1.0 : -1.0;
  return sign * (std::abs(value) - deadzone) / (1.0 - deadzone);
}

#include "vehiclecontrolservice.moc"
