#include "logger.h"
#include <iostream>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/async.h> // 异步日志，避免阻塞控制线程

namespace vehicle::common {

std::string Logger::s_node_id = "unknown";
std::shared_ptr<spdlog::logger> Logger::s_logger;
std::shared_ptr<spdlog::details::thread_pool> Logger::s_thread_pool;

void Logger::init(const std::string& node_id, const std::string& level) {
    // Plan 5.2: Fix node_id to be descriptive
    s_node_id = node_id.empty() ? "vehicle-side" : node_id;

    try {
        // Plan 11: 严格 JSON 格式 (移除颜色码)
        // 修复：必须保存 thread_pool 的强引用，否则 async_logger 内的 weak_ptr 会失效导致崩溃
        // 报错信息：async log: thread pool doesn't exist anymore
        s_thread_pool = std::make_shared<spdlog::details::thread_pool>(8192, 1);
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        
        // 创建异步 logger
        s_logger = std::make_shared<spdlog::async_logger>("vehicle", console_sink, s_thread_pool, spdlog::async_overflow_policy::block);
        
        s_logger->set_level(spdlog::level::from_str(level));

        // 设置 JSON 格式 (统一 ISO 8601 时间戳) - Plan 5.1
        // Ensure strict JSON format without extra newlines inside the json object itself if possible
        // 使用 %^%l%$ 移除 level 的颜色标记（虽然已换 sink，但以防万一）
        spdlog::set_pattern(fmt::format("{{\"timestamp\": \"{}\", \"level\": \"{}\", \"node_id\": \"{}\", \"msg\": \"%v\"}}", 
                             "%Y-%m-%dT%H:%M:%S.%f", "%^%l%$", s_node_id));
        spdlog::set_level(spdlog::level::from_str(level));
        spdlog::flush_on(spdlog::level::warn); // warn 级别及以上立即 flush
        
        // 注册
        spdlog::set_default_logger(s_logger);
        spdlog::flush_on(spdlog::level::warn); // warn 级别及以上立即 flush

    } catch (const spdlog::spdlog_ex& ex) {
        std::cerr << "[Vehicle-side][Logger] Initialization failed: " << ex.what() << std::endl;
    }
}

void Logger::shutdown() {
    spdlog::shutdown();
}

void Logger::setTraceId(const std::string& trace_id) {
    // 暂不实现，需要 custom formatter
}

std::shared_ptr<spdlog::logger>& Logger::getLogger() {
    return s_logger;
}

void Logger::trace(const std::string& msg) { if (s_logger) s_logger->trace(msg); }
void Logger::debug(const std::string& msg) { if (s_logger) s_logger->debug(msg); }
void Logger::info(const std::string& msg) { if (s_logger) s_logger->info(msg); }
void Logger::warn(const std::string& msg) { if (s_logger) s_logger->warn(msg); }
void Logger::error(const std::string& msg) { if (s_logger) s_logger->error(msg); }
void Logger::critical(const std::string& msg) { if (s_logger) s_logger->critical(msg); }

} // namespace vehicle::common
