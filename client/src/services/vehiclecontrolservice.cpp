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
      // ★ 核心修复（5 Why 分析结论）：
      // 硬件设备采样回来的 InputState 是快照，如果直接 store 会覆盖掉 UI 先前修改的 modal 状态（如 gear）。
      // 我们需要将最新的 modal 状态合并进去。
      IInputDevice::InputState current = m_svc->m_latestInput.load();
      
      // 如果硬件设备的档位没变，则保留服务当前的档位（可能是 UI 刚改的）
      // 如果硬件设备的档位变了（说明用户动了硬件拨杆），则以硬件为准
      if (input.gear == m_lastPollGear) {
          input.gear = current.gear;
      } else {
          m_lastPollGear = input.gear;
      }

      // [RootCauseFix] 必须要合并 targetSpeed，否则硬件采样（始终为0）会冲掉 UI 的巡航设定
      if (std::abs(input.targetSpeed - m_lastPollSpeed) < 0.001) {
          if (input.targetSpeed != current.targetSpeed && teleopTraceEnvEnabled()) {
              qInfo().noquote() << "[Client][Control][Merge] Speed preserved from UI:" << current.targetSpeed 
                                << "(Hardware zero ignored)";
          }
          input.targetSpeed = current.targetSpeed;
      } else {
          qInfo().noquote() << "[Client][Control][Merge] Speed updated from Hardware:" << input.targetSpeed;
          m_lastPollSpeed = input.targetSpeed;
      }

      // 同理处理 emergencyStop 等 modal 状态
      if (input.emergencyStop == m_lastPollEmg) {
          input.emergencyStop = current.emergencyStop;
      } else {
          m_lastPollEmg = input.emergencyStop;
      }

      m_svc->m_latestInput.store(input); // 更新合并后的缓存
    }

    static std::atomic<int64_t> s_tickCount{0};
    Q_UNUSED(s_tickCount.fetch_add(1, std::memory_order_relaxed));

    if (input.emergencyStop) {
      m_svc->requestEmergencyStop();
      return;
    }

    // 2. 输入处理
    auto processed = m_svc->processInput(input);

    // ★ 核心修复：输入链路活跃度监控 (Input Link Watchdog)
    // 2025/2026 规范修正：有效输入不仅包含物理踏板(throttle/brake)，也包含逻辑时速意图(targetSpeed)。
    // 这样在“松开按键巡航”时，即使油门为0，看门狗也能识别到驾驶员的持续意图。
    const int64_t nowMs = TimeUtils::wallClockMs();
    bool hasEffectiveInput = (std::abs(processed.steeringAngle) > 0.001 || 
                              processed.throttle > 0.001 || 
                              processed.brake > 0.001 ||
                              processed.targetSpeed > 0.1); 
    
    if (hasEffectiveInput) {
      m_svc->m_lastEffectiveInputMs.store(nowMs, std::memory_order_relaxed);
    }

    // 检查静默状态：操作员有活动（来自 UI）但采样器持续上报 0
    const int64_t lastActivity = m_svc->m_lastOperatorActivityMs.load(std::memory_order_relaxed);
    const int64_t lastEffective = m_svc->m_lastEffectiveInputMs.load(std::memory_order_relaxed);
    
    // 如果 1s 内有 UI 活动，但采样器超过 silentThresholdMs 没有有效输入
    bool isSilent = false;
    if (nowMs - lastActivity < 1000) {
      if (nowMs - lastEffective > m_svc->m_config.silentThresholdMs) {
        isSilent = true;
      }
    }

    if (isSilent != m_svc->m_inputLinkSilent.load()) {
      m_svc->m_inputLinkSilent.store(isSilent);
      auto svc = m_svc;
      QMetaObject::invokeMethod(svc, [svc, isSilent]() {
        emit svc->inputLinkSilentChanged(isSilent);
      }, Qt::QueuedConnection);
      
      if (isSilent) {
        qCritical() << "[Client][Control][Watchdog] ⚠ 检测到静默输入链路！"
                    << "UI 有活动但控制环数据持续为 0。可能原因：焦点丢失、映射冲突或采样器断路。";
      }
    }

    // 3. 延迟补偿预测
    auto cmdHandle = m_svc->commandPool().acquireHandle();
    auto& cmd = *cmdHandle;
    
    // 初始化命令基础信息 (2025/2026 规范：从热路径对象池获取)
    cmd.steeringAngle = processed.steeringAngle;
    cmd.throttle = processed.throttle;
    cmd.brake = processed.brake;
    cmd.gear = processed.gear;
    cmd.targetSpeed = processed.targetSpeed; // [Crucial] 注入时速意图
    cmd.emergencyStop = processed.emergencyStop;
    cmd.timestamp = TimeUtils::wallClockMs();
    cmd.sequenceNumber = m_svc->nextSequenceNumber();

    if (m_svc->m_config.enablePrediction && m_svc->m_predictor) {
      auto pred = m_svc->m_predictor->predict(processed, m_svc->m_currentRTTMs.load());
      cmd.predictedSteeringAngle = pred.predictedSteeringAngle;
      cmd.predictionHorizonMs = pred.predictionHorizonMs;
    } else {
      cmd.predictedSteeringAngle = processed.steeringAngle;
      cmd.predictionHorizonMs = 0.0;
    }

    // 4. 速率限制（无锁，因为只在此线程运行）
    const double dt = 1.0 / m_svc->m_config.controlRateHz;
    const double maxSteeringDelta = m_svc->m_config.maxSteeringRateRadPerSec * dt;
    
    // 对预测后的转向角应用物理限制
    const double steeringDelta = cmd.predictedSteeringAngle - m_svc->m_lastCommand.steeringAngle;
    if (std::abs(steeringDelta) > maxSteeringDelta) {
      cmd.steeringAngle =
          m_svc->m_lastCommand.steeringAngle + std::copysign(maxSteeringDelta, steeringDelta);
    } else {
      cmd.steeringAngle = cmd.predictedSteeringAngle;
    }

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
  int m_lastPollGear = 0;
  double m_lastPollSpeed = 0.0;
  bool m_lastPollEmg = false;
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
      m_activeContext(nullptr),
      m_latestInput(),
      m_lastCommand(),
      m_worker(nullptr),
      m_seqCounter(0),
      m_commandsPerSec(0),
      m_ticksThisSecond(0),
      m_secondStart(0),
      m_currentRTTMs(50.0),
      m_running(false),
      m_lastEffectiveInputMs(0),
      m_lastOperatorActivityMs(0),
      m_inputLinkSilent(false),
      m_safety(nullptr),
      m_transport(nullptr) {
  m_worker = std::make_unique<VehicleControlWorker>(this);
}

