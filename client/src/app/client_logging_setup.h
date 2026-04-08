#pragma once

class QString;

/**
 * 启动/关闭 main.cpp 使用的异步文件日志（AsyncLogQueue + Qt message handler）。
 * 与 Logger 类并存；后续可统一迁移至 Logger::instance()。
 */
namespace ClientLogging {

bool init();
void shutdown();

} // namespace ClientLogging
