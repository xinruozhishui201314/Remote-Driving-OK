#pragma once
#ifndef VEHICLE_LOG_MACROS_H
#define VEHICLE_LOG_MACROS_H

#include <string>
#include <sstream>
#include <memory>
#include <chrono>
#include <iomanip>
#include <thread>
#include <mutex>
#include "logger.h"
#include "error_code.h"
#include <spdlog/fmt/fmt.h> // 使用 spdlog 自带的 fmt，避免版本冲突

/**
 * @brief 增强的日志宏系统
 *
 * 功能：
 * 1. 统一日志格式：[级别][模块][错误码][时间戳][线程ID][TraceID] 消息
 * 2. 调用链追踪：自动记录函数入口/出口
 * 3. 错误码记录：集成 error_code.h
 * 4. 条件日志：支持按模块/级别过滤
 */
namespace vehicle::logging {

/**
 * @brief 获取当前时间戳字符串
 */
inline std::string getTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    std::stringstream ss;
    ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
    ss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}

/**
 * @brief 获取线程ID字符串
 */
inline std::string getThreadId() {
    std::stringstream ss;
    ss << std::this_thread::get_id();
    return ss.str();
}

/**
 * @brief 日志上下文信息
 */
struct LogContext {
    std::string module;      // 模块名
    std::string function;   // 函数名
    std::string trace_id;    // 调用链追踪ID
    int line;                // 行号
    std::string file;        // 文件名

    LogContext(const char* mod, const char* func, int l, const char* f)
        : module(mod), function(func), line(l), file(f) {}

    std::string toString() const {
        std::stringstream ss;
        ss << "[" << module << "][" << function << ":" << line << "]";
        return ss.str();
    }
};

// 线程本地 TraceID
inline thread_local std::string s_trace_id;

/**
 * @brief 设置当前线程的TraceID
 */
inline void setTraceId(const std::string& trace_id) {
    s_trace_id = trace_id;
}

/**
 * @brief 生成新的TraceID
 */
inline std::string generateTraceId() {
    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    std::stringstream ss;
    ss << std::this_thread::get_id() << "-" << now;
    return ss.str();
}

/**
 * @brief 获取当前TraceID
 */
inline const std::string& getTraceId() {
    return s_trace_id;
}

/**
 * @brief 获取带TraceID前缀的日志前缀
 */
inline std::string getLogPrefix(const std::string& module, const std::string& func, int line) {
    std::stringstream ss;
    ss << "[" << getTimestamp() << "]"
       << "[T:" << getThreadId() << "]";
    if (!s_trace_id.empty()) {
        ss << "[Trace:" << s_trace_id << "]";
    }
    ss << "[" << module << "][" << func << ":" << line << "] ";
    return ss.str();
}

// 日志级别枚举
enum class Level {
    TRACE = 0,
    DEBUG = 1,
    INFO = 2,
    WARN = 3,
    ERROR = 4,
    CRITICAL = 5
};

/**
 * @brief 格式化错误码信息
 */
inline std::string formatErrorCode(uint32_t code, const std::string& msg = "") {
    std::stringstream ss;
    ss << "E" << std::setfill('0') << std::setw(4) << code;
    if (!msg.empty()) {
        ss << " (" << msg << ")";
    }
    return ss.str();
}

}  // namespace vehicle::logging

// ============== 模块化日志宏 ==============

// 模块定义
#define LOG_MODULE_SYSTEM    "SYS"
#define LOG_MODULE_MQTT     "MQTT"
#define LOG_MODULE_CTRL     "CTRL"
#define LOG_MODULE_SAFETY   "SAFE"
#define LOG_MODULE_NETWORK  "NET"
#define LOG_MODULE_CARLA    "CARLA"
#define LOG_MODULE_STREAM   "STREAM"
#define LOG_MODULE_CONFIG   "CFG"
#define LOG_MODULE_SECURITY "SEC"

// ============== 基础日志宏 ==============

/**
 * @brief 通用日志宏 - 自动添加上下文信息
 */
#define LOG_BASE(module, level, ...) \
    do { \
        auto prefix = vehicle::logging::getLogPrefix(module, __FUNCTION__, __LINE__); \
        LOG_##level(prefix, __VA_ARGS__); \
    } while(0)

