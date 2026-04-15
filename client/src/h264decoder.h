// h264decoder.h
#ifndef H264DECODER_H
#define H264DECODER_H

#include "media/DmaBufFrameHandle.h"
#include "media/RtpTrueJitterBuffer.h"

#include <QByteArray>
#include <QHash>
#include <QImage>
#include <QObject>
#include <QTimer>

#include <memory>

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
}

class RtpPacketSpscQueue;
class RtpStreamClockContext;
class H264WebRtcHwBridge;

class H264Decoder : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY(H264Decoder)
 public:
  explicit H264Decoder(const QString &streamTag = QString(), QObject *parent = nullptr);
  ~H264Decoder();

  void feedRtp(const uint8_t *data, size_t len, quint64 lifecycleId = 0);
  void reset();

  /** 解码线程：从 SPSC 环形队列批量拉取 RTP 再 feedRtp（替代每包 QueuedConnection） */
  void setIngressQueue(RtpPacketSpscQueue *q);

  /** 每路共享：RTCP SR 更新 RTP 时钟（可为 nullptr） */
  void setRtpClockContext(RtpStreamClockContext *ctx);

  /** 诊断：返回 frameReady(const QImage&, quint64) 信号的接收者数量 */
  int receiverCountFrameReady() const {
    return receivers(SIGNAL(frameReady(const QImage &, quint64)));
  }
  int receiverCountFrameReadyDmaBuf() const {
    return receivers(SIGNAL(frameReadyDmaBuf(SharedDmaBufFrame, quint64)));
  }

  /** v3 新增：获取当前帧的生命周期追踪 ID（由 RTP 包到达时分配）；原子，可供主线程诊断读取 */
  quint64 currentLifecycleId() const {
    return m_currentLifecycleId.load(std::memory_order_acquire);
  }

  /**
   * ★ 5-WHY 加固：主线程背压反馈。由主线程在 QML/VideoSink 成功处理帧后调用，
   * 标记该帧对应的池槽位已空闲。解码线程根据「未释放槽位」比例自动限帧/降采样。
   */
  void releaseFrame(quint64 frameId);

  /** 诊断：获取当前帧池积压比例（0.0-1.0）；>0.5 表明主线程排队严重 */
  double poolPressure() const;

  /**
   * WebRTC 硬解路径（VAAPI/NVDEC）：NV12→RGBA 完成后由此入口与软解共用帧池 / frameReady / 证据链。
   * 仅由 H264WebRtcHwBridge 在解码线程调用。
   */
  void ingestHardwareRgbaFrame(QImage&& rgba, const QString& hwBackendLabel);

  /**
   * WebRTC 硬解 DMA-BUF（VAAPI DRM PRIME）：经 Scene Graph EGLImage 路径呈现，不经 CPU NV12→RGBA。
   * 仅当构建含 CLIENT_HAVE_NV12_DMABUF_SG 且 H264WebRtcHwBridge 侧启用 CLIENT_WEBRTC_DMABUF_SG 时由桥接调用。
   */
  void ingestHardwareDmaBufFrame(VideoFrame&& vf, const QString& hwBackendLabel);

  /**
   * 主线程 1Hz 调用：取解码线程自上次调用以来 `emit frameReady` 的次数并清零。
   * 与 WebRtcClient::drainPresentSecondStats().framesToSink（QVideoSink::setVideoFrame
   * 成功次数）对齐，可钉死「解码有帧 / 主线程未呈现」。
   */
  int takeAndResetFrameReadyEmitDiagCount();

  /**
   * ★ 事件队列延迟测量：供 WebRtcClient 在主线程读取最后一次 emit frameReady 的墙钟 ms。
   * decode 线程写，主线程读；差值 = QueuedConnection 从 emit 到 onVideoFrameFromDecoder
   * 执行的排队延迟。
   */
  int64_t lastFrameReadyEmitWallMs() const {
    return m_lastFrameReadyEmitWallMs.load(std::memory_order_acquire);
  }

 signals:
  void frameReady(const QImage &image, quint64 frameId);
  void frameReadyDmaBuf(SharedDmaBufFrame handle, quint64 frameId);
  /** 丢包/seq 重同步等：建议向发送端请求 IDR（libdatachannel Track::requestKeyframe） */
  void senderKeyframeSuggested();
  /** ★ WHY5 修复：请求发送 RTCP NACK 包 */
  void nackPacketsRequested(const QByteArray &rtcpPacket);
  /**
   * 码流/解码配置风险（如多 slice + thread_count>1 易水平条纹）。
   * 解码线程发出；订阅方须 QueuedConnection。mitigationApplied=已关闭解码器并强制单线程，需 IDR。
   */
  void decodeIntegrityAlert(const QString &code, const QString &detail, bool mitigationApplied,
                          const QString &healthContractLine = QString());
  /** drainRtpIngressQueue 结束：morePending=队列仍非空，需再次调度 drain */
  void ingressDrainFinished(bool morePending);

 public slots:
  void drainRtpIngressQueue();
  /** teardown：在解码线程清空入站队列 */
  void clearIngressQueue();
  /**
   * Scene Graph DMA-BUF 呈现失败：须在解码线程执行（由 WebRtcClient QueuedConnection 投递）。
   * 关闭硬解 DRM PRIME 导出，后续帧走 CPU NV12→RGBA。
   */
  void onDmaBufSceneGraphPresentFailed();

 private:
  /** 仅解码线程调用（读 m_ctx / 硬解桥；主线程勿用） */
  QString buildVideoStreamHealthDetailLine() const;
  void logVideoStreamHealthContract(const QString &phase) const;
  void emitKeyframeSuggestThrottled(const char *reason);

  RtpPacketSpscQueue *m_ingressQueue = nullptr;

  /** SPSC pop 之后、feedRtp 之前：真抖动缓冲（RTCP SR / 自适应 / 固定） */
  RtpTrueJitterBuffer m_trueJitter;
  QTimer *m_playoutWakeTimer = nullptr;
  bool m_playoutEnvLogged = false;

  static constexpr size_t kRtpHeaderMinLen = 12;
  static constexpr int kRtpReorderBufferMax = 64; // 从 512 降低到 64，高丢包时更快跳过空洞（防止 5-10s 黑屏卡顿）
  static constexpr int kRtpJitterThreshold = 50;  // 新增：抖动阈值

  // RTP 排序
  QHash<quint16, QByteArray> m_rtpBuffer;
  quint16 m_rtpNextExpectedSeq = 0;
  bool m_rtpSeqInitialized = false;

  /** 帧唯一编号：递增，每帧一个 ID，便于端到端追踪（feedRtp → emit frameReady → QML handler） */
  quint64 m_frameIdCounter = 0;

  void processRtpPacket(const uint8_t *data, size_t len);
  void processRtpPayload(quint16 seq, quint32 ts, bool marker, const uint8_t *payload,
                         size_t payloadLen);
  void drainRtpBuffer();

  // FU-A
  QByteArray m_fuBuffer;
  bool m_fuStarted = false;
  int m_fuNalType = 0;

  // 帧聚合
  struct PendingFrame {
    PendingFrame() : timestamp(0), nalUnits(), rtpSeqs(), complete(false), hasKeyframe(false), closedByRtpMarker(false) {}
    quint32 timestamp = 0;
    std::vector<QByteArray> nalUnits;
    std::vector<quint16> rtpSeqs; // v4: 记录本帧包含的 RTP 序列号
    bool complete = false;
    bool hasKeyframe = false; // ★ 新增：标记本帧是否包含 IDR/SPS/PPS
    /** 本帧是否由 RTP M 位收尾（时间戳切换触发的 flush 为 false） */
    bool closedByRtpMarker = false;
  };
  PendingFrame m_pendingFrame;

  void appendNalToFrame(quint32 ts, quint16 rtpSeq, const uint8_t *nal, size_t nalLen, bool marker);
  void flushPendingFrame();

  // ★ WHY5 修复：NACK 相关
  QByteArray buildNackPacket(quint32 senderSsrc, quint32 mediaSsrc, const std::vector<quint16> &lostSeqs);
  void checkAndRequestNacks();
  /** 记录已请求 NACK 的序列号及其请求时间，用于重试/限频 */
  struct NackRequest {
      qint64 lastRequestMs = 0;
      int retryCount = 0;
  };
  QHash<quint16, NackRequest> m_pendingNacks;
  qint64 m_lastNackSentMs = 0;
  qint64 m_lastRtpPacketTime = 0;

  // SPS/PPS
  QByteArray m_sps;
  QByteArray m_pps;
  bool handleParameterSet(const uint8_t *nal, size_t nalLen);

  // 解码器
  const AVCodec *m_codec = nullptr;
  AVCodecContext *m_ctx = nullptr;
  bool m_codecOpen = false;
  bool m_haveDecodedKeyframe = false;
  // ★ 丢包恢复：丢包后不立即禁止P帧，而是标记"需要IDR"
  //   同时计数P帧，如果连续N帧没有IDR，才考虑更激进的策略
  bool m_needKeyframe = false;           // 丢包后标记，收到IDR时清除
  int m_framesSinceKeyframeRequest = 0;  // 丢包后已过帧数
  int m_expectedSliceCount = 0;          // 学习到的每帧slice数
  /** -1=按 CLIENT_FFMPEG_DECODE_THREADS；>=1 强制 libavcodec thread_count（条状自愈后锁 1） */
  int m_forcedDecodeThreadCount = -1;
  bool m_stripeMitigationApplied = false;
  bool m_loggedMultiSliceThreadOk = false;
  /** 首 N 帧打印 [H264][DecodePath]；CLIENT_H264_DECODE_PATH_DIAG=1 时每帧打印（高噪声） */
  int m_decodePathDiagFramesRemaining = 16;
  /** tryMitigate 早期返回原因各记一次日志（位掩码，reset 清零） */
  uint32_t m_stripeDiagSkipLoggedMask = 0;

  bool ensureDecoder();
  bool openDecoderWithExtradata();
  QByteArray buildAvccExtradata() const;
  /**
   * V1：仅刷新解码器实例缓冲（libavcodec drain+flush、WebRTC 硬解旁路 shutdown）。
   * 不修改 m_sps/m_pps，不修改 m_needKeyframe / m_haveDecodedKeyframe。
   */
  void flushCodecBuffers();
  /** 清除码流参数集（SPS/PPS），与 flushCodecBuffers 正交；错误恢复时与后者组合使用 */
  void resetStreamStateIncludingExtradata();
  /** 标记需等待新关键帧/GOP（与参数集是否保留无关） */
  void resetKeyframeGopExpectation();
  /** IDR 丢包恢复：flushCodecBuffers + resetKeyframeGopExpectation，保留参数集 */
  void idrRecoveryFlushCodecOnly();
  /** 码流损坏等：刷新解码器、丢弃参数集、等待新 IDR+参数集 */
  void recoverDecoderAfterCorruptBitstream();
  void closeDecoder();

  /**
   * 多 slice + FF_THREAD_SLICE 且 thread_count>1 时易水平条纹；须在 ensureDecoder() 已成功
   * （m_ctx 已分配且 codec 已 open）后调用。首帧在 flushPendingFrame 内 m_codecOpen 常仍为 false，
   * 故不能仅在 flushPendingFrame 判定。
   * @return true 已应用缓解并应放弃解码当前帧（调用方 return）
   */
  bool tryMitigateStripeRiskIfNeeded(int sliceCount, const char *phaseTag);

    void decodeCompleteFrame(const std::vector<QByteArray> &nalUnits, bool bitstreamIncomplete = false);
    void emitDecodedFrames(bool bitstreamIncomplete = false);

  // 色彩转换
  SwsContext *m_sws = nullptr;
  /** 当前 m_sws 对应的解码输出像素格式；与 frame->format 不一致时必须重建（FFmpeg sws 绑定源
   * fmt）。 */
  AVPixelFormat m_swsSrcFmt = AV_PIX_FMT_NONE;
  int m_width = 0;
  int m_height = 0;

  /**
   * 帧缓冲池：3槽轮转，消除主线程持帧时的 COW 堆分配。
   *
   * 背景：单帧缓冲 m_frameBuffer 方案下，解码线程 emit frameReady() 后主线程
   * 通过 QueuedConnection 持有同一 QImage（refcount=2）。下一帧 detach() 检测
   * refcount>1，触发 8MB COW 堆分配，每帧一次。
   * 4路×30fps×8MB = 960 MB/s 额外分配压力，在 CPU-only 模式下尤为严重。
   *
   * 3槽方案：解码线程轮转写入 pool[0→1→2→0]；主线程最多持有 1~2 个槽位，
   * 第3个槽位返回解码线程时 refcount==1，detach() 为 no-op，零分配。
   */
  static constexpr int kFramePoolSize = 60;
  QImage m_framePool[kFramePoolSize];
  int m_framePoolIdx = 0;

  /** v4 新增：槽位生命周期审计，钉死内存竞争/溢出 */
  enum class SlotStatus : int8_t { Idle = 0, Decoding = 1, Queued = 2, Reading = 3 };
  struct SlotAudit {
    std::atomic<SlotStatus> status{SlotStatus::Idle};
    std::atomic<quint64> lastFid{0};
    std::atomic<int64_t> lastAcquireMs{0};
  };
  SlotAudit m_slotAudit[kFramePoolSize];

  /** v4 新增：解码器跳帧策略审计 */
  int m_lastSkipFrame = -1;
  int m_lastSkipLoopFilter = -1;

  // ★ 诊断标签：用于区分四路解码器的日志（streamTag = cam_front/rear/left/right）
  QString m_streamTag;

  // 统计
  int m_framesEmitted = 0;
  int m_droppedPFrameCount = 0;
  int m_rtpPacketsProcessed = 0;
  quint16 m_lastRtpSeq = 0;
  /** 上一成功处理的 RTP SSRC（RFC 3550）；与 seq 联立可区分换流/混线与 16-bit seq 回绕误判 */
  quint32 m_lastSeenSsrc = 0;
  int m_logSendCount = 0;
  // ── 诊断日志增强 ─────────────────────────────────────────────────────────
  int64_t m_statsWindowStart = 0;            // 统计窗口开始时间
  int m_statsFramesInWindow = 0;             // 窗口内帧数
  int m_statsPacketsInWindow = 0;            // 窗口内 RTP 包数
  int m_statsDroppedInWindow = 0;            // 窗口内丢弃帧数
  int m_lastStatSeqNum = 0;                  // 上次统计时的 RTP seq（用于算丢包率）
  static const int kStatsIntervalMs = 1000;  // 1s 统计一次
  // ── 诊断日志增强结束 ───────────────────────────────────────────────────
  /** feedRtp 进入时刻（毫秒），用于与 RTP arrival 时间对比测量 libdatachannel→主线程延迟 */
  int64_t m_lastFeedRtpTime = 0;
  /** v3 新增：当前帧的端到端生命周期追踪 ID（由 RTP 包到达时分配） */
  std::atomic<quint64> m_currentLifecycleId{0};

  /** 解码线程：每次成功 emit frameReady 后 +1；主线程 takeAndResetFrameReadyEmitDiagCount 交换清零
   */
  std::atomic<int> m_diagFrameReadyEmitAccum{0};

  /** CLIENT_VIDEO_SAVE_FRAME：每路已落盘帧数（上限 CLIENT_VIDEO_SAVE_FRAME_MAX） */
  int m_diagFrameDumpCount = 0;

  /**
   * ★ 事件队列延迟测量：解码线程调用 emit frameReady 的墙钟时间（ms）。
   * 主线程在 onVideoFrameFromDecoder 中读取此值，差值 = QueuedConnection 排队延迟。
   * 多帧并发时以"最后一次 emit"时间为准（原子写覆盖）。
   */
  std::atomic<int64_t> m_lastFrameReadyEmitWallMs{0};

  qint64 m_lastKeyframeSuggestMs = 0;

  /**
   * 协商的 RTP payload type（与 WebRtcClient SDP m=video ... 96 一致）。
   * libdatachannel Track::onMessage 可能混入 RTCP（如 SR 第二字节为 200=0xC8）；
   * 若按 RTP 解析会得到 M=1、PT=72（0xC8&0x7F），进而污染 seq/SSRC —— 见 RFC 3550 §6.4.1。
   */
  int m_expectedRtpPayloadType = 96;
  int m_droppedNonH264RtpTotal = 0;
  int m_droppedNonH264RtpInStatsWindow = 0;

  /** media.hardware_decode（及兼容 CLIENT_WEBRTC_HW_DECODE 显式关）：IHardwareDecoder 旁路；与 m_codecOpen 互斥 */
  std::unique_ptr<H264WebRtcHwBridge> m_webrtcHw;
  bool m_webrtcHwActive = false;
  int64_t m_webrtcHwPts = 0;
  /** closeDecoder 后重置，便于条纹缓解 / 换路径后再次打 [VideoHealth][Stream] */
  bool m_videoHealthLoggedHwContract = false;
  bool m_videoHealthLoggedSwContract = false;
  /** ★ 性能优化：若 tryOpen 返回 false 或非硬解，不再每帧尝试，直到 reset/IDR */
  bool m_hwBridgeTryOpenFailed = false;

  /** 每秒统计窗口内：各类「可能致花屏/闪烁」事件计数（与 [H264][Stats] 同行输出） */
  int m_feedTooShortInWindow = 0;
  int m_feedBadRtpVersionInWindow = 0;
  int m_dupRtpIgnoredInWindow = 0;
  int m_reorderEnqueueInWindow = 0;
  int m_seqJumpResyncInWindow = 0;
  int m_rtpHdrParseFailInWindow = 0;
  int m_rtpNonMinimalHdrInWindow = 0;  // cc>0 或 extension，payload 偏移 != 12

  /** 每秒窗口：libavcodec 发送/接收失败、解码器打开失败、画质路径丢弃（sws/QImage 等） */
  int m_statsAvSendFailInWindow = 0;
  int m_statsAvRecvFailInWindow = 0;
  int m_statsEnsureDecoderFailInWindow = 0;
  int m_statsQualityDropInWindow = 0;
  int m_statsEagainSendInWindow = 0;

  /** 每秒窗口：RTP 空洞丢帧统计 — 量化「不完整码流导致条状」的频率 */
  int m_statsIncompleteFrameDropInWindow = 0;    // bitstreamIncomplete=true 且被丢弃的帧
  int m_statsIncompleteFrameEmitInWindow = 0;    // bitstreamIncomplete=true 但未丢弃（允许 emit）
  int m_statsIncompleteHoleTotalInWindow = 0;    // 本窗口所有空洞包总数（越高→丢包越严重）

  /** ★ RTP 序列跳跃历史：存储跳跃前最后 10 个包的 (seq, rtp_timestamp) 用于与 ZLM 侧对齐 */
  struct RtpSeqHistEntry {
    quint16 seq = 0;
    quint32 rtpTs = 0;
    int64_t wallMs = 0;
    quint8 marker = 0;
    quint8 payloadType = 0;
  };
  static constexpr int kRtpSeqHistSize = 10;
  std::array<RtpSeqHistEntry, kRtpSeqHistSize> m_rtpSeqHist{};
  int m_rtpSeqHistIdx = 0;    // 环形写入指针（mod kRtpSeqHistSize）
  int m_rtpSeqHistCount = 0;  // 已有效历史条目数（上限 kRtpSeqHistSize）
};

#endif