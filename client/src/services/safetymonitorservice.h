#pragma once
#include <QObject>
#include <QTimer>

#include <atomic>
#include <cstdint>
#include <memory>

class VehicleStatus;
class SystemStateMachine;
class VehicleControlService;

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
 */
class SafetyMonitorService : public QObject {
  Q_OBJECT

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
  virtual void start();
  virtual void stop();

  /** 单测专用：注入单调时钟；本类内所有原 TimeUtils::nowMs() 路径改为使用该时间。生产代码勿调用。
   */
  static void setUnitTestNowMsForTesting(int64_t ms);
  static void clearUnitTestClockForTesting();

  // 外部输入（直接调用）
  void updateLatency(double oneWayMs, double rttMs);
  void onHeartbeatReceived();

  bool isDeadmanActive() const { return m_deadmanActive.load(); }

 signals:
  void safetyWarning(const QString& message);
  void speedLimitRequested(double maxKmh);
  void emergencyStopTriggered(const QString& reason);
  void safetyStatusChanged(bool allOk);

 public slots:
  void runSafetyChecks();
  /** VehicleControlService::pingSafety 使用 QMetaObject::invokeMethod 排队调用；必须为 slot
   *（Qt 文档：invokable 仅限 slot / Q_INVOKABLE / signal）。 */
  void onOperatorActivity();

 private:
  void checkLatency();
  void checkHeartbeat();
  void checkOperatorActivity();
  void checkDeadman();
  void triggerEmergencyStop(const QString& reason);

  Config m_config;
  VehicleStatus* m_vehicleStatus = nullptr;
  SystemStateMachine* m_fsm = nullptr;
  VehicleControlService* m_control = nullptr;

  QTimer m_safetyTimer;

  // Latency watchdog
  std::atomic<double> m_currentOneWayMs{0.0};
  std::atomic<double> m_currentRTTMs{0.0};
  int m_latencyViolationCount = 0;

  // Heartbeat monitor
  int64_t m_lastHeartbeatMs = 0;
  int m_missedHeartbeats = 0;

  // Operator / Deadman
  int64_t m_lastOperatorActivityMs = 0;
  std::atomic<bool> m_deadmanActive{false};

  bool m_started = false;

  static constexpr int kSafetyCheckHz = 50;
  static constexpr int kSafetyCheckIntervalMs = 1000 / kSafetyCheckHz;  // 20ms
};