/**
 * @brief 带错误码的日志宏
 */
#define LOG_ERROR_WITH_CODE(module, code, ...) \
    do { \
        auto prefix = vehicle::logging::getLogPrefix(module, __FUNCTION__, __LINE__); \
        auto err_str = vehicle::logging::formatErrorCode(static_cast<uint32_t>(code)); \
        std::string full_msg = prefix + "[" + err_str + "] " + ::fmt::format(__VA_ARGS__); \
        LOG_ERROR("{}", full_msg); \
    } while(0)

#define LOG_WARN_WITH_CODE(module, code, ...) \
    do { \
        auto prefix = vehicle::logging::getLogPrefix(module, __FUNCTION__, __LINE__); \
        auto err_str = vehicle::logging::formatErrorCode(static_cast<uint32_t>(code)); \
        std::string full_msg = prefix + "[" + err_str + "] " + ::fmt::format(__VA_ARGS__); \
        LOG_WARN("{}", full_msg); \
    } while(0)

// ============== 模块化日志宏 ==============

// 系统模块日志
#define LOG_SYS_TRACE(...)  LOG_BASE(LOG_MODULE_SYSTEM, TRACE, __VA_ARGS__)
#define LOG_SYS_DEBUG(...)  LOG_BASE(LOG_MODULE_SYSTEM, DEBUG, __VA_ARGS__)
#define LOG_SYS_INFO(...)   LOG_BASE(LOG_MODULE_SYSTEM, INFO, __VA_ARGS__)
#define LOG_SYS_WARN(...)   LOG_BASE(LOG_MODULE_SYSTEM, WARN, __VA_ARGS__)
#define LOG_SYS_ERROR(...)  LOG_BASE(LOG_MODULE_SYSTEM, ERROR, __VA_ARGS__)
#define LOG_SYS_CRITICAL(...) LOG_BASE(LOG_MODULE_SYSTEM, CRITICAL, __VA_ARGS__)
#define LOG_SYS_ERROR_WITH_CODE(code, ...) LOG_ERROR_WITH_CODE(LOG_MODULE_SYSTEM, code, __VA_ARGS__)
#define LOG_SYS_WARN_WITH_CODE(code, ...)  LOG_WARN_WITH_CODE(LOG_MODULE_SYSTEM, code, __VA_ARGS__)

// MQTT模块日志
#define LOG_MQTT_TRACE(...)  LOG_BASE(LOG_MODULE_MQTT, TRACE, __VA_ARGS__)
#define LOG_MQTT_DEBUG(...)  LOG_BASE(LOG_MODULE_MQTT, DEBUG, __VA_ARGS__)
#define LOG_MQTT_INFO(...)   LOG_BASE(LOG_MODULE_MQTT, INFO, __VA_ARGS__)
#define LOG_MQTT_WARN(...)   LOG_BASE(LOG_MODULE_MQTT, WARN, __VA_ARGS__)
#define LOG_MQTT_ERROR(...)  LOG_BASE(LOG_MODULE_MQTT, ERROR, __VA_ARGS__)
#define LOG_MQTT_CRITICAL(...) LOG_BASE(LOG_MODULE_MQTT, CRITICAL, __VA_ARGS__)
#define LOG_MQTT_ERROR_WITH_CODE(code, ...) LOG_ERROR_WITH_CODE(LOG_MODULE_MQTT, code, __VA_ARGS__)
#define LOG_MQTT_WARN_WITH_CODE(code, ...)  LOG_WARN_WITH_CODE(LOG_MODULE_MQTT, code, __VA_ARGS__)