VehicleControlService::~VehicleControlService() { 
  stop(); 
  SessionContext* ctx = m_activeContext.exchange(nullptr);
  delete ctx;
}

uint32_t VehicleControlService::nextSequenceNumber() {
#ifndef VEHICLE_CONTROL_SERVICE_UNIT_TEST
  if (m_mqtt) return m_mqtt->nextSequenceNumber();
#endif
  return m_seqCounter.fetch_add(1, std::memory_order_relaxed);
}

void VehicleControlService::setSafetyMonitor(SafetyMonitorService* safety) {
  m_safety = safety;
  if (m_safety) {
    // 监听来自 UI 的原始操作活动信号，用于输入链路对齐检查
    connect(m_safety.data(), &SafetyMonitorService::operatorActivityReported, this, &VehicleControlService::noteOperatorActivity, Qt::QueuedConnection);
  }
}

void VehicleControlService::noteOperatorActivity() {
  m_lastOperatorActivityMs.store(TimeUtils::wallClockMs(), std::memory_order_relaxed);
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

  // 1. 边界值检查 (Range Check)
  out.steeringAngle = std::clamp(raw.steeringAngle, -1.0, 1.0);
  out.throttle = std::clamp(raw.throttle, 0.0, 1.0);
  out.brake = std::clamp(raw.brake, 0.0, 1.0);
  out.targetSpeed = std::max(0.0, raw.targetSpeed); // 透传目标车速意图

  // 2. 变化率检查 (Slew Rate Limit) - 防止输入异常跳变
  // 仅在热路径（DRIVING 状态）且有上一次合法输入时应用
  const double dt = 1.0 / m_config.controlRateHz;
  const double maxSteeringJump = 4.0 * dt;  // 每秒最多变化 400% (2秒从最左到最右)
  const double maxPedalJump = 8.0 * dt;     // 每秒最多变化 800% (0.125秒从0到100%)

  if (m_running.load() && m_lastProcessedInput.timestamp > 0) {
    auto applySlewLimit = [&](double current, double previous, double maxDelta,
                              const char* name) {
      double delta = current - previous;
      if (std::abs(delta) > maxDelta) {
        double limited = previous + (delta > 0 ? maxDelta : -maxDelta);
        // 降低日志频率，避免 100Hz 灌满控制台
        static std::atomic<int> s_logCount{0};
        if (s_logCount.fetch_add(1, std::memory_order_relaxed) % 100 == 0) {
            qWarning() << "[Client][Control][Safety] Slew rate limit hit on" << name << "delta=" << delta
                       << "max=" << maxDelta << "clamped=" << limited;
        }
        return limited;
      }
      return current;
    };

    out.steeringAngle =
        applySlewLimit(out.steeringAngle, m_lastProcessedInput.steeringAngle, maxSteeringJump, "steering");
    out.throttle =
        applySlewLimit(out.throttle, m_lastProcessedInput.throttle, maxPedalJump, "throttle");
    out.brake =
        applySlewLimit(out.brake, m_lastProcessedInput.brake, maxPedalJump, "brake");
  }

  // 状态保存：必须保存【曲线处理前】的线性值，否则下一帧变化率计算会因非线性映射出错
  m_lastProcessedInput = out;

  // 3. 曲线与死区处理 (仅用于输出，不存入 m_lastProcessedInput)
  out.steeringAngle = applyCurve(applyDeadzone(out.steeringAngle, 0.005), m_config.steeringCurve);
  out.throttle = applyCurve(applyDeadzone(out.throttle, m_config.throttleDeadzone), 1.0);
  out.brake = applyDeadzone(out.brake, m_config.brakeDeadzone);

  return out;
}

