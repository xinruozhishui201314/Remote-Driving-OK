#include "KeyboardMouseInput.h"

#include <QDebug>
#include <QMutexLocker>
#include <Qt>

#include <algorithm>
#include <cmath>

KeyboardMouseInput::KeyboardMouseInput(QObject* parent)
    : IInputDevice(parent), m_pressedKeys(), m_mutex(), m_state() {}

bool KeyboardMouseInput::initialize() {
  qInfo() << "[Client][KeyboardMouseInput] initialized";
  return true;
}

void KeyboardMouseInput::shutdown() {
  QMutexLocker lock(&m_mutex);
  m_pressedKeys.clear();
  m_state = InputState{};
}

IInputDevice::InputState KeyboardMouseInput::poll() {
  QMutexLocker lock(&m_mutex);
  updateFromKeys();
  return m_state;
}

IInputDevice::DeviceCapabilities KeyboardMouseInput::capabilities() const {
  return {false, false, false, false, 2, 10, "KeyboardMouseInput"};
}

void KeyboardMouseInput::onKeyPressed(int qtKey) {
  QMutexLocker lock(&m_mutex);
  m_pressedKeys.insert(qtKey);

  if (qtKey == Qt::Key_Escape) {
    m_state.emergencyStop = true;
    qWarning() << "[Client][KeyboardMouseInput] EMERGENCY STOP key pressed";
  }
  if (qtKey == Qt::Key_Space) {
    m_state.handbrake = true;
  }
}

void KeyboardMouseInput::onKeyReleased(int qtKey) {
  QMutexLocker lock(&m_mutex);
  m_pressedKeys.remove(qtKey);

  if (qtKey == Qt::Key_Escape) {
    m_state.emergencyStop = false;
  }
  if (qtKey == Qt::Key_Space) {
    m_state.handbrake = false;
  }
}

void KeyboardMouseInput::onMouseMoved(double dx, double /*dy*/) {
  QMutexLocker lock(&m_mutex);
  // 水平鼠标移动辅助方向盘（可选）
  m_state.steeringAngle = std::clamp(m_state.steeringAngle + dx * 0.005, -1.0, 1.0);
}

void KeyboardMouseInput::updateFromKeys() {
  // 方向盘
  if (m_pressedKeys.contains(Qt::Key_A) || m_pressedKeys.contains(Qt::Key_Left)) {
    m_state.steeringAngle = std::max(-1.0, m_state.steeringAngle - kSteeringStep);
  } else if (m_pressedKeys.contains(Qt::Key_D) || m_pressedKeys.contains(Qt::Key_Right)) {
    m_state.steeringAngle = std::min(1.0, m_state.steeringAngle + kSteeringStep);
  } else {
    // 回中
    if (m_state.steeringAngle > 0)
      m_state.steeringAngle = std::max(0.0, m_state.steeringAngle - kDecayRate);
    else
      m_state.steeringAngle = std::min(0.0, m_state.steeringAngle + kDecayRate);
  }

  // 油门
  if (m_pressedKeys.contains(Qt::Key_W) || m_pressedKeys.contains(Qt::Key_Up)) {
    m_state.throttle = std::min(1.0, m_state.throttle + kThrottleStep);
  } else {
    m_state.throttle = std::max(0.0, m_state.throttle - kDecayRate * 2);
  }

  // 刹车
  if (m_pressedKeys.contains(Qt::Key_S) || m_pressedKeys.contains(Qt::Key_Down)) {
    m_state.brake = std::min(1.0, m_state.brake + kBrakeStep);
  } else {
    m_state.brake = std::max(0.0, m_state.brake - kDecayRate * 2);
  }
}
