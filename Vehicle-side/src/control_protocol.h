#ifndef CONTROL_PROTOCOL_H
#define CONTROL_PROTOCOL_H

#include <string>

class VehicleController;

// 统一控制 JSON 入口，供 MQTT / ZLM 控制通道复用
bool handle_control_json(VehicleController* controller,
                         const std::string& jsonPayload);

// ★ 推流健康检查：检查推流进程是否在运行，如果未运行且应该运行，则自动重启
// 返回 true 表示推流正在运行，false 表示未运行
bool check_and_restore_streaming();

#endif // CONTROL_PROTOCOL_H

