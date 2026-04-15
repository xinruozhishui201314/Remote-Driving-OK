#pragma once
#include "../../utils/LockFreeQueue.h"
#include "../../utils/TimeUtils.h"
#include "IInputDevice.h"

#include <QObject>
#include <QThread>
#include <QTimer>

#include <atomic>
#include <memory>

/**
 * 高精度输入采样器（《客户端架构设计》§3.1.3）。
 * 200Hz 精确定时，高优先级独立线程。
 * 输入滤波：死区处理 + 指数平滑。
 * 2025/2026 规范要求：无锁队列推送数据。
 */
class InputSampler : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY(InputSampler)

 public:
  struct FilterConfig {
    double steeringDeadzone = 0.02;
    double throttleDeadzone = 0.02;
    double brakeDeadzone = 0.02;
    double smoothingFactor = 0.15;
  };

  explicit InputSampler(QObject* parent = nullptr);
  ~InputSampler() override;

  // 规范要求：避免在热路径使用 shared_ptr，此处 device 生命周期由外部控制
  void setDevice(IInputDevice* device);
  void setFilterConfig(const FilterConfig& cfg) { m_filterCfg = cfg; }

  void start(uint32_t sampleRateHz = 200);
  void stop();

  // 供控制线程拉取数据的无锁队列
  SPSCQueue<IInputDevice::InputState, 512>& queue() { return m_queue; }

  IInputDevice::InputState latestInput() const { return m_latestInput.load(); }

 signals:
  void inputSampled(const IInputDevice::InputState& state);

 private slots:
  void onTimer();

 private:
  IInputDevice::InputState applyFilter(const IInputDevice::InputState& raw);
  static double applyDeadzone(double value, double deadzone);
  static double applySmoothing(double current, double target, double factor);

  IInputDevice* m_device = nullptr;
  FilterConfig m_filterCfg;
  std::atomic<IInputDevice::InputState> m_latestInput{};
  IInputDevice::InputState m_previousFiltered{};

  SPSCQueue<IInputDevice::InputState, 512> m_queue;

  QThread* m_samplerThread = nullptr;
  QTimer* m_timer = nullptr;
};
