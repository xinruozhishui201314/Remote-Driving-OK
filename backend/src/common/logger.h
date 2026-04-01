#pragma once
#ifndef TELEOP_COMMON_LOGGER_H
#define TELEOP_COMMON_LOGGER_H

#include <string>
#include <memory>
#include <spdlog/spdlog.h>
#include <spdlog/fmt/fmt.h>

namespace teleop::common {

/**
 * @brief 统一日志管理器
 * 封装 spdlog，提供结构化 JSON 输出、TraceID 透传等功能
 */
class Logger {
public:
    /**
     * @brief 初始化日志系统
     * @param node_id 节点ID (如 "backend-01", "vin:LSV12345")
     * @param level 日志级别 (trace, debug, info, warn, err, critical, off)
     */
    static void init(const std::string& node_id, const std::string& level = "info");

    /**
     * @brief 关闭日志系统（flush 缓冲）
     */
    static void shutdown();

    /**
     * @brief 设置当前线程的 TraceID (跨服务追踪)
     * @param trace_id 追踪ID
     */
    static void setTraceId(const std::string& trace_id);

    /**
     * @brief 获取当前日志器指针（用于直接使用 spdlog 功能）
     */
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

    // 特殊的 LATENCY 级别（用于性能分析）
    template<typename... Args>
    static void latency(fmt::format_string<Args...> fmt, Args&&... args) {
        if (s_logger) s_logger->info(fmt, args...);
    }

private:
    static std::string s_node_id;
    static std::shared_ptr<spdlog::logger> s_logger;
    
    // 自定义格式化器：在 Pattern 中注入 TraceID 和 NodeID
    // 由于 spdlog pattern 功能限制，我们这里通过 custom_formatter 或者简单的 pattern 实现
    // 这里我们使用 pattern string，在 pattern 中使用 %t (thread_id) 存储和 TraceID 不一定对应，
    // 更好的方式是使用自定义 sink，或者每次手动传入 context。
    // 为了简单且符合 project_spec.md，我们在 pattern 中固定字段，TraceID 通过线程局部变量或 context 传入
};

// 宏定义：方便快速调用，自动包含 file/line
#define LOG_TRACE(...) ::teleop::common::Logger::getLogger()->trace(__VA_ARGS__)
#define LOG_DEBUG(...) ::teleop::common::Logger::getLogger()->debug(__VA_ARGS__)
#define LOG_INFO(...)  ::teleop::common::Logger::getLogger()->info(__VA_ARGS__)
#define LOG_WARN(...)  ::teleop::common::Logger::getLogger()->warn(__VA_ARGS__)
#define LOG_ERROR(...) ::teleop::common::Logger::getLogger()->error(__VA_ARGS__)
#define LOG_CRITICAL(...) ::teleop::common::Logger::getLogger()->critical(__VA_ARGS__)

// 延迟日志（专门用于统计）
#define LOG_LATENCY(...) ::teleop::common::Logger::latency(__VA_ARGS__)

} // namespace teleop::common

#endif // TELEOP_COMMON_LOGGER_H
