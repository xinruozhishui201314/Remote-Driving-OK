#pragma once

#include <QString>

struct SwsContext;

/**
 * 配置 libswscale：YUV（有限幅）→ RGBA 8-bit 全范围 时使用的矩阵与范围。
 * 默认与 GPU 路径 video.frag / nv12_dmabuf.frag 一致：BT.709 有限幅 → 全范围 RGB。
 *
 * 环境变量 CLIENT_VIDEO_SWS_COLORSPACE（默认 bt709_limited）：
 *   bt709_limited — BT.709 矩阵，源 limited (MPEG)，目标 RGB full
 *   bt601_limited — BT.601（SD 源），源 limited，目标 RGB full
 *   default       — 不调用 sws_setColorspaceDetails，沿用 FFmpeg 默认推断
 */
void videoSwsConfigureYuvToRgbaColorspace(SwsContext *sws);

/** 单行诊断：当前生效的矩阵策略标记 */
QString videoSwsColorspaceDiagnosticsTag();
