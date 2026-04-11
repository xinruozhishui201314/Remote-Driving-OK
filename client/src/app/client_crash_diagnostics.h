#pragma once

#include <QString>

class QGuiApplication;

/**
 * 客户端「闪退 / 异常终止」相关观测：std::terminate、SIGABRT、SIGINT/SIGTERM（异步安全写
 * stderr，区分 ^C 与崩溃）、 X11/xcb 连接错误、Scene Graph 致命错误等。 与 libxcb 文档中
 * xcb_connection_has_error() 返回值语义一致（非 XErrorEvent.error_code）。
 */
namespace ClientCrashDiagnostics {

/** 在 QGuiApplication 之前调用：std::terminate 钩子、Linux SIGABRT/SIGINT/SIGTERM 等。 */
void installEarlyPlatformHooks();

/** QGuiApplication 已构造且 ClientLogging::init() 之后：xcb
 * 原生事件过滤、连接轮询（Linux+xcb+CLIENT_HAVE_XCB）。 */
void installAfterQGuiApplication(QGuiApplication &app);

/** QML 根窗口已创建后：为各 QQuickWindow 连接 sceneGraphError。 */
void installAfterTopLevelWindowsReady();

/**
 * 若 msg 为 Qt xcb 插件打印的 “The X11 connection broke (error N)” 则返回附加说明行（含 libxcb
 * 错误名），否则返回空。
 */
QString annotateIfX11Broke(const QString &msg);

}  // namespace ClientCrashDiagnostics
