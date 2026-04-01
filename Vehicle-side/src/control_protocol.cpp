#include "control_protocol.h"
#include "vehicle_controller.h"
#include "common/logger.h"

#include <iostream>
#include <tuple>
#include <regex>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <string>

// 检查推流进程是否在运行
static bool is_streaming_running()
{
    // 检查 PID 文件是否存在且进程存活
    const char* pidfile_dir = std::getenv("PIDFILE_DIR");
    if (!pidfile_dir || pidfile_dir[0] == '\0')
        pidfile_dir = "/tmp";
    
    std::string pidfile_nuscenes = std::string(pidfile_dir) + "/push-nuscenes-cameras.pid";
    std::string pidfile_testpattern = std::string(pidfile_dir) + "/push-testpattern.pid";
    
    // 检查 nuscenes 推流脚本的 PID 文件
    FILE* fp = fopen(pidfile_nuscenes.c_str(), "r");
    if (fp) {
        int pid = 0;
        if (fscanf(fp, "%d", &pid) == 1 && pid > 0) {
            fclose(fp);
            // 检查进程是否存活
            std::string check_cmd = "kill -0 " + std::to_string(pid) + " 2>/dev/null";
            int ret = std::system(check_cmd.c_str());
            if (ret == 0) {
                // 检查进程是否真的是推流脚本
                std::string cmdline_file = "/proc/" + std::to_string(pid) + "/cmdline";
                fp = fopen(cmdline_file.c_str(), "r");
                if (fp) {
                    char cmdline[512] = {0};
                    if (fread(cmdline, 1, sizeof(cmdline) - 1, fp) > 0) {
                        fclose(fp);
                        std::string cmdline_str(cmdline);
                        if (cmdline_str.find("push-nuscenes-cameras-to-zlm.sh") != std::string::npos) {
                            return true;
                        }
                    } else {
                        fclose(fp);
                    }
                }
            }
        } else {
            fclose(fp);
        }
    }
    
    // 检查 testpattern 推流脚本的 PID 文件
    fp = fopen(pidfile_testpattern.c_str(), "r");
    if (fp) {
        int pid = 0;
        if (fscanf(fp, "%d", &pid) == 1 && pid > 0) {
            fclose(fp);
            std::string check_cmd = "kill -0 " + std::to_string(pid) + " 2>/dev/null";
            int ret = std::system(check_cmd.c_str());
            if (ret == 0) {
                std::string cmdline_file = "/proc/" + std::to_string(pid) + "/cmdline";
                fp = fopen(cmdline_file.c_str(), "r");
                if (fp) {
                    char cmdline[512] = {0};
                    if (fread(cmdline, 1, sizeof(cmdline) - 1, fp) > 0) {
                        fclose(fp);
                        std::string cmdline_str(cmdline);
                        if (cmdline_str.find("push-testpattern-to-zlm.sh") != std::string::npos) {
                            return true;
                        }
                    } else {
                        fclose(fp);
                    }
                }
            }
        } else {
            fclose(fp);
        }
    }
    
    // 检查是否有 ffmpeg 进程正在推流（备用检查）
    // ★ 修复：使用更精确的检查，避免匹配到 pgrep 命令本身
    const char* zlm_host = std::getenv("ZLM_HOST");
    const char* zlm_port = std::getenv("ZLM_RTMP_PORT");
    const char* app = std::getenv("ZLM_APP");
    const char* vin_env = std::getenv("VEHICLE_VIN");
    if (!vin_env) vin_env = std::getenv("VIN");
    if (!zlm_host) zlm_host = "127.0.0.1";
    if (!zlm_port) zlm_port = "1935";
    if (!app) app = "teleop";
    
    // 多车隔离：流名加 VIN 前缀，格式 {vin}_cam_front；VIN 为空时退化为 cam_front
    std::string vin_prefix = (vin_env && vin_env[0]) ? (std::string(vin_env) + "_") : "";
    std::string rtmp_base = std::string("rtmp://") + zlm_host + ":" + zlm_port + "/" + app;
    std::string target_stream = rtmp_base + "/" + vin_prefix + "cam_front";
    // ★ 使用 ps + grep 组合，排除 grep 和 pgrep 进程本身
    std::string check_cmd = "ps aux | grep -E 'ffmpeg.*" + target_stream + "' | grep -v grep | grep -v pgrep >/dev/null 2>&1";
    int ret = std::system(check_cmd.c_str());
    return (ret == 0);
}

