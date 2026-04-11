#pragma once

class QObject;

/**
 * 可选：Docker/X11 等环境下「窗框正常但客户区像宿桌面」的钉因观测。
 *
 * - CLIENT_PRESENT_COMP_DIAG=1：同时打开 1Hz 汇总与 xwininfo（Linux）
 * - CLIENT_PRESENT_HEALTH_1HZ=1：每秒 [Client][PresentHealth][1Hz] frameSwapped 累计/增量（全程）
 * - CLIENT_XWININFO_SNAPSHOT=1：Linux+xcb 下抓取 xwininfo（标题替换为 <redacted-title>）
 *
 * 默认（无上述显式开关时）：若同时满足 软件 GL(LIBGL_ALWAYS_SOFTWARE=1) + 疑似容器 + 交互式 GUI，
 * 且非 CI、且 CLIENT_AUTO_PRESENT_HEALTH 未设为 0，则自动启用 PresentHealth **15 秒** 后停止。
 *
 * 在 logQmlPostLoadSummary 末尾调用（QML 已 load、顶层窗口已存在）。
 */
namespace ClientPresentCompositionDiag {

void installIfEnabled(QObject *parentForTimers);

}  // namespace ClientPresentCompositionDiag
