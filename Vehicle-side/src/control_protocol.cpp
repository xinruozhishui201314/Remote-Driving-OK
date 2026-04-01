#include "control_protocol.h"
#include "vehicle_controller.h"
#include "common/logger.h"
#include "common/error_code.h"
#include "common/log_macros.h"

#include <iostream>
#include <tuple>
#include <regex>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <string>
#include <sstream>

// 检查推流进程是否在运行
static bool is_streaming_running()
{
    LOG_STREAM_TRACE("Checking if streaming process is running...");

    try {
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
                                LOG_STREAM_DEBUG("Found running nuscenes streaming process: PID={}", pid);
                                return true;
                            }
                        } else {
                            fclose(fp);
                            LOG_STREAM_WARN("Failed to read cmdline for PID={}", pid);
                        }
                    }
                } else {
                    LOG_STREAM_DEBUG("PID {} from {} not alive, will cleanup", pid, pidfile_nuscenes);
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
                                LOG_STREAM_DEBUG("Found running testpattern streaming process: PID={}", pid);
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

        // 使用 ps + grep 组合，排除 grep 和 pgrep 进程本身
        std::string check_cmd = "ps aux | grep -E 'ffmpeg.*" + target_stream + "' | grep -v grep | grep -v pgrep >/dev/null 2>&1";
        int ret = std::system(check_cmd.c_str());

        LOG_STREAM_TRACE("Streaming check result: {} (rtmp_target={})", (ret == 0 ? "running" : "not running"), target_stream);
        return (ret == 0);

    } catch (const std::exception& e) {
        LOG_STREAM_ERROR_WITH_CODE(SYS_UNEXPECTED_EXCEPTION, "Exception in is_streaming_running: {}", e.what());
        return false;
    } catch (...) {
        LOG_STREAM_ERROR("Unknown exception in is_streaming_running");
        return false;
    }
}

