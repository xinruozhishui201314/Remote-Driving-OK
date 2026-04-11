#pragma once

#include <QJsonObject>
#include <QString>

/**
 * 多路并行视频：进程级环境策略快照 + 统一日志前缀 [Client][VideoHealth]。
 * 与每路 H264Decoder / H264WebRtcHwBridge 运行时状态组合，便于 grep stream= 对齐四路契约。
 */
struct ClientVideoGlobalPolicySnapshot {
  QString webRtcHwDecodeEnvRaw;
  /** 与 H264WebRtcHwBridge::hardwareDecodeRequested() 一致（配置 media.hardware_decode + 兼容关断） */
  bool webRtcHwDecodeEffective = false;
  bool mediaRequireHardwareDecode = false;

  /** 编译期：CMake 探测到 libva+libva-drm 且定义 ENABLE_VAAPI */
  bool hwDecodeVaapiCompiled = false;
  /** 编译期：ENABLE_NVDEC（CMake 默认 ON，无 FFmpeg 时关闭）且 FFmpeg 含 CUDA/NVDEC 链 */
  bool hwDecodeNvdecCompiled = false;

  bool ffmpegCompiled = false;
  bool eglDmabufCompiled = false;

  /** CLIENT_WEBRTC_DMABUF_SG 原始值（契约校验：显式开启但无 NV12 SG 编译 → ERROR） */
  QString webRtcDmabufSgEnvRaw;

  int webRtcHwExportDmaBufEnv = 0;  // qEnvironmentVariableIntValue
  bool webRtcDmabufSgEnvEffective = false;
  bool nv12DmabufSgCompiled = false;

  QString ffmpegDecodeThreadsEnvRaw;
  int ffmpegDecodeThreadsParsedDefault = 1;

  QString stripeAutoMitigationEnvRaw;
  bool stripeAutoMitigationEffective = true;

  QString libglAlwaysSoftware;
  QString qsgRhiBackend;
  QString clientAssumeSoftwareGl;
  bool videoDecoupledPresentEffective = true;

  /** CLIENT_H264_DECODE_FRAME_SUMMARY_EVERY：未设置时默认 60；0=关闭周期性 [FrameSummary] */
  QString h264DecodeFrameSummaryEveryEnvRaw;
  int decodeFrameSummaryEveryEffective = 60;

  int h264StripeDiagEnv = 0;

  /** CLIENT_VIDEO_CPU_PRESENT_FORMAT_STRICT：未设或非 0/false/off/no → CPU 呈现（Surface+QVideoSink）仅接受 RGBA8888+stride */
  QString cpuPresentFormatStrictEnvRaw;
  bool cpuPresentFormatStrictEffective = true;

  /** CLIENT_VIDEO_INTERLACED_POLICY 原始值；空=默认 blend（见 VideoInterlacedPolicy） */
  QString interlacedPolicyEnvRaw;
  QString interlacedPolicyTag;
  QString swsColorspaceEnvRaw;
  QString swsColorspaceTag;

  static ClientVideoGlobalPolicySnapshot capture();

  /** 单行短摘要，适合与 per-stream 行拼接 */
  QString formatGlobalBracket() const;
};

namespace ClientVideoStreamHealth {

/** 进程内仅打印一次：全量环境策略（四路共享，避免每路重复刷屏） */
void logGlobalEnvOnce();

/** 缓存的进程级策略（首次调用时 capture） */
const ClientVideoGlobalPolicySnapshot& globalPolicy();

/** 供 MQTT / DataChannel hint 附带的 JSON 对象（扁平、可版本演进） */
void fillJsonClientVideoPolicy(QJsonObject& out);

/**
 * 是否将显示栈视为软件光栅（LIBGL_ALWAYS_SOFTWARE=1 或 CLIENT_ASSUME_SOFTWARE_GL 真值）。
 * 用于硬解 DRM PRIME / 多线程软解等「正确性优先」策略。
 */
bool displayStackAssumedSoftwareRaster();

/**
 * 实际是否启用硬解 DMA-BUF 导出：环境 CLIENT_WEBRTC_HW_EXPORT_DMABUF=1 时，在软件光栅栈上默认
 * 强制为 false，避免 [DROP_DMABUF] 与 Mesa/llvmpipe 路径花屏。
 * 显式 CLIENT_VIDEO_ALLOW_DMABUF_UNDER_SOFTWARE_GL=1 时尊重环境导出开关。
 */
bool effectivePreferDmaBufExport();

/**
 * 软件光栅下是否应将 libavcodec slice 线程数限制为 1（即使 CLIENT_FFMPEG_DECODE_THREADS>1）。
 * 放行：CLIENT_VIDEO_ALLOW_MULTITHREAD_DECODE_UNDER_SOFTWARE_GL=1。
 */
bool shouldForceSingleThreadDecodeUnderSoftwareRaster(int envRequestedThreads);

/**
 * CPU 侧呈现是否拒绝非 RGBA8888/stride 契约（默认 true）。
 * 影响：RemoteVideoSurface（applyFrame/updatePaintNode）与 QVideoSink::setVideoFrame 路径一致。
 * 关闭：CLIENT_VIDEO_CPU_PRESENT_FORMAT_STRICT=0|false|off|no（允许 convertToFormat，有损性能且可能掩盖上游 bug）
 */
bool cpuPresentFormatStrict();

/** 仅单元测试：使 globalPolicy() 重新读取环境（非测试勿用） */
void debugResetPolicyCacheForTest();

}  // namespace ClientVideoStreamHealth
