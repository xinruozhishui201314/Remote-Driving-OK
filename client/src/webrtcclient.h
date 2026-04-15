#ifndef WEBRTCCLIENT_H
#define WEBRTCCLIENT_H

#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaMethod>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QPointer>
#include <QSize>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <QVideoSink>
#include <QtQml/qqmlregistration.h>

#include <atomic>
#include <cstdint>
#include <memory>

/**
 * 视频呈现后端（互斥状态机）：bindVideoOutput 与 bindVideoSurface 不得同时指向存活对象。
 * InternalPlaceholderSink：均未绑定有效项时帧进入 m_ownedSink。
 */
enum class WebRtcPresentBackend : std::uint8_t {
  InternalPlaceholderSink = 0,
  VideoOutput = 1,
  RemoteSurface = 2,
  InvalidBothBound = 255,
};

/** 主线程呈现路径每秒汇总（供 1Hz 诊断日志）；与 Qt6 QVideoSink::setVideoFrame 官方路径对齐统计 */
struct WebRtcPresentSecondStats {
  int framesToSink = 0; /**< 成功 setVideoFrame */
  int nullSink = 0;     /**< activeSink 为空 */
  int invalidVf = 0;    /**< QVideoFrame(QImage) 无效 */
  int slowPresent = 0;  /**< 单帧槽耗时 ≥ CLIENT_VIDEO_SLOW_PRESENT_US（默认 8000µs） */
  int maxPending = 0;   /**< 周期内 m_framesPendingInQueue 峰值 */
  qint64 maxHandlerUs = 0; /**< 周期内单帧槽最大耗时（微秒） */
  /** flushCoalescedVideoPresent 因 CLIENT_VIDEO_MAX_PRESENT_FPS 未到间隔而跳过（与 dE−n 对照） */
  int skippedPresentRateLimit = 0;

  /** ★ QueuedConnection 事件队列延迟（ms）：decode 线程 emit→主线程 handler 执行的排队等待时间。
   * 峰值高（>50ms）→ 主线程事件循环阻塞；结合 SOFTWARE_GL 环境典型表现。*/
  int64_t maxQueuedLagMs = 0;
  int64_t avgQueuedLagMs = 0; /**< 本周期平均 QueuedConnection 延迟（ms） */

  /** ★ Coalescing 丢帧数：onVideoFrameFromDecoder 收到帧但被后续帧覆盖而未呈现 */
  int coalescedDrops = 0;

  /** 主线程 onVideoFrameFromDecoder 槽每秒进入次数（与 dE 对照：dE≫vse 则 Queued 堆积未执行） */
  int videoSlotEntries = 0;
  /** 合帧路径下 flushCoalescedVideoPresent 每秒实际调用 presentDecodedFrameToOutputs 次数 */
  int flushCoalescedCount = 0;
};

#if defined(ENABLE_WEBRTC_LIBDATACHANNEL) && defined(ENABLE_FFMPEG)
class H264Decoder;
class QThread;
class RtpPacketSpscQueue;
class RtpStreamClockContext;
class VideoFramePresentWorker;
#endif

#include "ui/RemoteVideoSurface.h"

#if defined(ENABLE_WEBRTC_LIBDATACHANNEL) && defined(ENABLE_FFMPEG)
#include "media/DmaBufFrameHandle.h"
#endif

#ifdef ENABLE_WEBRTC_LIBDATACHANNEL
#include <rtc/rtc.hpp>
#endif

/**
 * @brief WebRTC 客户端类
 * 负责从 ZLMediaKit 接收 WebRTC 视频流；解码后经 QVideoSink（VideoOutput）或
 * RemoteVideoSurface（Scene Graph）呈现。
 * 呈现后端互斥：WebRtcPresentBackend 状态机保证 bindVideoOutput 与 bindVideoSurface 不同时绑定存活对象
 * （见 syncPresentBackendStateMachine / computePresentBackend）。
 */
