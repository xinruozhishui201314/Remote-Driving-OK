#pragma once
#include "IInputDevice.h"

#include <QMutex>
#include <QObject>
#include <QPointF>
#include <QSet>

#include <atomic>

/**
 * 键盘/鼠标输入设备（降级方案，《客户端架构设计》§3.1.3）。
 * 通过 QML 端事件注入，不直接过滤系统事件。
 *
 * 控制映射：
 *   W / Up    → 油门
 *   S / Down  → 刹车
 *   A / Left  → 方向盘左
 *   D / Right → 方向盘右
 *   Space     → 手刹
 *   Escape    → 急停
 */
class KeyboardMouseInput : public IInputDevice {
  Q_OBJECT
  Q_DISABLE_COPY(KeyboardMouseInput)

 public:
  explicit KeyboardMouseInput(QObject* parent = nullptr);

  bool initialize() override;
  void shutdown() override;
  DeviceType type() const override { return DeviceType::KEYBOARD_MOUSE; }
  InputState poll() override;
  void setForceFeedback(const ForceFeedbackEffect&) override {}  // 不支持
  DeviceCapabilities capabilities() const override;

  // QML 调用：按键事件注入
  Q_INVOKABLE void onKeyPressed(int qtKey);
  Q_INVOKABLE void onKeyReleased(int qtKey);
  Q_INVOKABLE void onMouseMoved(double dx, double dy);

 private:
  void updateFromKeys();

  QSet<int> m_pressedKeys;
  mutable QMutex m_mutex;
  InputState m_state;

  static constexpr double kSteeringStep = 0.1;
  static constexpr double kThrottleStep = 0.15;
  static constexpr double kBrakeStep = 0.2;
  static constexpr double kDecayRate = 0.05;  // 松键后的衰减
};