// 启动推流脚本（测试图案或 nuscenes -> ZLM），脚本路径由 VEHICLE_PUSH_SCRIPT 指定
// ★ 改进：检查现有进程，避免重复启动；容器重启后自动恢复推流
static void run_dataset_push_script()
{
    // ★ 先检查推流是否已在运行（精确定位：记录检查结果）
    bool already = is_streaming_running();
    std::cout << "[Vehicle-side][ZLM][Push] is_streaming_running()=" << (already ? "true" : "false") << "（收到 start_stream 后检查）" << std::endl;
    if (already) {
        std::cout << "[Vehicle-side][ZLM][Push] 推流进程已在运行，跳过启动" << std::endl;
        std::cout << "[Vehicle-side][ZLM][Push] 提示: 如需重启推流，请先停止现有进程或删除 PID 文件" << std::endl;
        return;
    }
    
    const char* script = std::getenv("VEHICLE_PUSH_SCRIPT");
    if (!script || script[0] == '\0')
        script = "scripts/push-nuscenes-cameras-to-zlm.sh";
    const char* zlm_host = std::getenv("ZLM_HOST");
    const char* zlm_port = std::getenv("ZLM_RTMP_PORT");
    std::cout << "[Vehicle-side][ZLM][Push] 执行推流脚本 script=" << script
              << " ZLM_HOST=" << (zlm_host ? zlm_host : "(未设置)")
              << " ZLM_RTMP_PORT=" << (zlm_port ? zlm_port : "(未设置)") << std::endl;
    
    // ★ 使用 nohup 和重定向，确保进程在后台稳定运行
    // 导出 VEHICLE_VIN 供推流脚本用于 VIN 前缀流名（多车隔离）
    const char* vin_env2 = std::getenv("VEHICLE_VIN");
    if (!vin_env2) vin_env2 = std::getenv("VIN");
    std::string vin_export = (vin_env2 && vin_env2[0])
        ? (std::string("VEHICLE_VIN=") + vin_env2 + " VIN=" + vin_env2 + " ")
        : "";
    std::string cmd = "nohup env " + vin_export + "bash ";
    cmd += script;
    cmd += " >/tmp/push-stream.log 2>&1 &";
    std::cout << "[Vehicle-side][ZLM][Push] 命令: " << cmd << std::endl;
    int r = std::system(cmd.c_str());
    std::cout << "[Vehicle-side][ZLM][Push] system() 返回: " << r << "（0=成功 fork 推流脚本）" << std::endl;
    std::cout << "[Vehicle-side][ZLM][Push] 推流脚本已 fork，ZLM 流约 5~15s 后可用，客户端将自动重试拉流；脚本日志 /tmp/push-stream.log" << std::endl;
    
    // ★ 等待一小段时间，检查进程是否成功启动
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    bool after500 = is_streaming_running();
    std::cout << "[Vehicle-side][ZLM][Push] 500ms 后 is_streaming_running()=" << (after500 ? "true" : "false") << std::endl;
    if (after500) {
        std::cout << "[Vehicle-side][ZLM][Push] ✓ 推流进程已成功启动（脚本或 ffmpeg 已检测到）" << std::endl;
    } else {
        std::cerr << "[Vehicle-side][ZLM][Push] 警告: 推流脚本执行后 500ms 内未检测到运行中的进程" << std::endl;
        std::cerr << "[Vehicle-side][ZLM][Push] 提示: 请检查日志 /tmp/push-stream.log" << std::endl;
        std::cerr << "[Vehicle-side][ZLM][Push] 提示: 如果使用 NuScenes 数据集，请检查数据集路径是否正确挂载" << std::endl;
        std::cerr << "[Vehicle-side][ZLM][Push] 提示: 或使用测试图案脚本: VEHICLE_PUSH_SCRIPT=/app/scripts/push-testpattern-to-zlm.sh" << std::endl;
    }
    // ★ 再等 2s 后打一条日志，便于分析 ZLM 流就绪时间
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    bool after2s = is_streaming_running();
    std::cout << "[Vehicle-side][ZLM][Push] 2.5s 后 is_streaming_running()=" << (after2s ? "true" : "false") << std::endl;
}

