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

#include <atomic>
#include <cstdint>
#include <memory>

class MqttController;
class VehicleManager;
class WebRtcStreamManager;
class ITransportManager;

/**
 * 车辆控制服务（《客户端架构设计》§3.3.1 完整实现）。
 *
 * 控制环路：
 *   InputSampler(200Hz) → updateInput() → m_latestInput
 *   Qt::PreciseTimer(100Hz) → controlTick()
 *     → processInput（非线性曲线/死区）
 *     → LatencyCompensator（RTT感知预测）
 *     → applyRateLimit（方向盘速率限制）
 *     → SafetyChecker
 *     → sendCommand（CONTROL_CRITICAL通道）
 */
class VehicleControlService : public QObject {
  Q_OBJECT

 public:
  struct ControlConfig {
    int controlRateHz = 100;
    /** 方向盘最大变化速率；与 IInputDevice::InputState::steeringAngle 一致：归一化
     * [-1,1]/秒（历史字段名含 Rad，非弧度）。 */
    double maxSteeringRateRadPerSec = 2.0;
    double steeringCurve = 1.5;  // 非线性指数
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
                                 WebRtcStreamManager* webrtc, QObject* parent = nullptr);
  ~VehicleControlService() override;

  void setSafetyMonitor(SafetyMonitorService* safety) { m_safety = safety; }
  void setTransport(ITransportManager* transport) { m_transport = transport; }
  void setControlConfig(const ControlConfig& cfg);

  /**
   * 设置会话凭证（会话建立后调用，用于 HMAC 签名）。
   * @param vin       当前驾驶的车辆 VIN
   * @param sessionId 会话 ID
   * @param token     认证令牌
   */
  virtual void setSessionCredentials(const QString& vin, const QString& sessionId,
                                     const QString& token);
  /** 登出或会话结束时清除签名与 VIN/会话上下文 */
  virtual void clearSessionCredentials();

  bool initialize();
  /** 启动控制定时器（会话建立后由 SessionManager 调用；可重复调用，幂等）。 */
  virtual void start();
  /** 停止定时器并发送空档/中立指令；登出或清空 VIN 前调用。 */
  virtual void stop();

  // 输入更新（从 InputSampler 信号连接，无锁原子写）
  void updateInput(const IInputDevice::InputState& input);

  // QML / UI 接口（向后兼容）
  Q_INVOKABLE void sendUiCommand(const QString& type, const QVariantMap& payload);
  Q_INVOKABLE void sendDriveCommand(double steering, double throttle, double brake);
  Q_INVOKABLE void requestEmergencyStop();
  Q_INVOKABLE void sendRawControlJson(const QJsonObject& obj);

  // 当前 RTT（由 SafetyMonitorService 更新）
  void setCurrentRTT(double rttMs) { m_currentRTTMs = rttMs; }

  uint32_t commandsPerSecond() const { return m_commandsPerSec.load(); }

 signals:
  void commandSent(const ControlCommand& cmd);
  void safetyWarning(const QString& reason);
  void emergencyStopActivated(const QString& reason);

 private slots:
  void controlTick();

 private:
  IInputDevice::InputState processInput(const IInputDevice::InputState& raw);
  ControlCommand applyRateLimit(const IInputDevice::InputState& processed);
  void sendCommand(const ControlCommand& cmd);
  void sendNeutralCommand();
  void pingSafety();

  static double applyCurve(double value, double exponent);
  static double applyDeadzone(double value, double deadzone);

  ControlConfig m_config;
  std::unique_ptr<LatencyCompensator> m_predictor;
  CommandSigner m_signer;
  QString m_currentVin;
  QString m_sessionId;

  std::atomic<IInputDevice::InputState> m_latestInput{};
  ControlCommand m_lastCommand;

  QTimer m_controlTimer;
  std::atomic<uint32_t> m_seqCounter{0};
  std::atomic<uint32_t> m_commandsPerSec{0};
  std::atomic<uint32_t> m_ticksThisSecond{0};
  int64_t m_secondStart = 0;
  std::atomic<double> m_currentRTTMs{50.0};

  bool m_running = false;

  // Dependencies (injected)
  MqttController* m_mqtt = nullptr;
  VehicleManager* m_vehicles = nullptr;
  WebRtcStreamManager* m_wsm = nullptr;
  QPointer<SafetyMonitorService> m_safety;
  ITransportManager* m_transport = nullptr;
};
