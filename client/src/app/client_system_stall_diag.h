#pragma once

#include <QtGlobal>

class QGuiApplication;

/**
 * 系统级卡滞/阻塞诊断：主线程事件循环调度间隙 + QQuickWindow Scene Graph GL 字符串。
 *
 * 环境变量：
 * - CLIENT_MAIN_THREAD_STALL_DIAG=1
 *   在主线程挂高精度定时器，测量相邻两次触发的墙钟间隔；若显著大于 interval，
 *   说明主线程长时间未运行事件循环（长槽、同步阻塞、或极端排队）。
 * - CLIENT_MAIN_THREAD_STALL_INTERVAL_MS（可选，默认 20，范围 5–100）
 * - CLIENT_MAIN_THREAD_STALL_WARN_EXTRA_MS（可选，默认 45）：判定 stall 的额外余量，
 *   即 gap > interval + extra 计一次 stall 并打 instant WARN。
 * - CLIENT_VIDEO_SCENE_GL_LOG=1
 *   在 QQuickWindow::sceneGraphInitialized 打印 GL_VENDOR/GL_RENDERER（与 RHI 非 GL 时跳过）。
 */
namespace ClientSystemStallDiag {

void installMainThreadWatchdogIfEnabled(QGuiApplication *app);
void hookQuickWindowsSceneGlIfEnabled();

bool isMainThreadWatchdogEnabled();

struct MainThreadWatchdogSecondStats {
  int maxTickGapMs = 0;
  int stallEvents = 0; /**< 本秒内 gap > interval+extra 的次数 */
};

/** 由 WebRtcStreamManager 1Hz 汇总调用；与视频诊断同一节拍对齐。 */
MainThreadWatchdogSecondStats drainMainThreadWatchdogSecondStats();

}  // namespace ClientSystemStallDiag
