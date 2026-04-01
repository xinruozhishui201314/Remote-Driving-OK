#ifndef ZLM_CONTROL_CHANNEL_H
#define ZLM_CONTROL_CHANNEL_H

#include <string>

class VehicleController;

// 预留给 ZLMediaKit 控制通道（WebSocket/DataChannel）的占位类
class ZlmControlChannel {
public:
    explicit ZlmControlChannel(VehicleController* controller);
    ~ZlmControlChannel();

    // 启动与 ZLM 的控制通道（当前 Gate 中仅打印日志，不做真实连接）
    bool start();
    void stop();

    // 收到来自 ZLM 的控制 JSON 时调用
    void onMessage(const std::string& json);

private:
    VehicleController* m_controller;
    bool m_started{false};
};

#endif // ZLM_CONTROL_CHANNEL_H

