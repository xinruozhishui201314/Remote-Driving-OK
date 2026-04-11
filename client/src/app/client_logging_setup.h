#pragma once

class QGuiApplication;
class QString;

/**
 * 启动/关闭 main.cpp 使用的异步文件日志（AsyncLogQueue + Qt message handler）。
 * 与 Logger 类并存；后续可统一迁移至 Logger::instance()。
 */
namespace ClientLogging {

bool init();
void shutdown();

/**
 * Unix：SIGINT/SIGTERM 默认可能绕过 QCoreApplication::quit，导致异步日志未 drain。
 * 在 QGuiApplication 构造且 init() 之后调用：自管道 + QSocketNotifier 在主线程 shutdown 并 exit(130/143)。
 * 非 Unix 或 pipe 失败时为 no-op。
 */
void installUnixSignalLogFlushAndQuit(QGuiApplication &app);

}  // namespace ClientLogging