// 控制器模块日志
#define LOG_CTRL_TRACE(...)  LOG_BASE(LOG_MODULE_CTRL, TRACE, __VA_ARGS__)
#define LOG_CTRL_DEBUG(...)  LOG_BASE(LOG_MODULE_CTRL, DEBUG, __VA_ARGS__)
#define LOG_CTRL_INFO(...)   LOG_BASE(LOG_MODULE_CTRL, INFO, __VA_ARGS__)
#define LOG_CTRL_WARN(...)   LOG_BASE(LOG_MODULE_CTRL, WARN, __VA_ARGS__)
#define LOG_CTRL_ERROR(...)  LOG_BASE(LOG_MODULE_CTRL, ERROR, __VA_ARGS__)
#define LOG_CTRL_CRITICAL(...) LOG_BASE(LOG_MODULE_CTRL, CRITICAL, __VA_ARGS__)
#define LOG_CTRL_ERROR_WITH_CODE(code, ...) LOG_ERROR_WITH_CODE(LOG_MODULE_CTRL, code, __VA_ARGS__)
#define LOG_CTRL_WARN_WITH_CODE(code, ...)  LOG_WARN_WITH_CODE(LOG_MODULE_CTRL, code, __VA_ARGS__)

// 安全模块日志
#define LOG_SAFE_TRACE(...)  LOG_BASE(LOG_MODULE_SAFETY, TRACE, __VA_ARGS__)
#define LOG_SAFE_DEBUG(...)  LOG_BASE(LOG_MODULE_SAFETY, DEBUG, __VA_ARGS__)
#define LOG_SAFE_INFO(...)   LOG_BASE(LOG_MODULE_SAFETY, INFO, __VA_ARGS__)
#define LOG_SAFE_WARN(...)   LOG_BASE(LOG_MODULE_SAFETY, WARN, __VA_ARGS__)
#define LOG_SAFE_ERROR(...)  LOG_BASE(LOG_MODULE_SAFETY, ERROR, __VA_ARGS__)
#define LOG_SAFE_CRITICAL(...) LOG_BASE(LOG_MODULE_SAFETY, CRITICAL, __VA_ARGS__)
#define LOG_SAFE_ERROR_WITH_CODE(code, ...) LOG_ERROR_WITH_CODE(LOG_MODULE_SAFETY, code, __VA_ARGS__)
#define LOG_SAFE_WARN_WITH_CODE(code, ...)  LOG_WARN_WITH_CODE(LOG_MODULE_SAFETY, code, __VA_ARGS__)

// 网络模块日志
#define LOG_NET_TRACE(...)  LOG_BASE(LOG_MODULE_NETWORK, TRACE, __VA_ARGS__)
#define LOG_NET_DEBUG(...)  LOG_BASE(LOG_MODULE_NETWORK, DEBUG, __VA_ARGS__)
#define LOG_NET_INFO(...)   LOG_BASE(LOG_MODULE_NETWORK, INFO, __VA_ARGS__)
#define LOG_NET_WARN(...)   LOG_BASE(LOG_MODULE_NETWORK, WARN, __VA_ARGS__)
#define LOG_NET_ERROR(...)  LOG_BASE(LOG_MODULE_NETWORK, ERROR, __VA_ARGS__)
#define LOG_NET_CRITICAL(...) LOG_BASE(LOG_MODULE_NETWORK, CRITICAL, __VA_ARGS__)
#define LOG_NET_ERROR_WITH_CODE(code, ...) LOG_ERROR_WITH_CODE(LOG_MODULE_NETWORK, code, __VA_ARGS__)
#define LOG_NET_WARN_WITH_CODE(code, ...)  LOG_WARN_WITH_CODE(LOG_MODULE_NETWORK, code, __VA_ARGS__)

// CARLA模块日志
#define LOG_CARLA_TRACE(...)  LOG_BASE(LOG_MODULE_CARLA, TRACE, __VA_ARGS__)
#define LOG_CARLA_DEBUG(...)  LOG_BASE(LOG_MODULE_CARLA, DEBUG, __VA_ARGS__)
#define LOG_CARLA_INFO(...)   LOG_BASE(LOG_MODULE_CARLA, INFO, __VA_ARGS__)
#define LOG_CARLA_WARN(...)   LOG_BASE(LOG_MODULE_CARLA, WARN, __VA_ARGS__)
#define LOG_CARLA_ERROR(...)  LOG_BASE(LOG_MODULE_CARLA, ERROR, __VA_ARGS__)
#define LOG_CARLA_CRITICAL(...) LOG_BASE(LOG_MODULE_CARLA, CRITICAL, __VA_ARGS__)
#define LOG_CARLA_ERROR_WITH_CODE(code, ...) LOG_ERROR_WITH_CODE(LOG_MODULE_CARLA, code, __VA_ARGS__)
#define LOG_CARLA_WARN_WITH_CODE(code, ...)  LOG_WARN_WITH_CODE(LOG_MODULE_CARLA, code, __VA_ARGS__)