// 停止推流进程（通过PID文件或进程名）
static void stop_streaming_processes()
{
    const char* pidfile_dir = std::getenv("PIDFILE_DIR");
    if (!pidfile_dir || pidfile_dir[0] == '\0')
        pidfile_dir = "/tmp";
    
    std::string pidfile_nuscenes = std::string(pidfile_dir) + "/push-nuscenes-cameras.pid";
    std::string pidfile_testpattern = std::string(pidfile_dir) + "/push-testpattern.pid";
    
    // 停止 nuscenes 推流脚本
    FILE* fp = fopen(pidfile_nuscenes.c_str(), "r");
    if (fp) {
        int pid = 0;
        if (fscanf(fp, "%d", &pid) == 1 && pid > 0) {
            fclose(fp);
            std::cout << "[Vehicle-side][ZLM][Push] 停止推流脚本进程 PID=" << pid << std::endl;
            // 发送 SIGTERM 信号，让脚本优雅退出
            std::string kill_cmd = "kill -TERM " + std::to_string(pid) + " 2>/dev/null";
            int ret = std::system(kill_cmd.c_str());
            if (ret == 0) {
                std::cout << "[Vehicle-side][ZLM][Push] ✓ 已发送停止信号给推流脚本 (PID=" << pid << ")" << std::endl;
            } else {
                // 如果进程不存在，清理PID文件
                std::cout << "[Vehicle-side][ZLM][Push] 推流脚本进程不存在，清理PID文件" << std::endl;
            }
        } else {
            fclose(fp);
        }
        // 删除PID文件
        std::remove(pidfile_nuscenes.c_str());
    }
    
    // 停止 testpattern 推流脚本
    fp = fopen(pidfile_testpattern.c_str(), "r");
    if (fp) {
        int pid = 0;
        if (fscanf(fp, "%d", &pid) == 1 && pid > 0) {
            fclose(fp);
            std::cout << "[Vehicle-side][ZLM][Push] 停止测试图案推流脚本进程 PID=" << pid << std::endl;
            std::string kill_cmd = "kill -TERM " + std::to_string(pid) + " 2>/dev/null";
            int ret = std::system(kill_cmd.c_str());
            if (ret == 0) {
                std::cout << "[Vehicle-side][ZLM][Push] ✓ 已发送停止信号给测试图案推流脚本 (PID=" << pid << ")" << std::endl;
            }
        } else {
            fclose(fp);
        }
        std::remove(pidfile_testpattern.c_str());
    }
    
    // 停止所有相关的 ffmpeg 进程（备用清理）
    const char* zlm_host = std::getenv("ZLM_HOST");
    const char* zlm_port = std::getenv("ZLM_RTMP_PORT");
    const char* app = std::getenv("ZLM_APP");
    if (!zlm_host) zlm_host = "127.0.0.1";
    if (!zlm_port) zlm_port = "1935";
    if (!app) app = "teleop";
    
    std::string rtmp_base = std::string("rtmp://") + zlm_host + ":" + zlm_port + "/" + app;
    std::string kill_ffmpeg_cmd = "pkill -TERM -f 'ffmpeg.*" + rtmp_base + "/cam_front' 2>/dev/null || true";
    std::ignore = std::system(kill_ffmpeg_cmd.c_str());
    std::cout << "[Vehicle-side][ZLM][Push] ✓ 已尝试停止所有相关 ffmpeg 进程 rtmp_base=" << rtmp_base << std::endl;
    
    // 等待一小段时间，确认进程已停止
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    bool still_running = is_streaming_running();
    if (!still_running) {
        std::cout << "[Vehicle-side][ZLM][Push] ✓ 推流进程已成功停止" << std::endl;
    } else {
        std::cout << "[Vehicle-side][ZLM][Push] 警告: 推流进程可能仍在运行，请检查日志" << std::endl;
    }
}

// 已将防重放逻辑移至 VehicleController::processCommand (带 seq/timestamp 参数版本)

// ★ 推流状态跟踪：记录是否应该推流（收到 start_stream 后为 true）
static bool g_streaming_should_be_running = false;
static auto g_last_streaming_check_time = std::chrono::steady_clock::now();