class WebRtcClient : public QObject {
  Q_OBJECT
  QML_ELEMENT
  Q_PROPERTY(QString streamUrl READ streamUrl WRITE setStreamUrl NOTIFY streamUrlChanged)
  Q_PROPERTY(bool isConnected READ isConnected NOTIFY connectionStatusChanged)
  Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
  /**
   * 当前用于 setVideoFrame 的 QVideoSink：优先为 QML VideoOutput 内置 sink（bindVideoOutput），
   * 否则为内部占位 sink。Qt 6 中 VideoOutput.videoSink 在 QML 侧只读，不可再赋值绑定。
   */
  Q_PROPERTY(QVideoSink *videoSink READ videoSink NOTIFY videoSinkChanged)

  Q_PROPERTY(bool videoFrozen READ videoFrozen NOTIFY videoFrozenChanged)

 public:
  explicit WebRtcClient(QObject *parent = nullptr);
  ~WebRtcClient();

  QString streamUrl() const { return m_streamUrl; }
  void setStreamUrl(const QString &url);

  bool isConnected() const { return m_isConnected; }
  QString statusText() const { return m_statusText; }

  QVideoSink *videoSink() const;

  /** 将解码帧输出到指定 QML VideoOutput（读取其只读 videoSink）；传 null 则回退内部 sink */
  Q_INVOKABLE void bindVideoOutput(QObject *videoOutputItem);
  /** 将解码帧输出到 QML RemoteVideoSurface（Scene Graph 纹理路径）；与 bindVideoOutput 互斥 */
  Q_INVOKABLE void bindVideoSurface(QObject *remoteVideoSurfaceItem);
  /** 诊断：主线程 onVideoFrameFromDecoder 尚未执行完的次数（QueuedConnection 堆积风险） */
  Q_INVOKABLE int pendingVideoHandlerDepth() const { return m_framesPendingInQueue; }
  /** 诊断：解码线程 RTP 队列深度（无 FFmpeg/WebRTC 编译时恒为 0） */
  Q_INVOKABLE int rtpDecodeQueueDepth() const;
  /** 诊断：bindVideoOutput 在本连接周期内累计调用次数（新连接时会被 prepareForNewConnection 清零）
   */
  Q_INVOKABLE int bindVideoOutputInvocationCount() const { return m_bindVideoOutputCallCount; }
  /** 诊断：bindVideoOutput 进程内累计次数（永不因重连清零，用于判断 QML 是否反复换绑） */
  Q_INVOKABLE int bindVideoOutputLifetimeInvocationCount() const {
    return m_bindVideoOutputLifetimeCallCount;
  }
  Q_INVOKABLE int bindVideoSurfaceInvocationCount() const { return m_bindVideoSurfaceCallCount; }
  Q_INVOKABLE int bindVideoSurfaceLifetimeInvocationCount() const {
    return m_bindVideoSurfaceLifetimeCallCount;
  }

  /** 当前呈现后端（由绑定指针派生；每帧可能与快照比对以检测 QML 销毁导致的漂移） */
  Q_INVOKABLE int presentBackend() const { return static_cast<int>(computePresentBackend()); }

  /** 诊断：已呈现帧像素尺寸（RemoteVideoSurface 或 QVideoSink::videoSize），无效则空 */
  Q_INVOKABLE QSize diagnosticPresentSize() const;

  /** 诊断：取回并清零上一秒累积的呈现统计（仅主线程调用；无 FFmpeg/WebRTC 时恒为零） */
  WebRtcPresentSecondStats drainPresentSecondStats();

  /**
   * 与 drainPresentSecondStats 在同一 1Hz tick、同路先后调用：解码线程 emit frameReady
   * 次数（取后清零）。 返回 -1 表示无解码器；dE 与 stats.framesToSink 应对齐，dE−n
   * 大说明主线程排队或槽内早退。
   */
  Q_INVOKABLE int drainDecoderFrameReadyEmitDiagCount();

  /**
   * 与 drainPresentSecondStats 同 1Hz tick：自上次调用以来 RTP 入环 tryPush 失败次数（环满/预算满）。
   * 与 [H264][Stats] holeTotal 对照：iDrop 大→客户端丢；iDrop≈0 且 holeTotal 大→UDP/NACK 链问题。
   */
  Q_INVOKABLE int drainIngressTryPushFailDiagCount();