// 启动推流脚本（测试图案或 nuscenes -> ZLM），脚本路径由 VEHICLE_PUSH_SCRIPT 指定
// 检查现有进程，避免重复启动；容器重启后自动恢复推流
static void run_dataset_push_script()
{
    LOG_ENTRY();
    LOG_STREAM_INFO("Attempting to start streaming script...");

    try {
        // 先检查推流是否已在运行（精确定位：记录检查结果）
        bool already = is_streaming_running();
        LOG_STREAM_DEBUG("is_streaming_running() returned: {}", already);

        if (already) {
            LOG_STREAM_INFO("Streaming process already running, skipping start");
            LOG_STREAM_INFO("Hint: Delete PID files or stop existing process to restart");
            LOG_EXIT_WITH_VALUE("skipped: already_running");
            return;
        }

        // 获取环境变量
        const char* script = std::getenv("VEHICLE_PUSH_SCRIPT");
        if (!script || script[0] == '\0') {
            script = "scripts/push-nuscenes-cameras-to-zlm.sh";
            LOG_STREAM_WARN("VEHICLE_PUSH_SCRIPT not set, using default: {}", script);
        }

        const char* zlm_host = std::getenv("ZLM_HOST");
        const char* zlm_port = std::getenv("ZLM_RTMP_PORT");
        const char* app = std::getenv("ZLM_APP");

        LOG_STREAM_INFO("Streaming config: script={}, ZLM_HOST={}, ZLM_RTMP_PORT={}, ZLM_APP={}",
                       script,
                       zlm_host ? zlm_host : "(not set, using default 127.0.0.1)",
                       zlm_port ? zlm_port : "(not set, using default 1935)",
                       app ? app : "(not set, using default teleop)");

        // 使用 nohup 和重定向，确保进程在后台稳定运行
        // 导出 VEHICLE_VIN 供推流脚本用于 VIN 前缀流名（多车隔离）
        const char* vin_env2 = std::getenv("VEHICLE_VIN");
        if (!vin_env2) vin_env2 = std::getenv("VIN");
        std::string vin_export = (vin_env2 && vin_env2[0])
            ? (std::string("VEHICLE_VIN=") + vin_env2 + " VIN=" + vin_env2 + " ")
            : "";

        if (!vin_export.empty()) {
            LOG_STREAM_INFO("VIN prefix enabled: {}", vin_env2);
        } else {
            LOG_STREAM_WARN("VIN not set, multi-vehicle isolation disabled");
        }

        std::string cmd = "nohup env " + vin_export + "bash ";
        cmd += script;
        cmd += " >/tmp/push-stream.log 2>&1 &";

        LOG_STREAM_INFO("Executing command: {}", cmd);

        int r = std::system(cmd.c_str());
        LOG_STREAM_DEBUG("system() returned: {} (0=success fork)", r);

        // 等待一小段时间，检查进程是否成功启动
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        bool after500 = is_streaming_running();

        if (after500) {
            LOG_STREAM_INFO("Streaming script started successfully");
            LOG_STREAM_INFO("ZLM stream expected to be available in 5~15 seconds");
            LOG_STREAM_INFO("Log file: /tmp/push-stream.log");
        } else {
            LOG_STREAM_ERROR_WITH_CODE(STREAM_START_FAILED,
                "Streaming script started but process not detected after 500ms");
            LOG_STREAM_ERROR("Check /tmp/push-stream.log for errors");
            LOG_STREAM_ERROR("If using NuScenes dataset, verify dataset path is mounted correctly");
            LOG_STREAM_ERROR("Or use test pattern: VEHICLE_PUSH_SCRIPT=/app/scripts/push-testpattern-to-zlm.sh");
        }

        // 再等 2.5s 后打一条日志，便于分析 ZLM 流就绪时间
        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
        bool after2s = is_streaming_running();
        LOG_STREAM_DEBUG("2.5s after start, is_streaming_running()={}", after2s);

        LOG_EXIT_WITH_VALUE("r={}, running={}", r, after500);

    } catch (const std::exception& e) {
        LOG_STREAM_ERROR_WITH_CODE(STREAM_START_FAILED,
            "Exception in run_dataset_push_script: {}", e.what());
        LOG_EXIT_WITH_VALUE("exception");
    } catch (...) {
        LOG_STREAM_ERROR("Unknown exception in run_dataset_push_script");
        LOG_EXIT_WITH_VALUE("unknown_exception");
    }
}

// 停止推流进程（通过PID文件或进程名）
static void stop_streaming_processes()
{
    LOG_ENTRY();
    LOG_STREAM_INFO("Stopping streaming processes...");

    try {
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
                LOG_STREAM_INFO("Stopping nuscenes streaming script: PID={}", pid);

                // 发送 SIGTERM 信号，让脚本优雅退出
                std::string kill_cmd = "kill -TERM " + std::to_string(pid) + " 2>/dev/null";
                int ret = std::system(kill_cmd.c_str());

                if (ret == 0) {
                    LOG_STREAM_INFO("SIGTERM sent to nuscenes streaming script (PID={})", pid);
                } else {
                    LOG_STREAM_WARN("Process PID={} already dead or not owned by us", pid);
                }

                // 删除PID文件
                if (std::remove(pidfile_nuscenes.c_str()) == 0) {
                    LOG_STREAM_DEBUG("Removed PID file: {}", pidfile_nuscenes);
                }
            } else {
                fclose(fp);
            }
        }

        // 停止 testpattern 推流脚本
        fp = fopen(pidfile_testpattern.c_str(), "r");
        if (fp) {
            int pid = 0;
            if (fscanf(fp, "%d", &pid) == 1 && pid > 0) {
                fclose(fp);
                LOG_STREAM_INFO("Stopping testpattern streaming script: PID={}", pid);

                std::string kill_cmd = "kill -TERM " + std::to_string(pid) + " 2>/dev/null";
                int ret = std::system(kill_cmd.c_str());

                if (ret == 0) {
                    LOG_STREAM_INFO("SIGTERM sent to testpattern streaming script (PID={})", pid);
                }
                std::remove(pidfile_testpattern.c_str());
            } else {
                fclose(fp);
            }
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
        LOG_STREAM_DEBUG("Attempted to stop ffmpeg processes matching: {}/*/cam_front", rtmp_base);

        // 等待一小段时间，确认进程已停止
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        bool still_running = is_streaming_running();

        if (!still_running) {
            LOG_STREAM_INFO("All streaming processes stopped successfully");
        } else {
            LOG_STREAM_WARN("Some streaming processes may still be running");
            LOG_STREAM_WARN("Manual intervention may be required");
        }

        LOG_EXIT_WITH_VALUE("still_running={}", still_running);

    } catch (const std::exception& e) {
        LOG_STREAM_ERROR_WITH_CODE(SYS_UNEXPECTED_EXCEPTION,
            "Exception in stop_streaming_processes: {}", e.what());
        LOG_EXIT();
    } catch (...) {
        LOG_STREAM_ERROR("Unknown exception in stop_streaming_processes");
        LOG_EXIT();
    }
}

