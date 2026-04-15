#pragma once
#include "../core/commandsigner.h"
#include "../infrastructure/hardware/IInputDevice.h"
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

/**
 * 车辆控制服务（《客户端架构设计》§3.3.1 完整实现）。
 * 2025/2026 规范要求：核心控制链路去锁、无锁队列、禁止 shared_ptr。
 */
class VehicleControlService : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY(VehicleControlService)

 public:
  struct ControlConfig {
    int controlRateHz = 100;
    double maxSteeringRateRadPerSec = 2.0;
    double steeringCurve = 1.5;
    double throttleDeadzone = 0.02;
    double brakeDeadzone = 0.02;
    double maxSpeedKmh = 80.0;
    bool enablePrediction = true;
  };

  struct ControlCommand {
    double steeringAngle = 0.0;
    double throttle = 0.0;
    double brake = 0.0;
    int gear = 0;
    bool emergencyStop = false;
    int64_t timestamp = 0;
    uint32_t sequenceNumber = 0;
    double predictedSteeringAngle = 0.0;
    double predictionHorizonMs = 0.0;
  };

  explicit VehicleControlService(MqttController* mqtt, VehicleManager* vehicles,
                                 WebRtcStreamManager* webrtc, InputSampler* sampler,
                                 QObject* parent = nullptr);
  ~VehicleControlService() override;

  void setSafetyMonitor(SafetyMonitorService* safety) { m_safety = safety; }
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
  Q_INVOKABLE void sendDriveCommand(double steering, double throttle, double brake);
  Q_INVOKABLE void requestEmergencyStop();
  Q_INVOKABLE void sendRawControlJson(const QJsonObject& obj);

  Q_SIGNAL void requestDataChannelSend(const QByteArray& payload);

  void setCurrentRTT(double rttMs) { m_currentRTTMs = rttMs; }

  uint32_t commandsPerSecond() const { return m_commandsPerSec.load(); }

  void notifyCommandSent(const ControlCommand& cmd);
  void notifySafetyWarning(const QString& reason);
  void notifyEmergencyStopActivated(const QString& reason);

 signals:
  void commandSent(const ControlCommand& cmd);
  void safetyWarning(const QString& reason);
  void emergencyStopActivated(const QString& reason);

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
  QString m_currentVin = {};
  QString m_sessionId = {};
  mutable QMutex m_credentialsMutex;

  std::atomic<IInputDevice::InputState> m_latestInput{};
  ControlCommand m_lastCommand;

  friend class VehicleControlWorker;
  std::unique_ptr<VehicleControlWorker> m_worker;

  std::atomic<uint32_t> m_seqCounter{0};
  std::atomic<uint32_t> m_commandsPerSec{0};
  std::atomic<uint32_t> m_ticksThisSecond{0};
  std::atomic<int64_t> m_secondStart{0};
  std::atomic<double> m_currentRTTMs{50.0};

  std::atomic<bool> m_running{false};

  QPointer<SafetyMonitorService> m_safety = nullptr;
  ITransportManager* m_transport = nullptr;
};
