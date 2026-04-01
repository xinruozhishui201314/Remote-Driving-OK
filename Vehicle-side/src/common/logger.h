#pragma once
#ifndef VEHICLE_SIDE_COMMON_LOGGER_H
#define VEHICLE_SIDE_COMMON_LOGGER_H

#include <string>
#include <memory>
#include <spdlog/spdlog.h>
#include <spdlog/fmt/fmt.h>

namespace vehicle::common {

/**
 * @brief 统一日志管理器
 * 封装 spdlog，提供结构化 JSON 输出、TraceID 透传等功能
 */
class Logger {
public:
    /**
     * @brief 初始化日志系统
     * @param node_id 节点ID (如 "vin:LSV12345")
     * @param level 日志级别
     */
    static void init(const std::string& node_id, const std::string& level = "info");

    static void shutdown();

    /**
     * @brief 设置当前线程的 TraceID
     */
    static void setTraceId(const std::string& trace_id);

    static std::shared_ptr<spdlog::logger>& getLogger();

    // 级别别名 (非模板，在 .cpp 中实现)
    static void trace(const std::string& msg);
    static void debug(const std::string& msg);
    static void info(const std::string& msg);
    static void warn(const std::string& msg);
    static void error(const std::string& msg);
    static void critical(const std::string& msg);

    // 带格式参数的日志 (模板，在此处定义以避免链接错误)
    template<typename... Args>
    static void trace(fmt::format_string<Args...> fmt, Args&&... args) {
        if (s_logger) s_logger->trace(fmt, args...);
    }

    template<typename... Args>
    static void debug(fmt::format_string<Args...> fmt, Args&&... args) {
        if (s_logger) s_logger->debug(fmt, args...);
    }

    template<typename... Args>
    static void info(fmt::format_string<Args...> fmt, Args&&... args) {
        if (s_logger) s_logger->info(fmt, args...);
    }

    template<typename... Args>
    static void warn(fmt::format_string<Args...> fmt, Args&&... args) {
        if (s_logger) s_logger->warn(fmt, args...);
    }

    template<typename... Args>
    static void error(fmt::format_string<Args...> fmt, Args&&... args) {
        if (s_logger) s_logger->error(fmt, args...);
    }

    template<typename... Args>
    static void critical(fmt::format_string<Args...> fmt, Args&&... args) {
        if (s_logger) s_logger->critical(fmt, args...);
    }

    // 特殊的 LATENCY 级别
    template<typename... Args>
    static void latency(fmt::format_string<Args...> fmt, Args&&... args) {
        if (s_logger) s_logger->info(fmt, args...);
    }

private:
    static std::string s_node_id;
    static std::shared_ptr<spdlog::logger> s_logger;
};

// 宏定义：方便快速调用
#define LOG_TRACE(...) ::vehicle::common::Logger::getLogger()->trace(__VA_ARGS__)
#define LOG_DEBUG(...) ::vehicle::common::Logger::getLogger()->debug(__VA_ARGS__)
#define LOG_INFO(...)  ::vehicle::common::Logger::getLogger()->info(__VA_ARGS__)
#define LOG_WARN(...)  ::vehicle::common::Logger::getLogger()->warn(__VA_ARGS__)
#define LOG_ERROR(...) ::vehicle::common::Logger::getLogger()->error(__VA_ARGS__)
#define LOG_CRITICAL(...) ::vehicle::common::Logger::getLogger()->critical(__VA_ARGS__)

#define LOG_LATENCY(...) ::vehicle::common::Logger::latency(__VA_ARGS__)

} // namespace vehicle::common

#endif // VEHICLE_SIDE_COMMON_LOGGER_H
