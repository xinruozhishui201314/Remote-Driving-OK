#include "logger.h"
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <chrono>
#include <iomanip>

namespace teleop::common {

std::string Logger::s_node_id = "unknown";
std::shared_ptr<spdlog::logger> Logger::s_logger;

// 自定义 TraceID 存储（线程局部）
// 在 Pattern 中使用 %{trace_id} 需要 spdlog 支持，或者我们自定义格式化器。
// 为了兼容性，这里使用简单的 JSON Pattern，TraceID 通过 context 或宏注入比较复杂，
// 暂时在日志体中不包含 TraceID 字段，或者在后续通过 Logger::setTraceId 实现。
// 这里的实现重点在于 JSON 格式输出。

void Logger::init(const std::string& node_id, const std::string& level) {
    s_node_id = node_id;

    try {
        // Console Sink
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        console_sink->set_level(spdlog::level::trace); // console output everything
        console_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [thread %t] %v");

        // File Sink (optional)
        std::vector<spdlog::sink_ptr> sinks = {console_sink};
        
        s_logger = std::make_shared<spdlog::logger>("teleop", begin(sinks), end(sinks));
        s_logger->set_level(spdlog::level::from_str(level));
        
        // 设置默认格式化器为 JSON (统一 ISO 8601 时间戳)
        // Timestamp: ISO 8601, Level: string, Node: string, Msg: string
        spdlog::set_pattern(fmt::format("{{\"timestamp\": \"{}\", \"level\": \"{}\", \"node_id\": \"{}\", \"msg\": \"%v\"}}", 
                             "%Y-%m-%dT%H:%M:%S.%f", "%l", s_node_id));
        spdlog::set_level(spdlog::level::from_str(level));

        // 注册全局
        spdlog::set_default_logger(s_logger);

    } catch (const spdlog::spdlog_ex& ex) {
        std::cerr << "Log initialization failed: " << ex.what() << std::endl;
    }
}

void Logger::shutdown() {
    spdlog::shutdown();
}

void Logger::setTraceId(const std::string& trace_id) {
    // 在简单的 pattern 实现中，直接注入 TraceID 比较困难。
    // 通常做法是将 trace_id 放入 thread_local storage，custom formatter 读取它。
    // 这里暂时记录到 MDC (Mapped Diagnostic Context)，如果有类似机制。
    // 或者我们可以暂时忽略在 JSON root 的 trace_id，只记录在 msg 里。
    // 高级实现：重写 formatter。
}

std::shared_ptr<spdlog::logger>& Logger::getLogger() {
    return s_logger;
}

// 实现各种级别日志
void Logger::trace(const std::string& msg) { s_logger->trace(msg); }
void Logger::debug(const std::string& msg) { s_logger->debug(msg); }
void Logger::info(const std::string& msg) { s_logger->info(msg); }
void Logger::warn(const std::string& msg) { s_logger->warn(msg); }
void Logger::error(const std::string& msg) { s_logger->error(msg); }
void Logger::critical(const std::string& msg) { s_logger->critical(msg); }

// 延迟日志使用 info 级别，但带上 [LATENCY] 标记，或者如果支持自定义 Level
void Logger::latency(const std::string& msg) { s_logger->info(msg); }

} // namespace teleop::common
