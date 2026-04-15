#pragma once
#include <QObject>
#include <QTimer>
#include <QMutex>

#include "core/eventbus.h"

#include <atomic>
#include <cstdint>
#include <memory>

class VehicleStatus;
class SystemStateMachine;
class VehicleControlService;

class SafetyWorker;

/**
 * 安全监控服务（《客户端架构设计》§3.3.2 完整实现）。
 *
 * 四层防护：
 *   Layer 0: 硬件级（紧急停车按钮）
 *   Layer 1: 车辆级（速度/转向限制）——由车端执行
 *   Layer 2: 通信安全（LatencyWatchdog + HeartbeatMonitor）—— 本类
 *   Layer 3: 系统级（DeadmanTimer + 状态一致性检查）—— 本类
 *   Layer 4: 人机安全（OperatorMonitor）—— 本类
 *
 * 50Hz 安全检查周期（20ms）。
 *
 * 【架构变更】为了兼容 QML (Qt 6) 线程亲和性要求：
 * - SafetyMonitorService 留在主线程（供 QML 绑定属性与信号）。
 * - 核心定时检查逻辑移至 SafetyWorker，并由 main.cpp 移入独立安全线程。
 */
class SafetyMonitorService : public QObject {
  Q_OBJECT

  Q_PROPERTY(bool emergencyActive READ emergencyActive NOTIFY emergencyActiveChanged)
  Q_PROPERTY(QString emergencyReason READ emergencyReason NOTIFY emergencyActiveChanged)
  /** 全链路熔断状态：true=一切正常，允许发控车指令；false=熔断，强制 E-STOP 层 */
  Q_PROPERTY(bool allSystemsGo READ allSystemsGo NOTIFY allSystemsGoChanged)

 public:
  struct Config {
    // Deadman timer（操作员持续操控）
    int deadmanTimeoutMs = 3000;

    // 延迟看门狗
    double maxOneWayLatencyMs = 150.0;
    double maxRoundTripMs = 250.0;
    double warningLatencyMs = 100.0;
    int emergencyTriggerCount = 3;  // 连续超限次数触发急停

    // 心跳监控
    int expectedHeartbeatIntervalMs = 100;
    int heartbeatTimeoutMs = 500;
    int missedBeforeWarning = 3;
    int missedBeforeEmergency = 5;

    // 操作员监控
    int inactivityTimeoutMs = 5000;
  };

  explicit SafetyMonitorService(VehicleStatus* vs, SystemStateMachine* fsm,
                                QObject* parent = nullptr);
  ~SafetyMonitorService() override;

  void setControlService(VehicleControlService* ctrl) { m_control = ctrl; }
  void setConfig(const Config& cfg);

  bool initialize();
  /** 启动 50Hz 安全巡检；后端会话建立后由 SessionManager 调用（幂等）。 */
  Q_INVOKABLE virtual void start();
  Q_INVOKABLE virtual void stop();

  /** 手动触发一次安全检查（主要用于单测模拟时钟或强制同步检查） */
  void runSafetyChecks();

  /** 【关键修复】将内部工作对象移至安全线程，Service 本身保留在主线程以兼容 QML */
  void attachToSafetyThread(QThread* thread);

  bool emergencyActive() const { return m_emergencyActive.load(); }
  QString emergencyReason() const {
    QMutexLocker lock(&m_reasonMutex);
    return m_emergencyReason;
  }
  bool allSystemsGo() const { return m_allSystemsGo.load(); }

  /** 单测专用：注入单调时钟；本类内所有原 TimeUtils::nowMs() 路径改为使用该时间。生产代码勿调用。
   */
  static void setUnitTestNowMsForTesting(int64_t ms);
  static void clearUnitTestClockForTesting();

  // 外部输入（直接调用）
  void updateLatency(double oneWayMs, double rttMs);
  void onHeartbeatReceived();
  /** 由 UI 线程定时更新，用于 Safety 线程监控 UI 是否卡死。 */
  void noteUiHeartbeat();

  bool isDeadmanActive() const { return m_deadmanActive.load(); }

  /** 供 Worker 跨线程触发 UI 信号 */
  void notifyEmergency(const QString& reason);
  void notifyAllSystemsGo(bool ok);

  signals:
  void safetyWarning(const QString& message);
  void speedLimitRequested(double maxKmh);
  void emergencyStopTriggered(const QString& reason);
  void safetyStatusChanged(bool allOk);
  void emergencyActiveChanged();
  void allSystemsGoChanged(bool ok);

  public slots:
  /** 订阅 EventBus 异常 */
  void onSystemError(const SystemErrorEvent& evt);
  /** VehicleControlService::pingSafety 使用 QMetaObject::invokeMethod 排队调用；必须为 slot
   *（Qt 文档：invokable 仅限 slot / Q_INVOKABLE / signal）。 */
  void onOperatorActivity();

 private:
  void triggerEmergencyStop(const QString& reason);

  Config m_config;
  VehicleStatus* m_vehicleStatus = nullptr;
  SystemStateMachine* m_fsm = nullptr;
  VehicleControlService* m_control = nullptr;

  friend class SafetyWorker;
  std::unique_ptr<SafetyWorker> m_worker;

  // Latency watchdog
  std::atomic<double> m_currentOneWayMs{0.0};
  std::atomic<double> m_currentRTTMs{0.0};
  std::atomic<int> m_latencyViolationCount{0};

  // Heartbeat monitor
  std::atomic<int64_t> m_lastHeartbeatMs{0};
  std::atomic<int> m_missedHeartbeats{0};

  // Operator / Deadman
  std::atomic<int64_t> m_lastOperatorActivityMs{0};
  std::atomic<bool> m_deadmanActive{false};

  // UI 监控（由独立安全线程检查）
  std::atomic<int64_t> m_lastUiHeartbeatMs{0};

  std::atomic<bool> m_emergencyActive{false};
  QString m_emergencyReason;
  mutable QMutex m_reasonMutex;

  std::atomic<bool> m_allSystemsGo{false};
  SubscriptionHandle m_errorSub = 0;

  std::atomic<bool> m_started{false};

 public:
  static constexpr int kSafetyCheckHz = 50;
  static constexpr int kSafetyCheckIntervalMs = 1000 / kSafetyCheckHz;  // 20ms
};