bool handle_control_json(VehicleController* controller,
                         const std::string& jsonPayload)
{
    if (!controller) {
        std::cerr << "[Vehicle-side][Control] controller is null, ignore message" << std::endl;
        return false;
    }

    try {
        double steering = 0.0;
        double throttle = 0.0;
        double brake = 0.0;
        int gear = 1;
        std::string vin;
        std::string sessionId;
        int schemaVersion = 0;
        long long seq = 0;
        long long timestampMs = 0;
        std::string nonce; // Plan §9.2: Extract nonce for security/audit
        std::string signature; // Plan §9.2: Extract signature for integrity check

        // 提取数字与控制字段
        std::regex type_regex("\"type\"\\s*:\\s*\"([^\"]+)\"");
        std::regex num_regex("\"(schemaVersion|seq|timestampMs|steering|throttle|brake|gear)\"\\s*:\\s*([+-]?\\d+\\.?\\d*)");
        std::regex vin_regex("\"vin\"\\s*:\\s*\"([^\"]+)\"");
        std::regex session_regex("\"sessionId\"\\s*:\\s*\"([^\"]+)\"");
        std::regex nonce_regex("\"nonce\"\\s*:\\s*\"([^\"]+)\""); // Plan §9.2
        std::regex sig_regex("\"signature\"\\s*:\\s*\"([^\"]+)\""); // Plan §9.2

        std::smatch match;
        std::string typeStr;
        if (std::regex_search(jsonPayload, match, type_regex)) {
            typeStr = match[1].str();
            std::cout << "[GEAR] [Control] 收到消息类型: " << typeStr << std::endl;
            std::cout.flush();
            // 提取消息中的 VIN（用于 start_stream/stop_stream 的 VIN 过滤）
            std::string msg_vin;
            if (std::regex_search(jsonPayload, match, vin_regex))
                msg_vin = match[1].str();
            const char* cfg_vin_env = std::getenv("VEHICLE_VIN");
            std::string cfg_vin = (cfg_vin_env && cfg_vin_env[0] != '\0') ? cfg_vin_env : "";

            if (typeStr == "start_stream") {
                // ★ 仅当未配置 VEHICLE_VIN，或消息无 VIN，或消息 VIN 与配置一致时才响应
                if (!cfg_vin.empty() && !msg_vin.empty() && msg_vin != cfg_vin) {
                    std::cout << "[Vehicle-side][Control] 忽略 start_stream（VIN 不匹配: 消息 vin=" << msg_vin << ", 本车 VEHICLE_VIN=" << cfg_vin << "）" << std::endl;
                    return true;
                }
                std::cout << "[Vehicle-side][Control] 本车响应 start_stream (msg_vin=" << msg_vin << ", VEHICLE_VIN=" << cfg_vin << ")，检查并启动数据集推流" << std::endl;
                // ★ 标记推流应该运行，然后启动推流
                g_streaming_should_be_running = true;
                g_last_streaming_check_time = std::chrono::steady_clock::now();
                // ★ 如果推流未运行，启动推流；如果已在运行，保持运行状态
                run_dataset_push_script();
                return true;
            }
            if (typeStr == "stop_stream") {
                if (!cfg_vin.empty() && !msg_vin.empty() && msg_vin != cfg_vin) {
                    std::cout << "[Vehicle-side][Control] 忽略 stop_stream（VIN 不匹配: 消息 vin=" << msg_vin << ", 本车 VEHICLE_VIN=" << cfg_vin << "）" << std::endl;
                    return true;
                }
                std::cout << "[Vehicle-side][ZLM][Push] 收到 stop_stream（Control 已校验 VIN），停止推流进程" << std::endl;
                // ★ 标记推流应该停止
                g_streaming_should_be_running = false;
                // ★ 停止推流进程
                stop_streaming_processes();
                return true;
            }
            if (typeStr == "remote_control") {
                // 提取 enable 字段
                std::regex enable_regex("\"enable\"\\s*:\\s*(true|false)");
                std::smatch enable_match;
                bool enable = false;
                if (std::regex_search(jsonPayload, enable_match, enable_regex)) {
                    enable = (enable_match[1].str() == "true");
                }
                std::cout << "[Vehicle-side][Control] ✓✓✓ 收到 remote_control，远驾接管状态: " << (enable ? "启用" : "禁用") << std::endl;
                std::cout.flush();  // 强制刷新输出
                // ★ 设置远驾接管状态变量（供车辆控制逻辑使用）
                if (controller) {
                    std::cout << "[Vehicle-side][Control] 准备调用 setRemoteControlEnabled(" << (enable ? "true" : "false") << ")..." << std::endl;
                    std::cout.flush();
                    controller->setRemoteControlEnabled(enable);
                    std::cout << "[Vehicle-side][Control] setRemoteControlEnabled 调用完成" << std::endl;
                    std::cout.flush();
                    // 根据远驾接管状态设置驾驶模式
                    if (enable) {
                        std::cout << "[Vehicle-side][Control] 准备调用 setDrivingMode(REMOTE_DRIVING)..." << std::endl;
                        std::cout.flush();
                        controller->setDrivingMode(VehicleController::DrivingMode::REMOTE_DRIVING);
                        std::cout << "[Vehicle-side][Control] ✓ 远驾接管已启用，驾驶模式设置为: 远驾" << std::endl;
                        std::cout.flush();
                    } else {
                        // 禁用远驾接管时，恢复为自驾模式
                        std::cout << "[Vehicle-side][Control] 准备调用 setDrivingMode(AUTONOMOUS)..." << std::endl;
                        std::cout.flush();
                        controller->setDrivingMode(VehicleController::DrivingMode::AUTONOMOUS);
                        std::cout << "[Vehicle-side][Control] ✓ 远驾接管已禁用，驾驶模式恢复为: 自驾" << std::endl;
                        std::cout.flush();
                    }
                    std::cout << "[Vehicle-side][Control] ✓✓✓ 远驾接管状态已设置: " << (enable ? "启用" : "禁用") << "，准备返回 true" << std::endl;
                    std::cout.flush();
                    return true;  // ★ 明确返回 true，表示处理成功
                } else {
                    std::cerr << "[Vehicle-side][Control] ✗✗✗ 警告: controller 为空，无法设置远驾接管状态，返回 false" << std::endl;
                    return false;  // controller 为空时返回 false
                }
            }
            if (typeStr == "gear") {
                // ★ 处理单独的档位命令
                std::cout << "[GEAR] ========== [Control] 收到档位命令 ==========" << std::endl;
                std::cout.flush();
                // 提取 value 字段（档位命令使用 value 字段）
                std::regex value_regex("\"value\"\\s*:\\s*([+-]?\\d+)");
                std::smatch value_match;
                int gearValue = 0;
                if (std::regex_search(jsonPayload, value_match, value_regex)) {
                    gearValue = std::stoi(value_match[1].str());
                    std::string gearStr = (gearValue == -1 ? "R" : (gearValue == 0 ? "N" : (gearValue == 1 ? "D" : (gearValue == 2 ? "P" : "未知"))));
                    std::cout << "[GEAR] ✓ 从 JSON 中提取档位值: " << gearValue << " (" << gearStr << ")" << std::endl;
                    std::cout.flush();
                } else {
                    std::cerr << "[GEAR] ✗✗✗ 警告: 未找到 value 字段，使用默认档位 0 (N)" << std::endl;
                    std::cerr.flush();
                }
                
                // 检查远驾接管状态
                bool remoteControlEnabled = controller ? controller->isRemoteControlEnabled() : false;
                if (!remoteControlEnabled) {
                    std::cout << "[GEAR] ⚠ 远驾接管未启用，忽略档位命令" << std::endl;
                    std::cout.flush();
                    return false;
                }
                
                // 应用档位命令
                if (controller) {
                    VehicleController::ControlCommand currentCmd = controller->getCurrentCommand();
                    std::cout << "[GEAR] 当前控制指令: steering=" << currentCmd.steering 
                              << ", throttle=" << currentCmd.throttle 
                              << ", brake=" << currentCmd.brake 
                              << ", gear=" << currentCmd.gear << std::endl;
                    std::cout.flush();
                    
                    // 只更新档位，保持其他控制参数不变
                    controller->processCommand(currentCmd.steering, currentCmd.throttle, currentCmd.brake, gearValue);
                    std::string gearStr = (gearValue == -1 ? "R" : (gearValue == 0 ? "N" : (gearValue == 1 ? "D" : (gearValue == 2 ? "P" : "未知"))));
                    std::cout << "[GEAR] ✓✓✓ 档位命令已应用: " << gearValue << " (" << gearStr << ")" << std::endl;
                    std::cout.flush();
                    std::cout << "[GEAR] ========== [Control] 档位命令处理完成 ==========" << std::endl;
                    std::cout.flush();
                    return true;
                } else {
                    std::cerr << "[GEAR] ✗✗✗ 警告: controller 为空，无法应用档位命令" << std::endl;
                    std::cerr.flush();
                    return false;
                }
            }
            if (typeStr == "sweep") {
                // ★ 处理清扫命令
                std::cout << "[SWEEP] ========== [Control] 收到清扫命令 ==========" << std::endl;
                std::cout.flush();
                
                // 提取 sweepType 和 active 字段
                std::regex sweepType_regex("\"sweepType\"\\s*:\\s*\"([^\"]+)\"");
                std::regex active_regex("\"active\"\\s*:\\s*(true|false)");
                std::smatch sweepType_match, active_match;
                std::string sweepTypeStr = "sweep";
                bool sweepActive = false;
                
                if (std::regex_search(jsonPayload, sweepType_match, sweepType_regex)) {
                    sweepTypeStr = sweepType_match[1].str();
                }
                if (std::regex_search(jsonPayload, active_match, active_regex)) {
                    sweepActive = (active_match[1].str() == "true");
                }
                
                std::cout << "[SWEEP] ✓ 从 JSON 中提取清扫信息: type=" << sweepTypeStr << ", active=" << (sweepActive ? "true" : "false") << std::endl;
                std::cout.flush();
                
                // 检查远驾接管状态
                bool remoteControlEnabled = controller ? controller->isRemoteControlEnabled() : false;
                if (!remoteControlEnabled) {
                    std::cout << "[SWEEP] ⚠ 远驾接管未启用，忽略清扫命令" << std::endl;
                    std::cout.flush();
                    return false;
                }
                
                // ★ 应用清扫命令
                if (controller) {
                    if (sweepTypeStr == "sweep") {
                        controller->setSweepActive(sweepActive);
                        std::cout << "[SWEEP] ✓✓✓ 清扫命令已应用: type=" << sweepTypeStr << ", active=" << (sweepActive ? "true" : "false") << std::endl;
                        std::cout.flush();
                    } else {
                        std::cout << "[SWEEP] ⚠ 暂不支持清扫类型: " << sweepTypeStr << std::endl;
                        std::cout.flush();
                    }
                    std::cout << "[SWEEP] ========== [Control] 清扫命令处理完成 ==========" << std::endl;
                    std::cout.flush();
                    return true;
                } else {
                    std::cerr << "[SWEEP] ✗✗✗ 警告: controller 为空，无法应用清扫命令" << std::endl;
                    std::cerr.flush();
                    return false;
                }
            }
            if (typeStr == "mode") {
                // ★ 处理模式/灯光命令 (Project Spec §5.5)
                std::cout << "[MODE] ========== [Control] 收到模式命令 ==========" << std::endl;
                std::cout.flush();

                // 提取 subType 字段
                std::regex subtype_regex("\"subType\"\\s*:\\s*\"([^\"]+)\"");
                std::smatch subtype_match;
                std::string subTypeStr;
                if (std::regex_search(jsonPayload, subtype_match, subtype_regex)) {
                    subTypeStr = subtype_match[1].str();
                }

                if (subTypeStr == "light") {
                    // 灯光控制
                    std::regex lightType_regex("\"lightType\"\\s*:\\s*\"([^\"]+)\"");
                    std::regex active_regex("\"active\"\\s*:\\s*(true|false)");
                    std::smatch lightType_match, active_match;
                    std::string lightTypeStr;
                    bool lightActive = false;

                    if (std::regex_search(jsonPayload, lightType_match, lightType_regex)) {
                        lightTypeStr = lightType_match[1].str();
                    }
                    if (std::regex_search(jsonPayload, active_match, active_regex)) {
                        lightActive = (active_match[1].str() == "true");
                    }

                    std::cout << "[MODE] ✓ 从 JSON 中提取灯光信息: type=" << lightTypeStr << ", active=" << (lightActive ? "true" : "false") << std::endl;
                    std::cout.flush();

                    // 检查远驾接管状态
                    bool remoteControlEnabled = controller ? controller->isRemoteControlEnabled() : false;
                    if (!remoteControlEnabled) {
                        std::cout << "[MODE] ⚠ 远驾接管未启用，忽略灯光命令" << std::endl;
                        std::cout.flush();
                        return false;
                    }

                    // TODO: 实际应用灯光控制到车辆硬件接口
                    // 例如：controller->setLightState(lightTypeStr, lightActive);
                    std::cout << "[MODE] ✓✓✓ 灯光命令已接收 (需实现硬件接口): type=" << lightTypeStr << std::endl;
                    std::cout << "[MODE] ========== [Control] 灯光命令处理完成 ==========" << std::endl;
                    std::cout.flush();
                    return true;

                } else {
                    std::cout << "[MODE] ⚠ 未知的 subType: " << subTypeStr << std::endl;
                    return false;
                }
            }
            if (typeStr == "target_speed") {
                // ★ 处理目标速度命令
                std::cout << "[SPEED] ========== [Control] 收到目标速度命令 ==========" << std::endl;
                std::cout.flush();
                
                // 提取 value 字段（目标速度命令使用 value 字段，单位：km/h）
                std::regex value_regex("\"value\"\\s*:\\s*([+-]?\\d+\\.?\\d*)");
                std::smatch value_match;
                double targetSpeedValue = 0.0;
                if (std::regex_search(jsonPayload, value_match, value_regex)) {
                    targetSpeedValue = std::stod(value_match[1].str());
                    targetSpeedValue = std::max(0.0, std::min(100.0, targetSpeedValue));  // 限制在0-100 km/h
                    std::cout << "[SPEED] ✓ 从 JSON 中提取目标速度值: " << targetSpeedValue << " km/h" << std::endl;
                    std::cout.flush();
                } else {
                    std::cerr << "[SPEED] ✗✗✗ 警告: 未找到 value 字段，使用默认目标速度 0.0 km/h" << std::endl;
                    std::cerr.flush();
                }
                
                // 检查远驾接管状态
                bool remoteControlEnabled = controller ? controller->isRemoteControlEnabled() : false;
                if (!remoteControlEnabled) {
                    std::cout << "[SPEED] ⚠ 远驾接管未启用，忽略目标速度命令" << std::endl;
                    std::cout.flush();
                    return false;
                }
                
                // ★ 应用目标速度命令（这里可以调用车辆控制器的速度设置方法）
                std::cout << "[SPEED] ✓✓✓ 目标速度命令已接收: " << targetSpeedValue << " km/h" << std::endl;
                std::cout.flush();
                std::cout << "[SPEED] ========== [Control] 目标速度命令处理完成 ==========" << std::endl;
                std::cout.flush();
                // TODO: 实际应用目标速度到车辆控制（例如设置油门/速度控制器）
                return true;
            }
            if (typeStr == "brake") {
                // ★ 处理刹车命令
                std::cout << "[BRAKE] ========== [Control] 收到刹车命令 ==========" << std::endl;
                std::cout.flush();
                
                // 提取 value 字段（刹车命令使用 value 字段，范围：0.0-1.0）
                std::regex value_regex("\"value\"\\s*:\\s*([+-]?\\d+\\.?\\d*)");
                std::smatch value_match;
                double brakeValue = 0.0;
                if (std::regex_search(jsonPayload, value_match, value_regex)) {
                    brakeValue = std::stod(value_match[1].str());
                    brakeValue = std::max(0.0, std::min(1.0, brakeValue));  // 限制在0.0-1.0
                    std::cout << "[BRAKE] ✓ 从 JSON 中提取刹车值: " << brakeValue << " (范围: 0.0-1.0)" << std::endl;
                    std::cout.flush();
                } else {
                    std::cerr << "[BRAKE] ✗✗✗ 警告: 未找到 value 字段，使用默认刹车值 0.0" << std::endl;
                    std::cerr.flush();
                }
                
                // 检查远驾接管状态
                bool remoteControlEnabled = controller ? controller->isRemoteControlEnabled() : false;
                if (!remoteControlEnabled) {
                    std::cout << "[BRAKE] ⚠ 远驾接管未启用，忽略刹车命令" << std::endl;
                    std::cout.flush();
                    return false;
                }
                
                // 应用刹车命令
                if (controller) {
                    VehicleController::ControlCommand currentCmd = controller->getCurrentCommand();
                    std::cout << "[BRAKE] 当前控制指令: steering=" << currentCmd.steering 
                              << ", throttle=" << currentCmd.throttle 
                              << ", brake=" << currentCmd.brake 
                              << ", gear=" << currentCmd.gear << std::endl;
                    std::cout.flush();
                    
                    // 只更新刹车，保持其他控制参数不变
                    controller->processCommand(currentCmd.steering, currentCmd.throttle, brakeValue, currentCmd.gear);
                    std::cout << "[BRAKE] ✓✓✓ 刹车命令已应用: " << brakeValue << " (范围: 0.0-1.0)" << std::endl;
                    std::cout.flush();
                    std::cout << "[BRAKE] ========== [Control] 刹车命令处理完成 ==========" << std::endl;
                    std::cout.flush();
                    return true;
                } else {
                    std::cerr << "[BRAKE] ✗✗✗ 警告: controller 为空，无法应用刹车命令" << std::endl;
                    std::cerr.flush();
                    return false;
                }
            }
            if (typeStr == "emergency_stop") {
                // ★ 处理急停命令（执行或解除）
                std::cout << "[EMERGENCY_STOP] ========== [Control] 收到急停命令 ==========" << std::endl;
                std::cout.flush();
                
                // 提取 enable 字段（true: 执行急停, false: 解除急停）
                std::regex enable_regex("\"enable\"\\s*:\\s*(true|false)");
                std::smatch enable_match;
                bool enableEmergencyStop = false;
                if (std::regex_search(jsonPayload, enable_match, enable_regex)) {
                    enableEmergencyStop = (enable_match[1].str() == "true");
                    std::cout << "[EMERGENCY_STOP] ✓ 从 JSON 中提取急停状态: " << (enableEmergencyStop ? "执行急停" : "解除急停") << std::endl;
                    std::cout.flush();
                } else {
                    std::cerr << "[EMERGENCY_STOP] ✗✗✗ 警告: 未找到 enable 字段，使用默认值 false（解除急停）" << std::endl;
                    std::cerr.flush();
                }
                
                // 检查远驾接管状态
                bool remoteControlEnabled = controller ? controller->isRemoteControlEnabled() : false;
                if (!remoteControlEnabled) {
                    std::cout << "[EMERGENCY_STOP] ⚠ 远驾接管未启用，忽略急停命令" << std::endl;
                    std::cout.flush();
                    return false;
                }
                
                // 应用急停命令
                if (controller) {
                    VehicleController::ControlCommand currentCmd = controller->getCurrentCommand();
                    std::cout << "[EMERGENCY_STOP] 当前控制指令: steering=" << currentCmd.steering 
                              << ", throttle=" << currentCmd.throttle 
                              << ", brake=" << currentCmd.brake 
                              << ", gear=" << currentCmd.gear << std::endl;
                    std::cout.flush();
                    
                    if (enableEmergencyStop) {
                        // 执行急停：设置刹车为1.0
                        controller->processCommand(currentCmd.steering, currentCmd.throttle, 1.0, currentCmd.gear);
                        std::cout << "[EMERGENCY_STOP] ✓✓✓ 急停命令已执行: brake=1.0" << std::endl;
                        std::cout.flush();
                    } else {
                        // 解除急停：设置刹车为0.0
                        controller->processCommand(currentCmd.steering, currentCmd.throttle, 0.0, currentCmd.gear);
                        std::cout << "[EMERGENCY_STOP] ✓✓✓ 急停命令已解除: brake=0.0" << std::endl;
                        std::cout.flush();
                    }
                    
                    std::cout << "[EMERGENCY_STOP] ========== [Control] 急停命令处理完成 ==========" << std::endl;
                    std::cout.flush();
                    return true;
                } else {
                    std::cerr << "[EMERGENCY_STOP] ✗✗✗ 警告: controller 为空，无法应用急停命令" << std::endl;
                    std::cerr.flush();
                    return false;
                }
            }
        }
        
        // ★ 检查远驾接管状态：如果未启用，则忽略控制指令（但允许 start_stream/stop_stream/remote_control/gear/sweep/target_speed/brake/emergency_stop）
        // 注意：typeStr 可能为空（如果消息中没有 type 字段），此时也允许处理（向后兼容）
        if (!typeStr.empty() && typeStr != "start_stream" && typeStr != "stop_stream" && typeStr != "remote_control") {
            bool remoteControlEnabled = controller ? controller->isRemoteControlEnabled() : false;
            if (!remoteControlEnabled) {
                std::cout << "[Vehicle-side][Control] 远驾接管未启用，忽略控制指令 type=" << typeStr << std::endl;
                return false;
            }
        }
        
        auto it = jsonPayload.cbegin();
        while (std::regex_search(it, jsonPayload.cend(), match, num_regex)) {
            const std::string key = match[1].str();
            const std::string val = match[2].str();
            
            try {
                if (key == "schemaVersion") {
                    schemaVersion = std::stoi(val);
                } else if (key == "seq") {
                    seq = static_cast<long long>(std::stoll(val));
                } else if (key == "timestampMs") {
                    timestampMs = static_cast<long long>(std::stoll(val));
                } else if (key == "steering") {
                    steering = std::clamp(std::stod(val), -1.0, 1.0);
                } else if (key == "throttle") {
                    throttle = std::clamp(std::stod(val), 0.0, 1.0);
                } else if (key == "brake") {
                    brake = std::clamp(std::stod(val), 0.0, 1.0);
                } else if (key == "gear") {
                    gear = std::stoi(val);
                    std::cout << "[GEAR] ✓ 从 JSON 中提取档位: " << gear << " (" << (gear == -1 ? "R" : (gear == 0 ? "N" : (gear == 1 ? "D" : "P"))) << ")" << std::endl;
                }
            } catch (const std::exception& e) {
                LOG_ERROR("Failed to parse control value for key {}: {}", key, e.what());
                // Apply safe defaults for critical values
                if (key == "steering") steering = 0.0;
                if (key == "throttle") throttle = 0.0;
                if (key == "brake") brake = 0.0;
                if (key == "gear") gear = 1;
                std::cout << "[GEAR] ✓ 从 JSON 中提取档位: " << gear << " (" << (gear == -1 ? "R" : (gear == 0 ? "N" : "D")) << ")" << std::endl;
                std::cout.flush();
            }
            it = match.suffix().first;
        }

        if (std::regex_search(jsonPayload, match, vin_regex)) {
            vin = match[1].str();
        }
        if (std::regex_search(jsonPayload, match, session_regex)) {
            sessionId = match[1].str();
        }
        if (std::regex_search(jsonPayload, match, nonce_regex)) {
            nonce = match[1].str();
        }
        if (std::regex_search(jsonPayload, match, sig_regex)) {
            signature = match[1].str();
        }

        // Plan 5.1 & 9.2: Audit logging of key fields
        std::cout << "[AUDIT] Control cmd parsed: "
                  << "schema=" << schemaVersion
                  << ", seq=" << seq
                  << ", ts=" << timestampMs
                  << ", vin=" << vin
                  << ", sid=" << sessionId
                  << ", nonce=" << (nonce.empty() ? "null" : nonce.substr(0, 8) + "...")
                  << ", sig=" << (signature.empty() ? "null" : signature.substr(0, 8) + "...")
                  << std::endl;

        // ★ 档位值：-1=R, 0=N, 1=D, 2=P
        int validGear = (gear == -1 || gear == 0 || gear == 1 || gear == 2) ? gear : 1;
        
        std::string gearStr = (validGear == -1 ? "R" : (validGear == 0 ? "N" : (validGear == 1 ? "D" : (validGear == 2 ? "P" : "未知"))));
        std::cout << "[GEAR] [Control] 准备应用控制指令: steering=" << steering 
                  << ", throttle=" << throttle 
                  << ", brake=" << brake 
                  << ", gear=" << validGear << " (" << gearStr << ")" << std::endl;
        std::cout.flush();

        // 调用带安全参数的 processCommand
        controller->processCommand(steering, throttle, brake, validGear, static_cast<uint32_t>(seq), timestampMs);
        std::cout << "[GEAR] [Control] ✓ 控制指令已应用" << std::endl;
        std::cout.flush();
        return true;

    } catch (const std::exception &e) {
        std::cerr << "[Vehicle-side][Control] 解析控制指令错误: " << e.what() << std::endl;
        return false;
    }
}

// ★ 推流健康检查：检查推流进程是否在运行，如果未运行且应该运行，则自动重启
bool check_and_restore_streaming()
{
    // 如果推流不应该运行，跳过检查
    if (!g_streaming_should_be_running) {
        return false;
    }
    
    // 检查推流是否在运行
    bool is_running = is_streaming_running();
    
    if (!is_running) {
        // 推流应该运行但未运行，尝试重启
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            now - g_last_streaming_check_time).count();
        
        // 避免频繁重启（至少间隔 5 秒）
        if (elapsed >= 5) {
            std::cout << "[Vehicle-side][ZLM][Push] 检测到推流进程未运行（应推流=true），尝试自动恢复..." << std::endl;
            run_dataset_push_script();
            g_last_streaming_check_time = now;
            
            // 等待一小段时间后再次检查
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            return is_streaming_running();
        }
    } else {
        // 推流正常运行，更新检查时间
        g_last_streaming_check_time = std::chrono::steady_clock::now();
    }
    
    return is_running;
}