  /**
   * 跨路媒体预算槽位 0..3（由 WebRtcStreamManager 设置，对应 ClientMediaBudget）。
   * 未设置或非 FFmpeg 编译时内部忽略。
   */
  void setMediaBudgetSlot(int slot);
  int mediaBudgetSlot() const { return m_mediaBudgetSlot; }

 public slots:
  void connectToStream(const QString &serverUrl, const QString &app, const QString &stream);
  void disconnect();
  /** Peer 已连且 DataChannel open；控制面选路须用它而非仅 isConnected() */
  Q_INVOKABLE bool isDataChannelOpen() const;
  /**
   * V2 门闸：DC 可用则发送并返回 true；否则返回 false（不发、不重复刷 warning，供热路径轮询）。
   * sendDataChannelMessage() 仍可对「必须尝试 void API」的调用方做节流告警。
   */
  Q_INVOKABLE bool trySendDataChannelMessage(const QByteArray &data);
  void sendDataChannelMessage(const QByteArray &data);
  /** ★ WHY5 修复：发送 RTCP 包（如 NACK、PLI） */
  void sendRtcp(const QByteArray &data);

  /** 诊断：返回 videoFrameReady 信号的接收者数量 */
  int receiverCountVideoFrameReady() const;
  QString videoFrameReadySignalMeta() const;

  /** 历史兼容：自研 VideoRenderer 已移除；保留空操作避免 QML/管理器调用崩溃 */
  Q_INVOKABLE void forceRefresh();

  bool videoFrozen() const { return m_videoFrozen; }

 signals:
  void videoSinkChanged();
  void streamUrlChanged(const QString &url);
  void connectionStatusChanged(bool connected);
  /** libdatachannel：控制用 DataChannel 打开/关闭（供 MqttController 选路与 UI 提示） */
  void dataChannelOpenChanged(bool open);
  void statusTextChanged(const QString &text);
  /** 解码后已呈现到 videoSink 的元信息（占位 UI / 诊断）；不传 QImage，避免每帧 C++→QML 大对象封送
   */
  void videoFrameReady(int frameWidth, int frameHeight, quint64 frameId);
  void errorOccurred(const QString &error);
  /**
   * disconnectWaveId 时间窗内（与 PeerConnection 状态回调一致）请求 WebRtcStreamManager 拉取 ZLM
   * getMediaList 快照。 peerStateEnum：libdatachannel PeerConnection::State
   * 整型（3=Disconnected,4=Failed,5=Closed）。
   */
  void zlmSnapshotRequested(int disconnectWaveId, const QString &stream, int peerStateEnum);
  /** 与 DataChannel `client_video_encoder_hint` 同内容；MqttController 可再发布至 MQTT 形成车端闭环 */
  void clientEncoderHintSent(const QJsonObject &jsonPayload);
  /** DMA-BUF SceneGraph 失败切 CPU 等呈现降级；供 NetworkQualityAggregator 拉低综合分（V1 FSM 绑定） */
  void videoPresentationDegraded(const QString &stream, const QString &reason);
  void videoFrozenChanged(bool frozen);

 private slots:
  void onSdpAnswerReceived(QNetworkReply *reply);
  void onVideoFrameFromDecoder(const QImage &image, quint64 frameId);
  void onRemoteSurfaceDmaBufSceneGraphFailed(const QString& reason);
#if defined(ENABLE_WEBRTC_LIBDATACHANNEL) && defined(ENABLE_FFMPEG)
  void onIngressDrainFinished(bool morePending);
  void onDecoderKeyframeSuggested();
  void onDecoderDecodeIntegrityAlert(const QString &code, const QString &detail,
                                     bool mitigationApplied, const QString &healthContractLine);
  void onPresentWorkerDeliveredFrame(QImage image, quint64 frameId);
  void onDecoderDmaBufFrameReady(SharedDmaBufFrame handle, quint64 frameId);
  /** 连接就绪后主动请求 H.264 单 slice，降低多路条状花屏概率（DataChannel 未开则短延迟重试） */
  void trySendProactiveEncoderDisplayContractHint();
#endif

