#pragma once

#include <QString>

struct AVFrame;
struct VideoFrame;

namespace VideoInterlacedPolicy {

enum class Policy {
  Off,           // 忽略元数据
  WarnOnly,      // 仅日志/指标，不改像素
  BlendLines,    // 奇数行与上下行均值（简易去隔行）
  TopFieldDup,   // 空间上场重复（偶数行复制填充）
  BottomFieldDup // 空间下场重复（奇数行复制填充）
};

Policy currentFromEnv();

/** 与 currentFromEnv() 一致的短标签，供 [VideoHealth] bracket */
QString diagnosticsTag();

QString envRaw();

/**
 * 仅当 AVFrame 带 AV_FRAME_FLAG_INTERLACED 时按策略改像素（yuv420p/nv12/yuvj420p）。
 * 在 sws_scale → RGBA 之前调用。
 */
void maybeApplyAvFrame(AVFrame *frame, const QString &streamTag);

/** 硬件/CPU NV12 平面：依赖 VideoFrame::interlacedMetadata（由解码器从 AVFrame 拷贝） */
void maybeApplyToNv12VideoFrame(VideoFrame &vf, const QString &streamTag);

bool avFrameIsInterlaced(const AVFrame *frame);

}  // namespace VideoInterlacedPolicy
