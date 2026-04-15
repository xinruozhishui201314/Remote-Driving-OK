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
  if (v.isEmpty()) return true;
  return qEnvironmentVariableIntValue("CLIENT_TELEOP_TRACE") != 0;
}
}  // namespace

/**
 * 【内部分离工作类】运行在独立的安全线程中。
 * 负责 100Hz 控制定时器与核心计算，避免 UI 挂起导致失控。
 */
class VehicleControlWorker : public QObject {
  Q_OBJECT
 public:
  explicit VehicleControlWorker(VehicleControlService* svc) : m_svc(svc), m_timer(this) {
    m_timer.setTimerType(Qt::PreciseTimer);
    m_timer.setInterval(1000 / m_svc->m_config.controlRateHz);
    connect(&m_timer, &QTimer::timeout, this, &VehicleControlWorker::controlTick);
  }

 public slots:
  void start() {
    qInfo().noquote() << "[Client][Teleop][DIAG] Worker::start() thread:" << QThread::currentThread()
                      << "Timer thread:" << m_timer.thread()
                      << "IsOrphan:" << (m_timer.parent() == nullptr ? "YES" : "NO");
    m_timer.start();
    if (!m_timer.isActive()) {
      qCritical().noquote() << "[Client][Teleop][FATAL] QTimer FAILED to start! Potential thread mismatch.";
    }
  }
  void stop() {
    qInfo().noquote() << "[Client][Teleop][DIAG] Worker::stop()";
    m_timer.stop();
  }

  void controlTick() {
    if (!m_svc->m_running.load()) return;

    try {
      const int64_t tickStart = TimeUtils::nowUs();

      // 1. 读取最新输入（无锁原子读）
      IInputDevice::InputState input = m_svc->m_latestInput.load();

      static std::atomic<int64_t> s_tickCount{0};
      const int64_t tickIdx = s_tickCount.fetch_add(1, std::memory_order_relaxed);

      if (teleopTraceEnvEnabled()) {
        if (tickIdx % 50 == 0) { // 每 50 次 Tick (0.5s) 记录一次完整输入
          qInfo().noquote() << "[Client][Teleop][TRACE][tickIn] idx=" << tickIdx << " steer=" << input.steeringAngle
                            << " thr=" << input.throttle << " brk=" << input.brake
                            << " gear=" << input.gear << " estop=" << (input.emergencyStop ? 1 : 0);
        }
      }

      // 急停检查
      if (input.emergencyStop) {
        qCritical() << "[Client][Teleop][TRACE][tick] EMERGENCY STOP detected in input state";
        m_svc->requestEmergencyStop();
        return;
      }

      // 2. 输入处理（非线性曲线/死区）
      auto processed = m_svc->processInput(input);
      if (teleopTraceEnvEnabled() && tickIdx % 100 == 0 && std::abs(processed.steeringAngle) > 0.001) {
          qInfo().noquote() << "[Client][Teleop][TRACE][tickProc] idx=" << tickIdx << " steer_raw=" << input.steeringAngle 
                            << " processed=" << processed.steeringAngle;
      }

      // 3. 延迟补偿预测
      VehicleControlService::ControlCommand cmd;
      if (m_svc->m_config.enablePrediction && m_svc->m_predictor) {
        auto pred = m_svc->m_predictor->predict(processed, m_svc->m_currentRTTMs.load());
        cmd.predictedSteeringAngle = pred.predictedSteeringAngle;
        cmd.predictionHorizonMs = pred.predictionHorizonMs;
      } else {
        cmd.predictedSteeringAngle = processed.steeringAngle;
      }

      // 4. 速率限制
      {
        QMutexLocker lock(&m_svc->m_lastCommandMutex);
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
      }

      // 5. 发送
      const int64_t beforeSendUs = TimeUtils::nowUs();
      const int64_t durationBeforeSendUs = beforeSendUs - tickStart;

      if (durationBeforeSendUs > 5000) {
          qCritical().noquote() << "[Client][VehicleControlService] tick computation took too long:" 
                                << durationBeforeSendUs / 1000.0 << "ms. Skipping command dispatch.";
          return;
      }

      if (m_svc->m_safety && !m_svc->m_safety->allSystemsGo()) {
        static int s_breakerLog = 0;
        if (++s_breakerLog % 100 == 1) {
          qWarning() << "[Client][VehicleControlService] Circuit Breaker ACTIVE: Control command "
                        "suppressed";
        }
        // 熔断时强制发送空指令（安全停车）
        m_svc->sendNeutralCommand();
      } else {
        m_svc->sendCommand(cmd);
      }

      // 6. 每秒统计
      m_svc->m_ticksThisSecond.fetch_add(1, std::memory_order_relaxed);
      const int64_t now = TimeUtils::nowMs();
      if (now - m_svc->m_secondStart.load() >= 1000) {
        m_svc->m_commandsPerSec.store(m_svc->m_ticksThisSecond.load(std::memory_order_relaxed));
        m_svc->m_ticksThisSecond.store(0, std::memory_order_relaxed);
        m_svc->m_secondStart.store(now);
      }

      // 7. 控制环延迟告警
      const int64_t tickDuration = TimeUtils::nowUs() - tickStart;
      if (tickDuration > 5000) {  // > 5ms
        qWarning() << "[Client][VehicleControlService] tick P99 too high:" << tickDuration / 1000.0
                   << "ms";
      }

      // 8. 心跳同步
      m_svc->pingSafety();

    } catch (const std::exception& e) {
      qCritical() << "[Client][VehicleControlService] controlTick std::exception:" << e.what();
      SystemErrorEvent evt;
      evt.domain = QStringLiteral("CONTROL");
      evt.severity = SystemErrorEvent::Severity::CRITICAL;
      evt.message = QStringLiteral("控制环崩溃: %1").arg(e.what());
      EventBus::instance().publish(evt);
    } catch (...) {
      qCritical() << "[Client][VehicleControlService] controlTick unknown exception";
      SystemErrorEvent evt;
      evt.domain = QStringLiteral("CONTROL");
      evt.severity = SystemErrorEvent::Severity::CRITICAL;
      evt.message = QStringLiteral("控制环发生未知异常");
      EventBus::instance().publish(evt);
    }
  }

