#ifndef H264_CLIENT_DIAG_H
#define H264_CLIENT_DIAG_H

#include <QByteArray>
#include <QImage>
#include <QString>

/**
 * 客户端 H.264 / 解码帧诊断：落盘 PNG+RAW、解析 SPS/PPS 中与去块相关的位。
 *
 * 环境变量（均为可选）：
 *
 * CLIENT_VIDEO_SAVE_FRAME
 *   unset / 0 / off — 不落盘
 *   png  — 保存 PNG（解码后 RGBA，与 Scene Graph 前一致）
 *   raw  — 保存 .rgba 原始像素（行主序，每像素 4 字节，stride=width*4）
 *   both — 两者都保存
 *
 * CLIENT_VIDEO_SAVE_FRAME_DIR
 *   输出目录，默认：系统临时目录下的 remote-driving-frame-diag
 *
 * CLIENT_VIDEO_SAVE_FRAME_MAX
 *   每路解码器最多保存帧数，默认 3（避免磁盘打满）
 *
 * **首选判读（条状/花屏，优先于只猜 stride）**：
 *   1) 设本组环境变量落盘；文件名 `<sanitizedStream>_f<frameId>.png`（frameId 与解码器 emit 的帧号一致）。
 *   2) 在同一日志中按 **同 stream + 同 frameId（或 Evidence 行的 fid=）** 串联：
 *      `[H264][FrameDump]` → `DECODE_OUT` / `[Client][VideoEvidence]` → `RS_APPLY` → `SG_UPLOAD`。
 *   3) PNG 已花/错位 → 问题在解码/sws/隔行/色彩等上游；PNG 正常而窗口花 → 纹理上传/RHI/合成/DPR。
 *   建议同时 `CLIENT_VIDEO_EVIDENCE_CHAIN=1`（或 `CLIENT_VIDEO_PIPELINE_TRACE=1`）便于自动对齐 rowHash。
 *   日志 tag: [H264][FrameDump]
 *
 * CLIENT_VIDEO_H264_PARAM_DIAG
 *   非 0：在 SPS/PPS 变化时解析并打印 profile/level 与 PPS 中
 *   deblocking_filter_control_present_flag（ITU-T H.264 7.3.2.2）。
 *   说明：HEVC 的 loop_filter_across_slices_enabled_flag 在 H.264 中不存在；
 *   多 slice 与去块边界相关时需结合 slice header 的 disable_deblocking_filter_idc（本工具未解析
 * slice）。
 */
namespace H264ClientDiag {

void maybeDumpDecodedFrame(const QImage &frameRgba8888, const QString &streamTag, quint64 frameId,
                           int *inOutSavedCount);

void logParameterSetsIfRequested(const QString &streamTag, const QByteArray &spsNalWithHeader,
                                 const QByteArray &ppsNalWithHeader);

}  // namespace H264ClientDiag

#endif