void VehicleControlService::setSessionCredentials(const QString& vin, const QString& sessionId,
                                                  const QString& token) {
  auto* newCtx = new SessionContext();
  std::strncpy(newCtx->vin, vin.toUtf8().constData(), sizeof(newCtx->vin) - 1);
  std::strncpy(newCtx->sessionId, sessionId.toUtf8().constData(), sizeof(newCtx->sessionId) - 1);
  std::strncpy(newCtx->token, token.toUtf8().constData(), sizeof(newCtx->token) - 1);

  SessionContext* oldCtx = m_activeContext.exchange(newCtx, std::memory_order_acq_rel);
  m_signer.setCredentials(vin, sessionId, token);
  
  // 延迟删除旧上下文以确保 Worker 线程不再引用（控制频率 100Hz，等待 50ms 足够）
  if (oldCtx) {
    QTimer::singleShot(50, [oldCtx]() { delete oldCtx; });
  }

  qInfo().noquote() << "[Client][VehicleControlService] session context updated vin=" << vin
                    << "sessionId=" << sessionId.left(12) << " (lock-free)";
}

void VehicleControlService::clearSessionCredentials() {
  SessionContext* oldCtx = m_activeContext.exchange(nullptr, std::memory_order_acq_rel);
  m_signer.clearCredentials();
  if (oldCtx) {
    QTimer::singleShot(50, [oldCtx]() { delete oldCtx; });
  }
  qInfo() << "[Client][VehicleControlService] session context cleared";
}

