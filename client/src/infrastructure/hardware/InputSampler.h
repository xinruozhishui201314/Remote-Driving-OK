#pragma once
#include <QObject>
#include <QThread>
#include <QTimer>
#include <memory>
#include <atomic>
#include "IInputDevice.h"
#include "../../utils/TimeUtils.h"

/**
 * 高精度输入采样器（《客户端架构设计》§3.1.3）。
 * 200Hz 精确定时，高优先级独立线程。
 * 输入滤波：死区处理 + 指数平滑。
 */
class InputSampler : public QObject {
    Q_OBJECT

public:
    struct FilterConfig {
        double steeringDeadzone = 0.02;
        double throttleDeadzone = 0.02;
        double brakeDeadzone    = 0.02;
        double smoothingFactor  = 0.15; // 指数平滑系数（0=无平滑, 1=无滤波）
    };

    explicit InputSampler(QObject* parent = nullptr);
    ~InputSampler() override;

    void setDevice(std::shared_ptr<IInputDevice> device);
    void setFilterConfig(const FilterConfig& cfg) { m_filterCfg = cfg; }

    void start(uint32_t sampleRateHz = 200);
    void stop();

    // 原子读取最新输入（无锁，供控制线程使用）
    IInputDevice::InputState latestInput() const { return m_latestInput.load(); }

signals:
    void inputSampled(const IInputDevice::InputState& state);

private slots:
    void onTimer();

private:
    IInputDevice::InputState applyFilter(const IInputDevice::InputState& raw);
    static double applyDeadzone(double value, double deadzone);
    static double applySmoothing(double current, double target, double factor);

    std::shared_ptr<IInputDevice> m_device;
    FilterConfig m_filterCfg;
    std::atomic<IInputDevice::InputState> m_latestInput{};
    IInputDevice::InputState m_previousFiltered{};

    QThread* m_samplerThread = nullptr;
    QTimer*  m_timer         = nullptr;
};
