#ifndef CLIENT_VIDEO_DIAG_CACHE_H
#define CLIENT_VIDEO_DIAG_CACHE_H

#include <QString>

class QQuickWindow;

/**
 * 跨模块缓存视频诊断用环境信息（X11 EWMH 混成器名等），供 RemoteVideoSurface 与 X11 探测共享。
 * 线程安全：读写均带锁；调用量低（每帧最多几次日志路径）。
 */
namespace ClientVideoDiagCache {

void setX11NetWmName(const QString &name);

QString x11NetWmName();

/**
 * 单行汇总：graphicsApi、QSG_RHI_BACKEND、X11 _NET_WM_NAME（若已由探测写入）。
 * 用于与 [Client][X11WM] 对照；未探测时 wm 为 "-"。
 */
QString renderStackSummaryLine(const QQuickWindow *win);

/**
 * 与条状/stride 排障相关的环境指纹（单行，便于粘贴到工单）。
 * 与 Qt QQuickWindow::createTextureFromImage、Mesa LIBGL_ALWAYS_SOFTWARE 文档对照。
 */
QString videoPipelineEnvFingerprint();

}  // namespace ClientVideoDiagCache

#endif