// 推流模块日志
#define LOG_STREAM_TRACE(...)  LOG_BASE(LOG_MODULE_STREAM, TRACE, __VA_ARGS__)
#define LOG_STREAM_DEBUG(...)  LOG_BASE(LOG_MODULE_STREAM, DEBUG, __VA_ARGS__)
#define LOG_STREAM_INFO(...)   LOG_BASE(LOG_MODULE_STREAM, INFO, __VA_ARGS__)
#define LOG_STREAM_WARN(...)   LOG_BASE(LOG_MODULE_STREAM, WARN, __VA_ARGS__)
#define LOG_STREAM_ERROR(...)  LOG_BASE(LOG_MODULE_STREAM, ERROR, __VA_ARGS__)
#define LOG_STREAM_CRITICAL(...) LOG_BASE(LOG_MODULE_STREAM, CRITICAL, __VA_ARGS__)
#define LOG_STREAM_ERROR_WITH_CODE(code, ...) LOG_ERROR_WITH_CODE(LOG_MODULE_STREAM, code, __VA_ARGS__)
#define LOG_STREAM_WARN_WITH_CODE(code, ...)  LOG_WARN_WITH_CODE(LOG_MODULE_STREAM, code, __VA_ARGS__)

// 配置模块日志
#define LOG_CFG_TRACE(...)  LOG_BASE(LOG_MODULE_CONFIG, TRACE, __VA_ARGS__)
#define LOG_CFG_DEBUG(...)  LOG_BASE(LOG_MODULE_CONFIG, DEBUG, __VA_ARGS__)
#define LOG_CFG_INFO(...)   LOG_BASE(LOG_MODULE_CONFIG, INFO, __VA_ARGS__)
#define LOG_CFG_WARN(...)   LOG_BASE(LOG_MODULE_CONFIG, WARN, __VA_ARGS__)
#define LOG_CFG_ERROR(...)  LOG_BASE(LOG_MODULE_CONFIG, ERROR, __VA_ARGS__)
#define LOG_CFG_CRITICAL(...) LOG_BASE(LOG_MODULE_CONFIG, CRITICAL, __VA_ARGS__)
#define LOG_CFG_ERROR_WITH_CODE(code, ...) LOG_ERROR_WITH_CODE(LOG_MODULE_CONFIG, code, __VA_ARGS__)
#define LOG_CFG_WARN_WITH_CODE(code, ...)  LOG_WARN_WITH_CODE(LOG_MODULE_CONFIG, code, __VA_ARGS__)

// 安全校验模块日志
#define LOG_SEC_TRACE(...)  LOG_BASE(LOG_MODULE_SECURITY, TRACE, __VA_ARGS__)
#define LOG_SEC_DEBUG(...)  LOG_BASE(LOG_MODULE_SECURITY, DEBUG, __VA_ARGS__)
#define LOG_SEC_INFO(...)   LOG_BASE(LOG_MODULE_SECURITY, INFO, __VA_ARGS__)
#define LOG_SEC_WARN(...)   LOG_BASE(LOG_MODULE_SECURITY, WARN, __VA_ARGS__)
#define LOG_SEC_ERROR(...)  LOG_BASE(LOG_MODULE_SECURITY, ERROR, __VA_ARGS__)
#define LOG_SEC_CRITICAL(...) LOG_BASE(LOG_MODULE_SECURITY, CRITICAL, __VA_ARGS__)
#define LOG_SEC_ERROR_WITH_CODE(code, ...) LOG_ERROR_WITH_CODE(LOG_MODULE_SECURITY, code, __VA_ARGS__)
#define LOG_SEC_WARN_WITH_CODE(code, ...)  LOG_WARN_WITH_CODE(LOG_MODULE_SECURITY, code, __VA_ARGS__)