 private:
  void createOffer();
  void doConnect();
  void sendOfferToServer(const QString &offer);
  void processAnswer(const QString &answer);
  void updateStatus(const QString &status, bool connected = false);
  void teardownMediaPipeline();
  void prepareForNewConnection();
  void scheduleAutoReconnectIfNeeded(const char *reason, int disconnectWaveId = -1);
  static QString ensureSdpHasMid(const QString &sdp);
  static QString ensureSdpBundleGroup(const QString &sdp);
  static QString injectRecvonlyAudioVideoIfSingleMedia(const QString &sdp);
  static QString buildMinimalPlayOfferSdp();

#if defined(ENABLE_WEBRTC_LIBDATACHANNEL) && defined(ENABLE_FFMPEG)
  void scheduleIngressDrainFromAnyThread();
  void requestKeyframeFromSender(const char *reason);
  /** 解码线程 frameReady 合并后主线程统一 setVideoFrame；与 CLIENT_VIDEO_MAX_PRESENT_FPS 配合限频
   */
  void flushCoalescedVideoPresent();
  /** 单次把已解码帧送到 QVideoSink 并 emit videoFrameReady(宽,高,id)（假定 image 非空） */
  void presentDecodedFrameToOutputs(const QImage &image, quint64 frameId);
  void startPresentPipeline();
  void stopPresentPipeline();
  void checkVideoIntegrity();
#endif

  /** 由 m_boundOutputSink / m_boundRemoteSurface 派生；二者不得同时非空 */
  WebRtcPresentBackend computePresentBackend() const;
  /** 绑定或呈现前调用：互斥断言、状态迁移日志、与 m_presentBackendSnapshot 同步 */
  void syncPresentBackendStateMachine(const char *ctx);

  int m_retryCount = 0;
  bool m_offerSent = false;
  int m_reconnectCount = 0;
  int m_videoFrameLogCount = 0;
  int m_framesPendingInQueue = 0;
  int m_bindVideoOutputCallCount = 0;
  int m_bindVideoOutputLifetimeCallCount = 0;
  int m_bindVideoSurfaceCallCount = 0;
  int m_bindVideoSurfaceLifetimeCallCount = 0;
  QSize m_lastPresentedImageSize;
  int64_t m_lastHandlerDoneTime = 0;
  bool m_manualDisconnect = false;
  bool m_connecting = false;
  bool m_reconnectScheduled = false;
  QTimer *m_reconnectTimer = nullptr;
  int64_t m_connectStartTime = 0;
  int64_t m_offerSentTime = 0;
  int64_t m_answerReceivedTime = 0;
  int64_t m_trackReceivedTime = 0;
  int64_t m_lastFrameTime = 0;
  /** 上一帧 setVideoFrame 完成后的墙钟 ms，用于 CLIENT_VIDEO_PRESENT_TRACE 计算帧间隔 */
  int64_t m_lastPresentWallMs = 0;
  int64_t m_lastRtpPacketTime = 0;
  int32_t m_lastFrameRtpTs = 0;
  /** RTP/解码侧与呈现侧可能并发访问，用 atomic */
  std::atomic<int> m_framesSinceLastStats{0};
  int m_presentSecFrames = 0;
  int m_presentSecNullSink = 0;
  int m_presentSecInvalidVf = 0;
  int m_presentSecSlow = 0;
  int m_presentSecMaxPending = 0;
  qint64 m_presentSecMaxHandlerUs = 0;
  int m_presentSecSkippedRateLimit = 0;

  /** ★ 事件队列延迟诊断（QueuedConnection lag）
   * 在 onVideoFrameFromDecoder 进入时计算当前墙钟与 H264Decoder::lastFrameReadyEmitWallMs()
   * 的差值。 单位 ms；峰值在 1Hz 汇报后清零。
   */
  int64_t m_presentSecMaxQueuedLagMs = 0;
  int64_t m_presentSecTotalQueuedLagMs = 0;
  int m_presentSecQueuedLagSamples = 0;