// 已将防重放逻辑移至 VehicleController::processCommand (带 seq/timestamp 参数版本)

// 推流状态跟踪：记录是否应该推流（收到 start_stream 后为 true）
static bool g_streaming_should_be_running = false;
static auto g_last_streaming_check_time = std::chrono::steady_clock::now();

// JSON解析异常类
class JsonParseException : public std::runtime_error {
public:
    vehicle::error::Code error_code_;
    JsonParseException(const std::string& msg, vehicle::error::Code code)
        : std::runtime_error(msg), error_code_(code) {}
};

bool handle_control_json(VehicleController* controller,
                         const std::string& jsonPayload)
{
    LOG_ENTRY();
    LOG_STREAM_TRACE("handle_control_json called, payload length: {} bytes", jsonPayload.length());

    if (!controller) {
        LOG_CTRL_ERROR_WITH_CODE(SYS_NULL_POINTER, "Controller is null, ignoring message");
        LOG_EXIT_WITH_VALUE("false: null_controller");
        return false;
    }

    // 检查输入参数
    if (jsonPayload.empty()) {
        LOG_CTRL_ERROR_WITH_CODE(SYS_INVALID_PARAM, "Empty JSON payload received");
        LOG_EXIT_WITH_VALUE("false: empty_payload");
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
        std::string nonce;  // Plan §9.2: Extract nonce for security/audit
        std::string signature;  // Plan §9.2: Extract signature for integrity check
        std::string typeStr;

        // 提取数字与控制字段
        std::regex type_regex("\"type\"\\s*:\\s*\"([^\"]+)\"");
        std::regex num_regex("\"(schemaVersion|seq|timestampMs|steering|throttle|brake|gear)\"\\s*:\\s*([+-]?\\d+\\.?\\d*)");
        std::regex vin_regex("\"vin\"\\s*:\\s*\"([^\"]+)\"");
        std::regex session_regex("\"sessionId\"\\s*:\\s*\"([^\"]+)\"");
        std::regex nonce_regex("\"nonce\"\\s*:\\s*\"([^\"]+)\"");  // Plan §9.2
        std::regex sig_regex("\"signature\"\\s*:\\s*\"([^\"]+)\"");  // Plan §9.2

        std::smatch match;

        // 提取消息类型
        if (std::regex_search(jsonPayload, match, type_regex)) {
            typeStr = match[1].str();
            LOG_CTRL_DEBUG("Message type extracted: {}", typeStr);
        } else {
            LOG_CTRL_WARN("No 'type' field found in JSON payload");
        }

        // 提取消息中的 VIN（用于 start_stream/stop_stream 的 VIN 过滤）
        std::string msg_vin;
        if (std::regex_search(jsonPayload, match, vin_regex)) {
            msg_vin = match[1].str();
            LOG_STREAM_TRACE("VIN extracted from message: {}", msg_vin);
        }

        const char* cfg_vin_env = std::getenv("VEHICLE_VIN");
        std::string cfg_vin = (cfg_vin_env && cfg_vin_env[0] != '\0') ? cfg_vin_env : "";
        LOG_STREAM_TRACE("VIN config: msg_vin={}, cfg_vin={}", msg_vin, cfg_vin);

        // ==================== 处理不同类型的消息 ====================

        if (typeStr == "start_stream") {
            // 仅当未配置 VEHICLE_VIN，或消息无 VIN，或消息 VIN 与配置一致时才响应
            if (!cfg_vin.empty() && !msg_vin.empty() && msg_vin != cfg_vin) {
                LOG_SEC_WARN_WITH_CODE(SEC_VIN_MISMATCH,
                    "Ignoring start_stream: VIN mismatch (msg_vin={}, cfg_vin={})", msg_vin, cfg_vin);
                LOG_EXIT_WITH_VALUE("true: vin_mismatch_ignored");
                return true;  // 返回true表示已处理，只是忽略
            }

            LOG_STREAM_INFO("Received start_stream command");
            LOG_STREAM_INFO("VIN check passed: msg_vin={}, cfg_vin={}", msg_vin, cfg_vin);

            // 标记推流应该运行，然后启动推流
            g_streaming_should_be_running = true;
            g_last_streaming_check_time = std::chrono::steady_clock::now();

            // 如果推流未运行，启动推流；如果已在运行，保持运行状态
            run_dataset_push_script();

            LOG_EXIT_WITH_VALUE("true: start_stream_handled");
            return true;
        }

        if (typeStr == "stop_stream") {
            if (!cfg_vin.empty() && !msg_vin.empty() && msg_vin != cfg_vin) {
                LOG_SEC_WARN_WITH_CODE(SEC_VIN_MISMATCH,
                    "Ignoring stop_stream: VIN mismatch (msg_vin={}, cfg_vin={})", msg_vin, cfg_vin);
                LOG_EXIT_WITH_VALUE("true: vin_mismatch_ignored");
                return true;
            }

            LOG_STREAM_INFO("Received stop_stream command");

            // 标记推流应该停止
            g_streaming_should_be_running = false;

            // 停止推流进程
            stop_streaming_processes();

            LOG_EXIT_WITH_VALUE("true: stop_stream_handled");
            return true;
        }

        if (typeStr == "remote_control") {
            LOG_CTRL_INFO("Processing remote_control command");

            // 提取 enable 字段
            std::regex enable_regex("\"enable\"\\s*:\\s*(true|false)");
            std::smatch enable_match;
            bool enable = false;
            if (std::regex_search(jsonPayload, enable_match, enable_regex)) {
                enable = (enable_match[1].str() == "true");
            }
            LOG_CTRL_INFO("Remote control enable value: {}", enable);

            // 设置远驾接管状态变量（供车辆控制逻辑使用）
            controller->setRemoteControlEnabled(enable);

            // 根据远驾接管状态设置驾驶模式
            if (enable) {
                controller->setDrivingMode(VehicleController::DrivingMode::REMOTE_DRIVING);
                LOG_CTRL_INFO("Remote control ENABLED, driving mode set to REMOTE_DRIVING");
            } else {
                // 禁用远驾接管时，恢复为自驾模式
                controller->setDrivingMode(VehicleController::DrivingMode::AUTONOMOUS);
                LOG_CTRL_INFO("Remote control DISABLED, driving mode set to AUTONOMOUS");
            }

            LOG_EXIT_WITH_VALUE("true: remote_control_handled");
            return true;
        }

        if (typeStr == "gear") {
            LOG_CTRL_INFO("Processing gear command");

            // 提取 value 字段（档位命令使用 value 字段）
            std::regex value_regex("\"value\"\\s*:\\s*([+-]?\\d+)");
            std::smatch value_match;
            int gearValue = 0;

            if (std::regex_search(jsonPayload, value_match, value_regex)) {
                try {
                    gearValue = std::stoi(value_match[1].str());
                    LOG_CTRL_DEBUG("Extracted gear value: {} ({})", gearValue,
                                 (gearValue == -1 ? "R" : (gearValue == 0 ? "N" : (gearValue == 1 ? "D" : (gearValue == 2 ? "P" : "Unknown")))));
                } catch (const std::exception& e) {
                    LOG_CTRL_ERROR_WITH_CODE(CTRL_INVALID_GEAR,
                        "Failed to parse gear value: {}", e.what());
                    gearValue = 0;
                }
            } else {
                LOG_CTRL_WARN("No 'value' field found in gear command, defaulting to N(0)");
            }

            // 检查远驾接管状态
            bool remoteControlEnabled = controller->isRemoteControlEnabled();
            if (!remoteControlEnabled) {
                LOG_CTRL_WARN("Remote control not enabled, ignoring gear command");
                LOG_EXIT_WITH_VALUE("false: remote_control_disabled");
                return false;
            }

            // 应用档位命令
            VehicleController::ControlCommand currentCmd = controller->getCurrentCommand();
            LOG_CTRL_DEBUG("Current command before gear change: steer={:.2f}, throttle={:.2f}, brake={:.2f}, gear={}",
                          currentCmd.steering, currentCmd.throttle, currentCmd.brake, currentCmd.gear);

            // 只更新档位，保持其他控制参数不变
            controller->processCommand(currentCmd.steering, currentCmd.throttle, currentCmd.brake, gearValue);

            LOG_CTRL_INFO("Gear command applied: {} -> {} ({})",
                         currentCmd.gear, gearValue,
                         (gearValue == -1 ? "R" : (gearValue == 0 ? "N" : (gearValue == 1 ? "D" : (gearValue == 2 ? "P" : "Unknown")))));

            LOG_EXIT_WITH_VALUE("true: gear_handled");
            return true;
        }

        if (typeStr == "sweep") {
            LOG_CTRL_INFO("Processing sweep command");

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

            LOG_CTRL_DEBUG("Sweep command: type={}, active={}", sweepTypeStr, sweepActive);

            // 检查远驾接管状态
            bool remoteControlEnabled = controller->isRemoteControlEnabled();
            if (!remoteControlEnabled) {
                LOG_CTRL_WARN("Remote control not enabled, ignoring sweep command");
                LOG_EXIT_WITH_VALUE("false: remote_control_disabled");
                return false;
            }

            // 应用清扫命令
            if (sweepTypeStr == "sweep") {
                controller->setSweepActive(sweepActive);
                LOG_CTRL_INFO("Sweep command applied: type={}, active={}", sweepTypeStr, sweepActive);
            } else {
                LOG_CTRL_WARN("Unsupported sweep type: {}", sweepTypeStr);
            }

            LOG_EXIT_WITH_VALUE("true: sweep_handled");
            return true;
        }

        if (typeStr == "mode") {
            LOG_CTRL_INFO("Processing mode command");

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
                std::regex light_active_regex("\"active\"\\s*:\\s*(true|false)");
                std::smatch lightType_match, lightActive_match;
                std::string lightTypeStr;
                bool lightActive = false;

                if (std::regex_search(jsonPayload, lightType_match, lightType_regex)) {
                    lightTypeStr = lightType_match[1].str();
                }
                if (std::regex_search(jsonPayload, lightActive_match, light_active_regex)) {
                    lightActive = (lightActive_match[1].str() == "true");
                }

                LOG_CTRL_DEBUG("Light command: type={}, active={}", lightTypeStr, lightActive);

                // 检查远驾接管状态
                bool remoteControlEnabled = controller->isRemoteControlEnabled();
                if (!remoteControlEnabled) {
                    LOG_CTRL_WARN("Remote control not enabled, ignoring light command");
                    LOG_EXIT_WITH_VALUE("false: remote_control_disabled");
                    return false;
                }

                // TODO: 实际应用灯光控制到车辆硬件接口
                LOG_CTRL_INFO("Light command received (hardware implementation pending): type={}, active={}", lightTypeStr, lightActive);
                LOG_EXIT_WITH_VALUE("true: light_pending_implementation");
                return true;

            } else {
                LOG_CTRL_WARN("Unknown mode subType: {}", subTypeStr);
                LOG_EXIT_WITH_VALUE("false: unknown_subtype");
                return false;
            }
        }

        if (typeStr == "target_speed") {
            LOG_CTRL_INFO("Processing target_speed command");

            // 提取 value 字段（目标速度命令使用 value 字段，单位：km/h）
            std::regex value_regex("\"value\"\\s*:\\s*([+-]?\\d+\\.?\\d*)");
            std::smatch value_match;
            double targetSpeedValue = 0.0;

            if (std::regex_search(jsonPayload, value_match, value_regex)) {
                try {
                    targetSpeedValue = std::stod(value_match[1].str());
                    targetSpeedValue = std::max(0.0, std::min(100.0, targetSpeedValue));  // 限制在0-100 km/h
                    LOG_CTRL_DEBUG("Target speed extracted: {} km/h", targetSpeedValue);
                } catch (const std::exception& e) {
                    LOG_CTRL_ERROR_WITH_CODE(CTRL_INVALID_PARAM,
                        "Failed to parse target speed: {}", e.what());
                    targetSpeedValue = 0.0;
                }
            } else {
                LOG_CTRL_WARN("No 'value' field found in target_speed command");
            }

            // 检查远驾接管状态
            bool remoteControlEnabled = controller->isRemoteControlEnabled();
            if (!remoteControlEnabled) {
                LOG_CTRL_WARN("Remote control not enabled, ignoring target_speed command");
                LOG_EXIT_WITH_VALUE("false: remote_control_disabled");
                return false;
            }

            // TODO: 实际应用目标速度到车辆控制
            LOG_CTRL_INFO("Target speed command received: {} km/h (hardware implementation pending)", targetSpeedValue);
            LOG_EXIT_WITH_VALUE("true: target_speed_pending_implementation");
            return true;
        }

        if (typeStr == "brake") {
            LOG_CTRL_INFO("Processing brake command");

            // 提取 value 字段（刹车命令使用 value 字段，范围：0.0-1.0）
            std::regex value_regex("\"value\"\\s*:\\s*([+-]?\\d+\\.?\\d*)");
            std::smatch value_match;
            double brakeValue = 0.0;

            if (std::regex_search(jsonPayload, value_match, value_regex)) {
                try {
                    brakeValue = std::stod(value_match[1].str());
                    brakeValue = std::max(0.0, std::min(1.0, brakeValue));  // 限制在0.0-1.0
                    LOG_CTRL_DEBUG("Brake value extracted: {} (range: 0.0-1.0)", brakeValue);
                } catch (const std::exception& e) {
                    LOG_CTRL_ERROR_WITH_CODE(CTRL_INVALID_BRAKE,
                        "Failed to parse brake value: {}", e.what());
                    brakeValue = 0.0;
                }
            } else {
                LOG_CTRL_WARN("No 'value' field found in brake command");
            }

            // 检查远驾接管状态
            bool remoteControlEnabled = controller->isRemoteControlEnabled();
            if (!remoteControlEnabled) {
                LOG_CTRL_WARN("Remote control not enabled, ignoring brake command");
                LOG_EXIT_WITH_VALUE("false: remote_control_disabled");
                return false;
            }

            // 应用刹车命令
            VehicleController::ControlCommand currentCmd = controller->getCurrentCommand();
            LOG_CTRL_DEBUG("Current command before brake change: steer={:.2f}, throttle={:.2f}, brake={:.2f}, gear={}",
                          currentCmd.steering, currentCmd.throttle, currentCmd.brake, currentCmd.gear);

            // 只更新刹车，保持其他控制参数不变
            controller->processCommand(currentCmd.steering, currentCmd.throttle, brakeValue, currentCmd.gear);

            LOG_CTRL_INFO("Brake command applied: {:.2f} (range: 0.0-1.0)", brakeValue);
            LOG_EXIT_WITH_VALUE("true: brake_handled");
            return true;
        }

        if (typeStr == "emergency_stop") {
            LOG_SAFE_CRITICAL("Processing emergency_stop command");

            // 提取 enable 字段（true: 执行急停, false: 解除急停）
            std::regex enable_regex("\"enable\"\\s*:\\s*(true|false)");
            std::smatch enable_match;
            bool enableEmergencyStop = false;

            if (std::regex_search(jsonPayload, enable_match, enable_regex)) {
                enableEmergencyStop = (enable_match[1].str() == "true");
                LOG_SAFE_INFO("Emergency stop value extracted: {}", enableEmergencyStop ? "EXECUTE" : "RELEASE");
            } else {
                LOG_SAFE_WARN("No 'enable' field found in emergency_stop command, defaulting to RELEASE");
            }

            // 检查远驾接管状态
            bool remoteControlEnabled = controller->isRemoteControlEnabled();
            if (!remoteControlEnabled) {
                LOG_SAFE_WARN("Remote control not enabled, ignoring emergency_stop command");
                LOG_EXIT_WITH_VALUE("false: remote_control_disabled");
                return false;
            }

            // 应用急停命令
            VehicleController::ControlCommand currentCmd = controller->getCurrentCommand();

            if (enableEmergencyStop) {
                // 执行急停：设置刹车为1.0
                controller->processCommand(currentCmd.steering, currentCmd.throttle, 1.0, currentCmd.gear);
                LOG_SAFE_CRITICAL("EMERGENCY STOP EXECUTED: brake=1.0");
            } else {
                // 解除急停：设置刹车为0.0
                controller->processCommand(currentCmd.steering, currentCmd.throttle, 0.0, currentCmd.gear);
                LOG_SAFE_INFO("EMERGENCY STOP RELEASED: brake=0.0");
            }

            LOG_EXIT_WITH_VALUE("true: emergency_stop_handled");
            return true;
        }

        // 检查远驾接管状态：如果未启用，则忽略控制指令（但允许 start_stream/stop_stream/remote_control/gear/sweep/target_speed/brake/emergency_stop）
        if (!typeStr.empty() && typeStr != "start_stream" && typeStr != "stop_stream" && typeStr != "remote_control") {
            bool remoteControlEnabled = controller->isRemoteControlEnabled();
            if (!remoteControlEnabled) {
                LOG_CTRL_WARN("Remote control not enabled, ignoring control command type={}", typeStr);
                LOG_EXIT_WITH_VALUE("false: remote_control_disabled");
                return false;
            }
        }

        // ==================== 解析通用控制字段 ====================
        auto it = jsonPayload.cbegin();
        while (std::regex_search(it, jsonPayload.cend(), match, num_regex)) {
            const std::string key = match[1].str();
            const std::string val = match[2].str();

            try {
                if (key == "schemaVersion") {
                    schemaVersion = std::stoi(val);
                    LOG_CTRL_TRACE("schemaVersion: {}", schemaVersion);
                } else if (key == "seq") {
                    seq = static_cast<long long>(std::stoll(val));
                    LOG_CTRL_TRACE("seq: {}", seq);
                } else if (key == "timestampMs") {
                    timestampMs = static_cast<long long>(std::stoll(val));
                    LOG_CTRL_TRACE("timestampMs: {}", timestampMs);
                } else if (key == "steering") {
                    steering = std::clamp(std::stod(val), -1.0, 1.0);
                    LOG_CTRL_TRACE("steering: {}", steering);
                } else if (key == "throttle") {
                    throttle = std::clamp(std::stod(val), 0.0, 1.0);
                    LOG_CTRL_TRACE("throttle: {}", throttle);
                } else if (key == "brake") {
                    brake = std::clamp(std::stod(val), 0.0, 1.0);
                    LOG_CTRL_TRACE("brake: {}", brake);
                } else if (key == "gear") {
                    gear = std::stoi(val);
                    LOG_CTRL_TRACE("gear: {} ({})", gear,
                                 (gear == -1 ? "R" : (gear == 0 ? "N" : (gear == 1 ? "D" : "P"))));
                }
            } catch (const std::exception& e) {
                LOG_CTRL_ERROR_WITH_CODE(CTRL_INVALID_PARAM,
                    "Failed to parse '{}': {}", key, e.what());
                // Apply safe defaults for critical values
                if (key == "steering") steering = 0.0;
                if (key == "throttle") throttle = 0.0;
                if (key == "brake") brake = 0.0;
                if (key == "gear") gear = 1;
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
        LOG_CTRL_INFO("Control cmd parsed: schema={}, seq={}, ts={}, vin={}, sid={}, nonce={}, sig={}",
                     schemaVersion, seq, timestampMs, vin, sessionId,
                     (nonce.empty() ? "null" : nonce.substr(0, 8) + "..."),
                     (signature.empty() ? "null" : signature.substr(0, 8) + "..."));

        // 档位值：-1=R, 0=N, 1=D, 2=P
        int validGear = (gear == -1 || gear == 0 || gear == 1 || gear == 2) ? gear : 1;

        std::string gearStr = (validGear == -1 ? "R" : (validGear == 0 ? "N" : (validGear == 1 ? "D" : (validGear == 2 ? "P" : "Unknown"))));
        LOG_CTRL_INFO("Applying control command: steer={:.3f}, throttle={:.3f}, brake={:.3f}, gear={} ({})",
                     steering, throttle, brake, validGear, gearStr);

        // 调用带安全参数的 processCommand
        controller->processCommand(steering, throttle, brake, validGear, static_cast<uint32_t>(seq), timestampMs);

        LOG_CTRL_INFO("Control command applied successfully");
        LOG_EXIT_WITH_VALUE("true: control_command_applied");

        return true;

    } catch (const JsonParseException& e) {
        LOG_CTRL_ERROR_WITH_CODE(static_cast<vehicle::error::Code>(e.error_code_),
            "JSON parse error: {}", e.what());
        LOG_EXIT_WITH_VALUE("false: json_parse_error");
        return false;
    } catch (const std::exception &e) {
        LOG_CTRL_ERROR_WITH_CODE(SYS_UNEXPECTED_EXCEPTION,
            "Unexpected error in handle_control_json: {}", e.what());
        LOG_EXIT_WITH_VALUE("false: unexpected_error");
        return false;
    }
}

// 推流健康检查：检查推流进程是否在运行，如果未运行且应该运行，则自动重启
bool check_and_restore_streaming()
{
    LOG_ENTRY();
    LOG_STREAM_TRACE("check_and_restore_streaming called, streaming_should_be_running={}", g_streaming_should_be_running);

    // 如果推流不应该运行，跳过检查
    if (!g_streaming_should_be_running) {
        LOG_STREAM_TRACE("Streaming should not be running, skipping check");
        LOG_EXIT_WITH_VALUE("false: not_required");
        return false;
    }

    // 检查推流是否在运行
    bool is_running = is_streaming_running();

    if (!is_running) {
        // 推流应该运行但未运行，尝试重启
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            now - g_last_streaming_check_time).count();

        LOG_STREAM_WARN("Streaming process not running but should be! Last check={}s ago", elapsed);

        // 避免频繁重启（至少间隔 5 秒）
        if (elapsed >= 5) {
            LOG_STREAM_INFO("Attempting to auto-restore streaming process...");

            run_dataset_push_script();

            g_last_streaming_check_time = now;

            // 等待一小段时间后再次检查
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            bool restored = is_streaming_running();

            if (restored) {
                LOG_STREAM_INFO("Streaming process auto-restored successfully");
                LOG_EXIT_WITH_VALUE("true: restored");
                return true;
            } else {
                LOG_STREAM_ERROR_WITH_CODE(STREAM_RESTORE_FAILED,
                    "Failed to auto-restore streaming process");
                LOG_EXIT_WITH_VALUE("false: restore_failed");
                return false;
            }
        } else {
            LOG_STREAM_TRACE("Too soon since last check ({}/5s), skipping", elapsed);
            LOG_EXIT_WITH_VALUE("false: too_soon");
            return false;
        }
    } else {
        // 推流正常运行，更新检查时间
        g_last_streaming_check_time = std::chrono::steady_clock::now();
        LOG_STREAM_TRACE("Streaming process is healthy");
        LOG_EXIT_WITH_VALUE("true: healthy");
        return true;
    }
}


