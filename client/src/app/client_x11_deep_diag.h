#pragma once

#include <QString>

class QObject;

/**
 * 可选 X11 / Qt Quick 深度诊断：用于定位 XCB_CONN_CLOSED_REQ_LEN_EXCEED 等「单条 X
 * 请求过长」类问题。
 *
 * 启用：环境变量 CLIENT_X11_DEEP_DIAG=1
 * 效果（摘要）：
 * - 启动前合并 qt.qpa.xcb.debug=true（与 CLIENT_QPA_XCB_DEBUG=1 同类，见 Qt QLoggingCategory）
 * - 周期性打印 xcb_get_maximum_request_length 与「整窗 RGBA32 粗算字节数」对照（对照 libxcb
 * 文档：大包需自行分片）
 * - QQuickWindow::sceneGraphInitialized 时在渲染线程打印 GL_VENDOR/RENDERER（若有当前 GL 上下文）
 * - 前若干帧 frameSwapped 打时间戳（确认崩溃发生在首帧前/后）
 */
namespace ClientX11DeepDiag {

/** 在 QGuiApplication 构造之前调用：根据环境变量合并 QLoggingCategory 规则。 */
void mergePreAppLoggingRules(QString &inOutFilterRules);

/** 在 engine.load() 之后、exec() 之前调用：挂接窗口与定时采样。 */
void installAfterQmlLoaded(QObject *appParentForTimers);

}  // namespace ClientX11DeepDiag