  /** ★ Coalescing 丢帧计数：onVideoFrameFromDecoder 收到帧但被后来帧覆盖而未呈现 */
  int m_presentSecCoalescedDrops = 0;
  int m_presentSecVideoSlotEntries = 0;
  int m_presentSecFlushCoalescedCount = 0;
  QString m_streamUrl;
  QString m_serverUrl;
  QString m_app;
  QString m_stream;
  bool m_isConnected = false;
  QString m_statusText = QStringLiteral("未连接");
  /** ClientMediaBudget 槽位；-1 表示未参与跨路预算 */
  int m_mediaBudgetSlot = -1;

  QNetworkAccessManager *m_networkManager = nullptr;
  QNetworkReply *m_currentReply = nullptr;

  QString m_localSdp;
  QString m_remoteSdp;

#ifdef ENABLE_WEBRTC_LIBDATACHANNEL
  std::shared_ptr<rtc::PeerConnection> m_peerConnection;
  std::shared_ptr<rtc::DataChannel> m_dataChannel;
  /** sendDataChannelMessage 在 DC 未开时 qWarning 节流（防热路径刷屏） */
  int m_dataChannelBlockedSendWarnCount = 0;
  std::shared_ptr<rtc::Track> m_videoTrack;
  std::shared_ptr<rtc::Track> m_audioTrack;
  void setupVideoDecoder();
#endif
#if defined(ENABLE_WEBRTC_LIBDATACHANNEL) && defined(ENABLE_FFMPEG)
  H264Decoder *m_h264Decoder = nullptr;
  /** 每路专用解码线程；RTP→FFmpeg 仅在此线程执行，主线程只做 QVideoSink::setVideoFrame */
  QThread *m_decodeThread = nullptr;
  std::atomic<bool> m_decodePipelineActive{false};
  /** 媒体线程 → 解码线程：有界 SPSC 包队列 + 跨路字节预算（替代每包 QueuedConnection） */
  std::unique_ptr<RtpPacketSpscQueue> m_rtpIngressRing;
  /** 合并调度：仅投递一次 drainRtpIngressQueue，避免 Qt 事件队列被 per-packet lambda 撑爆 */
  std::atomic<bool> m_ingressDrainPosted{false};
  /** tryPush 失败累计（由 drainIngressTryPushFailDiagCount 按秒取走清零） */
  std::atomic<int> m_ingressTryPushFailSinceLastDrain{0};
  qint64 m_lastTrackKeyframeRequestMs = 0;
  /** RTCP SR → RTP 时钟；与 H264Decoder 共享（仅解码线程读原子） */
  std::unique_ptr<RtpStreamClockContext> m_rtpClock;
  std::atomic<quint32> m_rtpVideoSsrc{0};
#endif
  QVideoSink *m_ownedSink = nullptr;
  /** VideoOutput 提供的 sink，随 QML 项销毁自动失效 */
  QPointer<QVideoSink> m_boundOutputSink;
  QPointer<RemoteVideoSurface> m_boundRemoteSurface;
  /** 上次已日志确认的呈现后端；与 computePresentBackend() 比对以检测 QPointer 失效等漂移 */
  WebRtcPresentBackend m_presentBackendSnapshot = WebRtcPresentBackend::InternalPlaceholderSink;

  QVideoSink *activeSink() const;

#if defined(ENABLE_WEBRTC_LIBDATACHANNEL) && defined(ENABLE_FFMPEG)
  QImage m_coalescedPresentImage;
  quint64 m_coalescedPresentFrameId = 0;
  std::atomic<bool> m_coalesceFlushQueued{false};
  std::atomic<quint64> m_coalescedPresentEpoch{0};
  QTimer *m_presentRateTimer = nullptr;
  VideoFramePresentWorker *m_presentWorker = nullptr;
  QThread *m_presentThread = nullptr;
  bool m_proactiveEncoderHintSent = false;
  int m_proactiveEncoderHintDcOpenRetries = 0;
#endif
  QTimer *m_integrityTimer = nullptr;
  int64_t m_lastFrameWallMs = 0;
  bool m_videoFrozen = false;
};

#endif  // WEBRTCCLIENT_H
