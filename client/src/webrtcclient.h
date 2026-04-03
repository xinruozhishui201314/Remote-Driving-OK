#ifndef WEBRTCCLIENT_H
#define WEBRTCCLIENT_H

#include <QObject>
#include <QString>
#include <QUrl>
#include <QImage>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonObject>
#include <QJsonDocument>
#include <QTimer>
#include <QPointer>
#include <QMetaMethod>
#include <QQuickItem>
#include <memory>

#if defined(ENABLE_WEBRTC_LIBDATACHANNEL) && defined(ENABLE_FFMPEG)
class H264Decoder;
#endif

#ifdef ENABLE_WEBRTC_LIBDATACHANNEL
#include <rtc/rtc.hpp>
#endif

/**
 * @brief WebRTC 客户端类
 * 负责从 ZLMediaKit 接收 WebRTC 视频流
 */
class WebRtcClient : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString streamUrl READ streamUrl WRITE setStreamUrl NOTIFY streamUrlChanged)
    Q_PROPERTY(bool isConnected READ isConnected NOTIFY connectionStatusChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    /** 直接渲染路径：Q_PROPERTY 绑定使 QML 可直接赋值（不依赖 Q_INVOKABLE 方法可见性） */
    Q_PROPERTY(QObject* videoRenderer READ videoRenderer WRITE setVideoRendererQt NOTIFY videoRendererChanged)

public:
    explicit WebRtcClient(QObject *parent = nullptr);
    ~WebRtcClient();

    QString streamUrl() const { return m_streamUrl; }
    void setStreamUrl(const QString &url);
    
    bool isConnected() const { return m_isConnected; }
    QString statusText() const { return m_statusText; }

public slots:
    void connectToStream(const QString &serverUrl, const QString &app, const QString &stream);
    void disconnect();
    void sendDataChannelMessage(const QByteArray &data);

    /**
     * 注册 VideoRenderer 以启用直接渲染路径。
     * 推荐通过 Q_PROPERTY videoRenderer 绑定替代（QML 可直接赋值）。
     * @deprecated 推荐在 QML 中直接写：streamClient.videoRenderer = videoRenderer
     */
    Q_INVOKABLE void setVideoRenderer(QObject* renderer, const QString& streamName = QString());

    /** videoRenderer Q_PROPERTY READ accessor（供 QML Q_PROPERTY 绑定使用） */
    QObject* videoRenderer() const { return m_videoRenderer.data(); }
    /** videoRenderer Q_PROPERTY WRITE accessor（QML 可直接赋值，无需方法调用可见性） */
    void setVideoRendererQt(QObject* renderer) { setVideoRenderer(renderer, m_rendererStreamName); }

    /** 诊断：返回 videoFrameReady(const QImage&, int, int, quint64) 信号的接收者数量（信号已消除 overload，4 参数版本为唯一版本） */
    int receiverCountVideoFrameReady() const;
    /** 诊断：返回 videoFrameReady 信号的元数据（方法索引、参数数量、参数类型名），
     *  用于比对 QML Connections 是否按正确签名建立了连接。
     *  返回格式："index=<idx> params=<N> sig=<完整信号签名>" */
    QString videoFrameReadySignalMeta() const;

    // ── 强制刷新机制（方案1）─────────────────────────────────────────────
    // 根因：Qt Scene Graph 在 VehicleSelectionDialog 显示期间可能阻塞渲染线程，
    // 导致 deliverFrame 收到帧但 updatePaintNode 不被调用。
    // 修复：在对话框关闭时强制刷新所有 VideoRenderer。
    /** Q_INVOKABLE：供 WebRtcStreamManager::forceRefreshAllRenderers 调用，触发关联的 VideoRenderer 强制刷新 */
    Q_INVOKABLE void forceRefresh();

signals:
    void streamUrlChanged(const QString &url);
    void connectionStatusChanged(bool connected);
    void statusTextChanged(const QString &text);
    /** videoRenderer Q_PROPERTY 变更通知（QML 绑定响应式更新） */
    void videoRendererChanged(QObject* renderer);
    /** 解码后的视频帧（RTP→H.264 解码→QImage），供 VideoRenderer 显示。
     *  4 参数版本（唯一版本）：显式宽高 + frameId。
     *  显式宽高解决 QImage 跨线程 QueuedConnection 到 QML 时，QVariant 包装导致
     *  image.width/height 方法调用返回 0/undefined 的 Qt 元对象边界问题。
     *  frameId 用于 C++ emit → QML handler 端到端追踪。
     *  【消除 overload】：原 1 参数版本 videoFrameReady(const QImage&) 已移除，
     *  避免 QML Connections 信号匹配歧义。 */
    void videoFrameReady(const QImage &image, int frameWidth, int frameHeight, quint64 frameId);
    void errorOccurred(const QString &error);

private slots:
    void onSdpAnswerReceived(QNetworkReply *reply);
    void onVideoFrameFromDecoder(const QImage &image, quint64 frameId);

private:
    void createOffer();
    void doConnect();  // 使用已设置的 m_serverUrl/m_app/m_stream 发起连接（含重试时调用）
    void sendOfferToServer(const QString &offer);
    void processAnswer(const QString &answer);
    void updateStatus(const QString &status, bool connected = false);
    /** 释放解码器/Track/PC/DataChannel（不碰 HTTP reply、不标记 manualDisconnect） */
    void teardownMediaPipeline();
    /** 新会话前清理：停重连定时器、abort reply、teardownMediaPipeline */
    void prepareForNewConnection();
    /** Failed/Closed 时排一次自动重连（带去重） */
    void scheduleAutoReconnectIfNeeded(const char *reason);
    /** 确保 SDP 中每个 m= 段都有 a=mid，满足 ZLMediaKit checkValid(!mid.empty()) */
    static QString ensureSdpHasMid(const QString &sdp);
    /** 确保 a=group:BUNDLE 存在且 mids 数量 <= media 数量，满足 ZLM「只支持 group BUNDLE 模式」*/
    static QString ensureSdpBundleGroup(const QString &sdp);
    /** 当 SDP 仅 1 个 m= 时注入 m=audio + m=video（mid 1/2），并设 a=group:BUNDLE 0 1 2，供 ZLM play 校验 */
    static QString injectRecvonlyAudioVideoIfSingleMedia(const QString &sdp);
    /** 生成 ZLM play 用最小 Offer（m=audio + m=video, recvonly），满足 direction/type 校验 */
    static QString buildMinimalPlayOfferSdp();

    int m_retryCount = 0;  // -400 stream not found 时重试计数，最多重试 12 次（车端推流约 5~25s 就绪）
    bool m_offerSent = false;  // 每个连接周期只发送一次 Offer，避免 setRemoteDescription 触发的二次 onLocalDescription 再 POST 导致 ZLM -400
    int m_reconnectCount = 0;  // 连接断开后自动重连计数，最多重试 5 次
    /** 每路连接周期内已转发到 QML 的帧数（用于 [Client][VideoFrame] 前 N 帧与节流日志） */
    int m_videoFrameLogCount = 0;
    /** 在事件队列中等待处理的帧数（进入 onVideoFrameFromDecoder 时 +1，退出时 -1）。
     *  >1 说明主线程被阻塞，事件积压，导致视频卡顿。 */
    int m_framesPendingInQueue = 0;
    /** 上一帧处理完成的墙上时间（毫秒），用于计算 emit→handler 端到端延迟。 */
    int64_t m_lastHandlerDoneTime = 0;
    bool m_manualDisconnect = false;  // 是否手动断开连接（手动断开时不自动重连）
    /**
     * 避免主动连接进行中（disconnectAll + connectToStream 同帧执行）时，
     * 旧 PeerConnection 的 onStateChange(Closed) 被当作「被动断连」触发定时器重连。
     * 在 doConnect 成功后（onTrack/onSdpAnswer）立即重置。
     */
    bool m_connecting = false;
    /** 避免 Disconnected+Closed 连续回调导致同一断链排队两次重连（会打爆 ZLM / 抖动） */
    bool m_reconnectScheduled = false;
    QTimer *m_reconnectTimer = nullptr;  // 自动重连定时器（用于取消）
    // ── 诊断日志增强 ────────────────────────────────────────────────────────────
    int64_t m_connectStartTime = 0;       // connectToStream() 调用时刻（毫秒）
    int64_t m_offerSentTime = 0;         // SDP Offer 发送时刻
    int64_t m_answerReceivedTime = 0;   // SDP Answer 收到时刻
    int64_t m_trackReceivedTime = 0;     // onTrack(video) 收到时刻
    int64_t m_lastFrameTime = 0;         // 上次收到帧的墙上时间（毫秒）
    int64_t m_lastRtpPacketTime = 0;     // 上次 RTP 包到达 libdatachannel 工作线程的墙上时间（毫秒）
    int32_t m_lastFrameRtpTs = 0;        // 上次收到帧的 RTP timestamp
    int m_framesSinceLastStats = 0;      // 上次统计后的帧数（用于断开时诊断）
    // ── 诊断日志增强结束 ───────────────────────────────────────────────────────
    QString m_streamUrl;
    QString m_serverUrl;
    QString m_app;
    QString m_stream;
    bool m_isConnected = false;
    QString m_statusText = "未连接";
    
    QNetworkAccessManager *m_networkManager = nullptr;
    QNetworkReply *m_currentReply = nullptr;
    
    // WebRTC 相关
    QString m_localSdp;
    QString m_remoteSdp;
    
#ifdef ENABLE_WEBRTC_LIBDATACHANNEL
    std::shared_ptr<rtc::PeerConnection> m_peerConnection;
    std::shared_ptr<rtc::DataChannel> m_dataChannel;
    std::shared_ptr<rtc::Track> m_videoTrack;
    std::shared_ptr<rtc::Track> m_audioTrack;
    /** 在主线程中创建解码器并绑定 track.onMessage，由 onTrack 通过 QTimer::singleShot 调用 */
    void setupVideoDecoder();
#endif
#if defined(ENABLE_WEBRTC_LIBDATACHANNEL) && defined(ENABLE_FFMPEG)
    H264Decoder *m_h264Decoder = nullptr;
#endif
    /** ★★★ 核心修复：直接渲染器引用（绕过 QML Connections 信号层）★★★ */
    QPointer<QQuickItem> m_videoRenderer;
    /** m_videoRenderer 的流名称（cam_front/rear/left/right），用于日志追踪 */
    QString m_rendererStreamName;
};

#endif // WEBRTCCLIENT_H
