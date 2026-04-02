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

signals:
    void streamUrlChanged(const QString &url);
    void connectionStatusChanged(bool connected);
    void statusTextChanged(const QString &text);
    /** 解码后的视频帧（RTP→H.264 解码→QImage），供 VideoRenderer 显示 */
    void videoFrameReady(const QImage &image);
    /** 带显式尺寸的版本：解决 QImage 跨线程 QueuedConnection 到 QML 时，
     *  QVariant 包装导致 image.width/height 方法调用返回 0/undefined 的 Qt 元对象边界问题。
     *  QML 应优先使用此信号，image 参数仅作 setFrame 数据源。
     *  第四参数 frameId 用于 C++ emit → QML handler 端到端追踪。 */
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
};

#endif // WEBRTCCLIENT_H