// ============== 调用链追踪宏 ==============

/**
 * @brief 函数入口追踪宏
 * 在函数开始处调用，自动记录TraceID和函数入口
 */
#define LOG_ENTRY() \
    do { \
        try { \
            auto _trace_id = vehicle::logging::generateTraceId(); \
            vehicle::logging::setTraceId(_trace_id); \
            LOG_TRACE("[ENTRY] TraceID={} function={} line={}", _trace_id, __FUNCTION__, __LINE__); \
        } catch (...) { \
            /* 日志系统可能未初始化，忽略 */ \
        } \
    } while(0)

/**
 * @brief 函数出口追踪宏
 * 在函数返回前调用，自动记录函数出口
 */
#define LOG_EXIT() \
    do { \
        try { \
            LOG_TRACE("[EXIT] TraceID={} function={} line={}", \
                vehicle::logging::getTraceId(), __FUNCTION__, __LINE__); \
        } catch (...) { \
            /* 日志系统可能未初始化，忽略 */ \
        } \
    } while(0)

/**
 * @brief 带返回值的函数出口追踪宏
 */
#define LOG_EXIT_WITH_VALUE(...) \
    do { \
        try { \
            std::string _result_str = fmt::format(__VA_ARGS__); \
            LOG_TRACE("[EXIT] TraceID={} function={} line={} result={}", \
                vehicle::logging::getTraceId(), __FUNCTION__, __LINE__, _result_str); \
        } catch (...) { \
            /* 日志系统可能未初始化，忽略 */ \
        } \
    } while(0)

/**
 * @brief 异常捕获宏 - 自动记录异常信息
 */
#define LOG_EXCEPTION(module, exception_ptr, ...) \
    do { \
        std::string _ex_msg; \
        try { \
            if (exception_ptr) { \
                _ex_msg = exception_ptr->what(); \
            } else { \
                _ex_msg = "unknown exception"; \
            } \
        } catch (...) { \
            _ex_msg = "failed to get exception message"; \
        } \
        LOG_ERROR_WITH_CODE(module, vehicle::error::Code::SYS_UNEXPECTED_EXCEPTION, \
            __VA_ARGS__ " | Exception: {}", _ex_msg); \
    } while(0)

// ============== 参数验证宏 ==============

/**
 * @brief 空指针检查宏
 */
#define CHECK_NULLptr(ptr, module, code, ret_val) \
    do { \
        if (!(ptr)) { \
            LOG_ERROR_WITH_CODE(module, code, "Null pointer detected: {}", #ptr); \
            return ret_val; \
        } \
    } while(0)

/**
 * @brief 参数范围检查宏
 */
#define CHECK_RANGE(val, min_val, max_val, module, code, ret_val) \
    do { \
        if ((val) < (min_val) || (val) > (max_val)) { \
            LOG_ERROR_WITH_CODE(module, code, \
                "Parameter out of range: {}={} (valid: [{}, {}])", \
                #val, val, min_val, max_val); \
            return ret_val; \
        } \
    } while(0)

/**
 * @brief 状态检查宏
 */
#define CHECK_STATE(condition, module, code, ret_val) \
    do { \
        if (!(condition)) { \
            LOG_ERROR_WITH_CODE(module, code, "State check failed: {}", #condition); \
            return ret_val; \
        } \
    } while(0)

// ============== 工具函数 ==============

namespace vehicle::logging {

// 前向声明，避免循环依赖
template<typename... Args>
inline std::string formatErrorMsg(const std::string& prefix, fmt::format_string<Args...> fmt, Args&&... args);

/**
 * @brief 格式化日志消息的辅助函数
 */
template<typename... Args>
inline std::string formatErrorMsg(const std::string& prefix, fmt::format_string<Args...> fmt, Args&&... args) {
    std::string msg = fmt::format(fmt, std::forward<Args>(args)...);
    return prefix + msg;
}

/**
 * @brief 获取当前模块名
 */
inline std::string getModuleName(const char* module) {
    return std::string("[") + module + "]";
}

}  // namespace vehicle::logging

#endif  // VEHICLE_LOG_MACROS_H