 private:
  VehicleControlService* m_svc;
  QTimer m_timer;
};

VehicleControlService::VehicleControlService(MqttController* mqtt, VehicleManager* vehicles,
                                             WebRtcStreamManager* webrtc, QObject* parent)
    : QObject(parent), m_mqtt(mqtt), m_vehicles(vehicles), m_wsm(webrtc) {
  m_worker = std::make_unique<VehicleControlWorker>(this);
}

VehicleControlService::~VehicleControlService() { stop(); }

uint32_t VehicleControlService::nextSequenceNumber() {
  if (m_mqtt) return m_mqtt->nextSequenceNumber();
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
    static int s_vinWarn = 0;
    if (++s_vinWarn % 100 == 1) {
      qWarning() << "[Client][VehicleControlService] sendCommand skipped: VIN "
                    "未绑定（会话未建立或凭证已清除） throttled="
                 << s_vinWarn;
    }
    return;
  }

  if (Tracing::instance().currentTraceId().isEmpty()) {
    Tracing::instance().setCurrentTraceId(Tracing::generateTraceId());
  }
  const QString traceId = Tracing::instance().currentTraceId();

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
    json["vin"] = vin;
    if (!sessionId.isEmpty()) json[QStringLiteral("sessionId")] = sessionId;
    json[QStringLiteral("trace_id")] = traceId;
    if (m_config.enablePrediction) {
      json["predicted_steering"] = cmd.predictedSteeringAngle;
      json["prediction_horizon_ms"] = cmd.predictionHorizonMs;
    }
    // HMAC 签名
    QMutexLocker lock(&m_credentialsMutex);
    if (m_signer.isReady()) {
      m_signer.sign(json);
    }
    return json;
  };

  static qint64 s_lastDriveLogMs = 0;
  const qint64 wallMs = TimeUtils::nowMs();

  QJsonObject json = buildJson();
  const QByteArray payload = QJsonDocument(json).toJson(QJsonDocument::Compact);

  static std::atomic<int64_t> s_sendCount{0};
  const int64_t sendIdx = s_sendCount.fetch_add(1, std::memory_order_relaxed);

  bool dcOk = true; // 信号发射视为已进入排队
#if !defined(VEHICLE_CONTROL_SERVICE_UNIT_TEST) && !defined(REMOTE_DRIVING_UNIT_TEST)
  // ★ 核心修复：解除同步耦合。由直接调用改为发射信号，通过 QueuedConnection 由 WebRTC 线程处理。
  if (teleopTraceEnvEnabled() && sendIdx % 100 == 0) {
      qInfo().noquote() << "[Client][Teleop][TRACE][sendDC] idx=" << sendIdx << " size=" << payload.size() << " (ASYNC EMIT)";
  }
  emit requestDataChannelSend(payload);
#endif

  bool mqttOk = false;
  if (m_mqtt && m_mqtt->mqttBrokerConnected()) {
    if (teleopTraceEnvEnabled() && sendIdx % 100 == 0) {
        qInfo().noquote() << "[Client][Teleop][TRACE][sendMQTT] idx=" << sendIdx << " ...";
    }
#ifndef VEHICLE_CONTROL_SERVICE_UNIT_TEST
    m_mqtt->sendControlCommand(json);
#endif
    mqttOk = true;
    if (teleopTraceEnvEnabled() && sendIdx % 100 == 0) {
        qInfo().noquote() << "[Client][Teleop][TRACE][sendMQTT] idx=" << sendIdx << " result=DONE";
    }
  }

  if (wallMs - s_lastDriveLogMs >= 1000) {
    s_lastDriveLogMs = wallMs;
    qInfo().noquote() << QStringLiteral(
                             "[Client][Teleop][DRIVE_1Hz] redundancy=on dc=%1 mqtt=%2 vin=%3 "
                             "steer=%4 thr=%5 brk=%6")
                             .arg(dcOk ? 1 : 0)
                             .arg(mqttOk ? 1 : 0)
                             .arg(vin)
                             .arg(cmd.steeringAngle)
                             .arg(cmd.throttle)
                             .arg(cmd.brake);
  }

  notifyCommandSent(cmd);
  pingSafety();
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
