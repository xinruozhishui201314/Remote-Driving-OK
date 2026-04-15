#pragma once
#include <QObject>

/**
 * 输入设备硬件抽象（《客户端架构设计》§3.1.3）。
 * 统一抽象方向盘/踏板/手柄/键鼠的输入。
 */
class IInputDevice : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY(IInputDevice)

 public:
  enum class DeviceType {
    STEERING_WHEEL,
    PEDAL_SET,
    GEAR_SHIFTER,
    GAMEPAD,
    JOYSTICK,
    KEYBOARD_MOUSE,
  };

  // 热路径状态结构：仅保留核心控制轴，满足 std::atomic<InputState> 的 trivially-copyable 要求。
  // 扩展轴/按钮通过 inputStateChanged(InputState) 信号按需传递，不放入此结构。
  struct InputState {
    double steeringAngle = 0.0;  // [-1.0, 1.0] 归一化
    double throttle = 0.0;       // [0.0, 1.0]
    double brake = 0.0;          // [0.0, 1.0]
    int gear = 0;                // -1=R, 0=N, 1..6=前进挡
    bool handbrake = false;
    bool emergencyStop = false;
    int64_t timestamp = 0;  // 采样时间戳（µs）
  };

  struct ForceFeedbackEffect {
    double strength = 0.0;  // [0.0, 1.0]
    double frequency = 0.0;
    QString type;  // "rumble", "spring", "damper"
  };

  struct DeviceCapabilities {
    bool hasForceFeedback = false;
    bool hasSteeringWheel = false;
    bool hasPedals = false;
    bool hasGearShifter = false;
    int axisCount = 0;
    int buttonCount = 0;
    QString deviceName;
  };

  explicit IInputDevice(QObject* parent = nullptr) : QObject(parent) {}

  virtual bool initialize() = 0;
  virtual void shutdown() = 0;
  virtual DeviceType type() const = 0;
  virtual InputState poll() = 0;
  virtual void setForceFeedback(const ForceFeedbackEffect& effect) = 0;
  virtual DeviceCapabilities capabilities() const = 0;

 signals:
  void inputStateChanged(const InputState& state);
  void deviceConnected();
  void deviceDisconnected();
};
