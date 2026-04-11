#pragma once

/**
 * Linux + xcb：在 QML 顶层窗口已创建后，自动记录 X11 visual / depth / TrueColor 等（钉「透底」类问题）。
 *
 * - 默认启用（交互式 xcb）；关闭：CLIENT_X11_VISUAL_PROBE=0
 * - 依赖 CLIENT_HAVE_XCB（与 crash_diag 相同）；未编译时打一行跳过说明
 *
 * 须在 QGuiApplication 事件循环将跑未跑之间调用；内部 Qt::singleShot(0) 推迟到下一拍以确保 winId 已实现。
 */
namespace ClientX11VisualProbe {

void scheduleLogTopLevelQuickWindowVisuals();

/**
 * Linux xcb：在 QGuiApplication 已就绪后记录 EWMH 混成器/WM 标识（_NET_SUPPORTING_WM_CHECK + _NET_WM_NAME）。
 * grep: [Client][X11WM]。关闭：CLIENT_X11_WM_PROBE=0。无 CLIENT_HAVE_XCB 时打一行跳过。
 */
void logX11WindowManagerInfo();

}  // namespace ClientX11VisualProbe
