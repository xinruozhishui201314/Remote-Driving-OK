#pragma once
#include "../core/commandsigner.h"
#include "../infrastructure/hardware/IInputDevice.h"
#include "../core/memorymanager.h"
#include "latencycompensator.h"

#include <QJsonObject>
#include <QMutex>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QVariantMap>

#include "safetymonitorservice.h"
#include "../infrastructure/hardware/InputSampler.h"

#include <atomic>
#include <cstdint>
#include <memory>

class MqttController;
class VehicleManager;
class WebRtcStreamManager;
class ITransportManager;

class VehicleControlWorker;

struct ControlCommand {
  double steeringAngle = 0.0;
  double throttle = 0.0;
  double brake = 0.0;
  int gear = 0;
  double targetSpeed = 0.0;    // [Crucial] 驾驶员时速意图
  bool emergencyStop = false;
  int64_t timestamp = 0;
  uint32_t sequenceNumber = 0;
  double predictedSteeringAngle = 0.0;
  double predictionHorizonMs = 0.0;
};

/**
 * 车辆控制服务（《客户端架构设计》§3.3.1 完整实现）。
 * 2025/2026 规范要求：核心控制链路去锁、无锁队列、禁止 shared_ptr。
 */
class VehicleControlService : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY(VehicleControlService)

  Q_PROPERTY(bool inputLinkSilent READ inputLinkSilent NOTIFY inputLinkSilentChanged)

 public:
  struct ControlConfig {
    int controlRateHz = 100;
    double maxSteeringRateRadPerSec = 2.0;
    double steeringCurve = 1.5;
    double throttleDeadzone = 0.02;
    double brakeDeadzone = 0.02;
    double maxSpeedKmh = 80.0;
    bool enablePrediction = true;
    int silentThresholdMs = 2000; // 2秒静默检测阈值
  };

  typedef ::ControlCommand ControlCommand;

  /** 不可变会话上下文，用于热路径零锁访问 */
  struct SessionContext {
    char vin[32];
    char sessionId[64];
    char token[1024];

    SessionContext() {
      vin[0] = '\0';
      sessionId[0] = '\0';
      token[0] = '\0';
    }
  };

  explicit VehicleControlService(MqttController* mqtt, VehicleManager* vehicles,
                                 WebRtcStreamManager* webrtc, InputSampler* sampler,
                                 QObject* parent = nullptr);
  ~VehicleControlService() override;

  void setSafetyMonitor(SafetyMonitorService* safety);
  void setTransport(ITransportManager* transport) { m_transport = transport; }
  void setControlConfig(const ControlConfig& cfg);

  uint32_t nextSequenceNumber();

  Q_INVOKABLE virtual void setSessionCredentials(const QString& vin, const QString& sessionId,
                                                 const QString& token);
  Q_INVOKABLE virtual void clearSessionCredentials();

  bool initialize();
  Q_INVOKABLE virtual void start();
  Q_INVOKABLE virtual void stop();

  void attachToSafetyThread(QThread* thread);

  // 输入更新（向后兼容，但推荐使用无锁队列）
  void updateInput(const IInputDevice::InputState& input);

  Q_INVOKABLE void sendUiCommand(const QString& type, const QVariantMap& payload);
  Q_INVOKABLE void setGear(int gear);
  Q_INVOKABLE void sendDriveCommand(double steering, double throttle, double brake, double targetSpeed = 0.0);
  Q_INVOKABLE void requestEmergencyStop();
  /** 解除急停状态锁定 */
  Q_INVOKABLE void clearEmergencyStop();
  Q_INVOKABLE void sendRawControlJson(const QJsonObject& obj);

  ControlCommand lastCommand() const { return m_lastCommand; }

  bool inputLinkSilent() const { return m_inputLinkSilent.load(); }

  Q_SIGNAL void requestDataChannelSend(const QByteArray& payload);
  Q_SIGNAL void inputLinkSilentChanged(bool silent);

  void setCurrentRTT(double rttMs) { m_currentRTTMs = rttMs; }

  uint32_t commandsPerSecond() const { return m_commandsPerSec.load(); }

  void notifyCommandSent(const ControlCommand& cmd);
  void notifySafetyWarning(const QString& reason);
  void notifyEmergencyStopActivated(const QString& reason);

 signals:
  void commandSent(const ControlCommand& cmd);
  void safetyWarning(const QString& reason);
  void emergencyStopActivated(const QString& reason);
  void targetSpeedForcedChanged(double speed);

 public slots:
  void noteOperatorActivity();

 private:
  IInputDevice::InputState processInput(const IInputDevice::InputState& raw);
  ControlCommand applyRateLimit(const IInputDevice::InputState& processed);
  void sendCommand(const ControlCommand& cmd);
  void sendNeutralCommand();
  void pingSafety();

  static double applyCurve(double value, double exponent);
  static double applyDeadzone(double value, double deadzone);

  MqttController* m_mqtt = nullptr;
  VehicleManager* m_vehicles = nullptr;
  WebRtcStreamManager* m_wsm = nullptr;
  InputSampler* m_sampler = nullptr;

  ControlConfig m_config;
  std::unique_ptr<LatencyCompensator> m_predictor;
  CommandSigner m_signer;
  
  // 2025/2026 规范：使用原子指针替换互斥锁保护的会话信息
  std::atomic<SessionContext*> m_activeContext{nullptr};

  std::atomic<IInputDevice::InputState> m_latestInput{};
  IInputDevice::InputState m_lastProcessedInput{};
  ControlCommand m_lastCommand;

  friend class VehicleControlWorker;
  std::unique_ptr<VehicleControlWorker> m_worker;

  // 2025/2026 规范：热路径对象池，消除运行时内存分配
  static ObjectPool<ControlCommand, 128>& commandPool() {
    static ObjectPool<ControlCommand, 128> pool;
    return pool;
  }
  
  static ObjectPool<IInputDevice::InputState, 128>& inputPool() {
    static ObjectPool<IInputDevice::InputState, 128> pool;
    return pool;
  }

  std::atomic<uint32_t> m_seqCounter{0};
  std::atomic<uint32_t> m_commandsPerSec{0};
  std::atomic<uint32_t> m_ticksThisSecond{0};
  std::atomic<int64_t> m_secondStart{0};
  std::atomic<double> m_currentRTTMs{50.0};

  std::atomic<bool> m_running{false};

  // 输入链路看门狗状态
  std::atomic<int64_t> m_lastEffectiveInputMs{0};
  std::atomic<int64_t> m_lastOperatorActivityMs{0};
  std::atomic<bool> m_inputLinkSilent{false};

  QPointer<SafetyMonitorService> m_safety = nullptr;
  ITransportManager* m_transport = nullptr;
};