void VehicleControlService::sendCommand(const ControlCommand& cmd) {
  // 1. 原子读取上下文 (O(1) 无锁)
  const SessionContext* ctx = m_activeContext.load(std::memory_order_acquire);
  if (!ctx || ctx->vin[0] == '\0') {
    return;
  }

  const char* vinStr = ctx->vin;
  const char* sessionIdStr = ctx->sessionId;

  // 为兼容 FlatBuffers 既有 uint32 契约，对非数字 sessionId 取哈希（生产环境建议更新 fbs 契约为 string）
  uint32_t sessionIdNum = 0;
  bool ok = false;
  sessionIdNum = QString::fromUtf8(sessionIdStr).toUInt(&ok);
  if (!ok) {
    sessionIdNum = static_cast<uint32_t>(qHash(QString::fromUtf8(sessionIdStr)));
  }

  if (Tracing::instance().currentTraceId().isEmpty()) {
    Tracing::instance().setCurrentTraceId(Tracing::generateTraceId());
  }
  const QString traceId = Tracing::instance().currentTraceId();

  // 2025/2026 规范：使用 FlatBuffers 进行契约优先的高性能序列化
  flatbuffers::FlatBufferBuilder builder(1024);
  
  auto vinOffset = builder.CreateString(vinStr);
  
  teleop::protocol::MessageHeaderTableBuilder headerBuilder(builder);
  headerBuilder.add_vin(vinOffset);
  headerBuilder.add_sessionId(sessionIdNum);
  headerBuilder.add_seq(cmd.sequenceNumber);
  headerBuilder.add_timestampMs(cmd.timestamp);
  auto headerOffset = headerBuilder.Finish();
  
  teleop::protocol::DriveCommandBuilder cmdBuilder(builder);
  cmdBuilder.add_header(headerOffset);
  cmdBuilder.add_throttle(static_cast<float>(cmd.throttle));
  cmdBuilder.add_brake(static_cast<float>(cmd.brake));
  cmdBuilder.add_steering(static_cast<float>(cmd.steeringAngle));
  cmdBuilder.add_gear(static_cast<int8_t>(cmd.gear));
  cmdBuilder.add_maxSpeed(static_cast<uint16_t>(cmd.targetSpeed)); // [Fix] 注入目标时速到二进制协议
  cmdBuilder.add_deadman(true); // 示例：始终为 true，实际应根据输入
  cmdBuilder.add_emergency(cmd.emergencyStop);
  
  auto driveCmdOffset = cmdBuilder.Finish();
  builder.Finish(driveCmdOffset);
  
  const QByteArray payload(reinterpret_cast<const char*>(builder.GetBufferPointer()), 
                           static_cast<int>(builder.GetSize()));

  // 核心路径：使用传输聚合器进行发送（内部实现双链路冗余热切换）
  if (m_transport) {
    m_transport->send(TransportChannel::CONTROL_CRITICAL, 
                      reinterpret_cast<const uint8_t*>(payload.constData()), 
                      static_cast<size_t>(payload.size()));
  } else {
    // 回退路径
    emit requestDataChannelSend(payload);
  }

  // ★ 系统性修复：针对 MQTT 链路，始终同步发送一份 JSON 格式的 drive 指令备份。
  // 注意：如果使用的是 mosquitto_pub 进程模式，必须降频发送以免压垮系统。
#ifndef VEHICLE_CONTROL_SERVICE_UNIT_TEST
  if (m_mqtt && m_mqtt->mqttBrokerConnected()) {
    static QElapsedTimer s_mqttBackupTimer;
    if (!s_mqttBackupTimer.isValid()) s_mqttBackupTimer.start();
    
    // 降频到 10Hz (100ms)，足以满足车端 500ms 看门狗
    if (s_mqttBackupTimer.elapsed() >= 100) {
      s_mqttBackupTimer.restart();
      QJsonObject json;
    json[QStringLiteral("type")] = QStringLiteral("drive");
    json[QStringLiteral("steering")] = cmd.steeringAngle;
    json[QStringLiteral("throttle")] = cmd.throttle;
    json[QStringLiteral("brake")] = cmd.brake;
    json[QStringLiteral("gear")] = cmd.gear;
    json[QStringLiteral("speed")] = cmd.targetSpeed; // [Fix] 同步目标速度
    json[QStringLiteral("seq")] = static_cast<qint64>(cmd.sequenceNumber);
    json[QStringLiteral("timestamp")] = static_cast<qint64>(cmd.timestamp);
    json[QStringLiteral("traceId")] = traceId;
    if (ctx) {
        json[QStringLiteral("sessionId")] = QString::fromUtf8(ctx->sessionId);
        json[QStringLiteral("vin")] = QString::fromUtf8(ctx->vin);
    }
    
    if (teleopTraceEnvEnabled()) {
      static int s_sendCount = 0;
      if (s_sendCount++ % 100 == 0) {
          qInfo().noquote() << "[Client][Control][Intent] SERIALIZED: steer=" << cmd.steeringAngle 
                            << " thr=" << cmd.throttle << " brk=" << cmd.brake
                            << " gear=" << cmd.gear << " spd=" << cmd.targetSpeed 
                            << " seq=" << cmd.sequenceNumber << " (TargetSpeed " 
                            << (cmd.targetSpeed > 0.1 ? "ACTIVE" : "OFF") << ")";
      }
    }
    
    m_mqtt->sendControlCommand(json);
    } // if (s_mqttBackupTimer.elapsed() >= 100)
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
  QString sessionId = QStringLiteral("0");
  const SessionContext* ctx = m_activeContext.load(std::memory_order_acquire);
  if (ctx) {
    vin = QString::fromUtf8(ctx->vin);
    sessionId = QString::fromUtf8(ctx->sessionId);
  }

  qInfo().noquote() << "[Client][Control][UI] type=" << type << "traceId=" << traceId.left(16)
                    << "vin=" << (vin.isEmpty() && m_vehicles ? m_vehicles->currentVin() : vin)
                    << "sessionId=" << sessionId
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
  if (m_signer.isReady()) {
    m_signer.sign(json);
  }
  sendRawControlJson(json);
}

void VehicleControlService::setGear(int gear) {
  IInputDevice::InputState input = m_latestInput.load();
  if (input.gear != gear) {
    input.gear = gear;
    input.timestamp = TimeUtils::nowUs();
    m_latestInput.store(input);
    
    // ★ 核心修复：同步到硬件采样器，防止下次采样被旧状态覆盖
    if (m_sampler) {
      m_sampler->syncDeviceState(input);
    }
    
    qInfo() << "[Client][VehicleControlService] gear updated to" << gear << "(sync-to-device)";
  }
}

void VehicleControlService::sendDriveCommand(double steering, double throttle, double brake, double targetSpeed) {
  IInputDevice::InputState input = m_latestInput.load();
  input.steeringAngle = steering;
  input.throttle = throttle;
  input.brake = brake;
  input.targetSpeed = targetSpeed;
  input.timestamp = TimeUtils::nowUs();
  m_latestInput.store(input);
  if (teleopTraceEnvEnabled()) {
    static qint64 s_lastDriveTraceMs = 0;
    const qint64 wallMs = TimeUtils::nowMs();
    if (wallMs - s_lastDriveTraceMs >= 100) {
      s_lastDriveTraceMs = wallMs;
      qInfo().noquote() << "[Client][Teleop][TRACE][sendDriveCommand] steer=" << steering
                        << " thr=" << throttle << " brk=" << brake << " spd=" << targetSpeed;
    }
  }
  pingSafety();
}

void VehicleControlService::requestEmergencyStop() {
  // ★ 安全增强：更新最新输入状态，确保控制环后续 tick 维持急停
  IInputDevice::InputState input = m_latestInput.load();
  input.emergencyStop = true;
  input.timestamp = TimeUtils::nowUs();
  m_latestInput.store(input);
  
  if (m_sampler) {
    m_sampler->syncDeviceState(input);
  }

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
  // ★ 核心修复：改用 onControlTick 仅代表控制环心跳，不 reset 操作员活跃度，解决看门狗误报。
  if (QThread::currentThread() == s->thread()) {
    s->onControlTick();
  } else {
    QMetaObject::invokeMethod(s, &SafetyMonitorService::onControlTick, Qt::QueuedConnection);
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
