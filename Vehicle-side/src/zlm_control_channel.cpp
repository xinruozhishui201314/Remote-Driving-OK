#include "zlm_control_channel.h"
#include "control_protocol.h"
#include "vehicle_controller.h"

#include <iostream>
#include <cstdlib>

ZlmControlChannel::ZlmControlChannel(VehicleController* controller)
    : m_controller(controller)
{
}

ZlmControlChannel::~ZlmControlChannel()
{
    stop();
}

bool ZlmControlChannel::start()
{
    const char* url = std::getenv("ZLM_CONTROL_WS_URL");
    if (!url || std::string(url).empty()) {
        std::cout << "[Vehicle-side][ZLM-Control] 未设置 ZLM_CONTROL_WS_URL，控制通道未启用" << std::endl;
        return false;
    }

    // 当前 Gate 中仅打印日志，占位不建立真实连接
    std::cout << "[Vehicle-side][ZLM-Control] 预留控制通道占位，目标 URL="
              << url << "（尚未实现具体连接逻辑）" << std::endl;

    m_started = true;
    return true;
}

void ZlmControlChannel::stop()
{
    if (m_started) {
        std::cout << "[Vehicle-side][ZLM-Control] 停止控制通道（占位实现）" << std::endl;
        m_started = false;
    }
}

void ZlmControlChannel::onMessage(const std::string& json)
{
    // 统一走控制 JSON 入口
    if (!m_controller) {
        std::cerr << "[Vehicle-side][ZLM-Control] controller 为空，丢弃消息" << std::endl;
        return;
    }
    (void)handle_control_json(m_controller, json);
}

