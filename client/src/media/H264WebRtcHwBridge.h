#pragma once

#include <QByteArray>
#include <QString>
#include <atomic>
#include <memory>

extern "C" {
#include <libavutil/pixfmt.h>
}

class H264Decoder;
class IHardwareDecoder;
struct SwsContext;

/**
 * WebRTC H.264 硬解旁路：Annex-B → IHardwareDecoder（VAAPI / NVDEC）→ NV12 CPU → RGBA →
 * H264Decoder::ingestHardwareRgbaFrame。
 *
 * 策略（主链路）：
 *   media.hardware_decode（默认 true）及 CLIENT_MEDIA_HARDWARE_DECODE 控制是否尝试硬解（Configuration，环境优先）。
 *   兼容：CLIENT_WEBRTC_HW_DECODE 仅在为非空且为 0/off/false/no 时强制关闭硬解（显式关，避免与配置「以为关了仍开」混淆）。
 * 其他环境变量：
 *   CLIENT_WEBRTC_HW_EXPORT_DMABUF  1 → VAAPI 侧 preferDmaBufExport（DRM PRIME / DMA-BUF）。
 *   CLIENT_WEBRTC_DMABUF_SG         空/1 →（构建含 CLIENT_HAVE_NV12_DMABUF_SG 时）DMA-BUF 帧走
 *                                    H264Decoder::frameReadyDmaBuf → RemoteVideoSurface EGL 路径；
 *                                    0/off → 丢弃 DMA-BUF 帧（可改 export=0 走 CPU NV12）。
 *   CLIENT_VIDEO_ALLOW_DMABUF_UNDER_SOFTWARE_GL  1 → 即使 LIBGL_ALWAYS_SOFTWARE=1 仍尊重
 *                                    CLIENT_WEBRTC_HW_EXPORT_DMABUF（默认软件栈下 effective=0）。
 *   Scene Graph 导入失败时：自动 setDmaBufExportPreferred(false)，后续帧走 CPU NV12→RGBA。
 */
class H264WebRtcHwBridge {
 public:
  explicit H264WebRtcHwBridge(QString streamTag);
  ~H264WebRtcHwBridge();

  H264WebRtcHwBridge(const H264WebRtcHwBridge&) = delete;
  H264WebRtcHwBridge& operator=(const H264WebRtcHwBridge&) = delete;

  /** 是否应走硬解尝试（配置 + 兼容关断）。 */
  static bool hardwareDecodeRequested();
  /** @deprecated 语义同 hardwareDecodeRequested()，保留别名。 */
  static bool requestedByEnv() { return hardwareDecodeRequested(); }
  static const char* kEnvVarName;

  bool isActive() const { return static_cast<bool>(m_dec); }
  bool isHardwareAccelerated() const;

  bool tryOpen(const QByteArray& avccExtradata, int codedW, int codedH);
  void shutdown();
  void flush();

  /** @return false 表示硬解 API 硬错误，调用方可 flush + 等 IDR */
  bool submitCompleteAnnexB(H264Decoder* dec, const uint8_t* annexB, size_t len, int64_t pts);

  /**
   * Scene Graph DMA-BUF 呈现失败（主线程经 H264Decoder 排队到解码线程调用）：
   * 关闭 DMA-BUF 导出，后续 receiveFrame 输出 CPU NV12→ingest Rgba。
   */
  void disableDmaBufExportForCpuFallback();

  /** Per-stream 健康模型：tryOpen 时的导出 / SG 意图（与运行时 sgBroken 对照） */
  bool preferDmaBufExport() const { return m_preferDmaBufExport; }
  bool dmaBufSceneGraphEnvWanted() const { return m_dmabufSgEnvWanted; }
  bool dmaBufSgBroken() const { return m_dmaBufSgBroken.load(std::memory_order_acquire); }
  QString backendLabel() const { return m_backendLabel; }

 private:
  bool drainAllOutput(H264Decoder* dec);
  bool convertCpuFrameToRgbaAndIngest(H264Decoder* dec, struct VideoFrame& vf);

  QString m_streamTag;
  std::unique_ptr<IHardwareDecoder> m_dec;
  struct SwsContext* m_swsNv12ToRgba = nullptr;
  int m_swsW = 0;
  int m_swsH = 0;
  AVPixelFormat m_swsSrcFmt = AV_PIX_FMT_NONE;
  QString m_backendLabel;
  std::atomic<bool> m_dmaBufSgBroken{false};
  bool m_preferDmaBufExport = false;
  bool m_dmabufSgEnvWanted = false;
};
