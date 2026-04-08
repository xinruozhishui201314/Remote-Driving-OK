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
#include <QMetaMethod>
#include <QPointer>
#include <QVideoSink>
#include <memory>

#if defined(ENABLE_WEBRTC_LIBDATACHANNEL) && defined(ENABLE_FFMPEG)
class H264Decoder;
#endif

#ifdef ENABLE_WEBRTC_LIBDATACHANNEL
#include <rtc/rtc.hpp>
#endif

/**
 * @brief WebRTC 客户端类
 * 负责从 ZLMediaKit 接收 WebRTC 视频流；解码后通过 QVideoSink 供 QML VideoOutput 显示。
 */
class WebRtcClient : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString streamUrl READ streamUrl WRITE setStreamUrl NOTIFY streamUrlChanged)
    Q_PROPERTY(bool isConnected READ isConnected NOTIFY connectionStatusChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    /**
     * 当前用于 setVideoFrame 的 QVideoSink：优先为 QML VideoOutput 内置 sink（bindVideoOutput），
     * 否则为内部占位 sink。Qt 6 中 VideoOutput.videoSink 在 QML 侧只读，不可再赋值绑定。
     */
    Q_PROPERTY(QVideoSink* videoSink READ videoSink NOTIFY videoSinkChanged)

public:
    explicit WebRtcClient(QObject *parent = nullptr);
    ~WebRtcClient();

    QString streamUrl() const { return m_streamUrl; }
    void setStreamUrl(const QString &url);

    bool isConnected() const { return m_isConnected; }
    QString statusText() const { return m_statusText; }

    QVideoSink* videoSink() const;

    /** 将解码帧输出到指定 QML VideoOutput（读取其只读 videoSink）；传 null 则回退内部 sink */
    Q_INVOKABLE void bindVideoOutput(QObject *videoOutputItem);

public slots:
    void connectToStream(const QString &serverUrl, const QString &app, const QString &stream);
    void disconnect();
    void sendDataChannelMessage(const QByteArray &data);

    /** 诊断：返回 videoFrameReady 信号的接收者数量 */
    int receiverCountVideoFrameReady() const;
    QString videoFrameReadySignalMeta() const;

    /** 历史兼容：自研 VideoRenderer 已移除；保留空操作避免 QML/管理器调用崩溃 */
    Q_INVOKABLE void forceRefresh();

signals:
    void videoSinkChanged();
    void streamUrlChanged(const QString &url);
    void connectionStatusChanged(bool connected);
    void statusTextChanged(const QString &text);
    /** 解码后的视频帧元信息（占位 UI / 诊断）；画面由 C++ push 到 videoSink */
    void videoFrameReady(const QImage &image, int frameWidth, int frameHeight, quint64 frameId);
    void errorOccurred(const QString &error);

private slots:
    void onSdpAnswerReceived(QNetworkReply *reply);
    void onVideoFrameFromDecoder(const QImage &image, quint64 frameId);

private:
    void createOffer();
    void doConnect();
    void sendOfferToServer(const QString &offer);
    void processAnswer(const QString &answer);
    void updateStatus(const QString &status, bool connected = false);
    void teardownMediaPipeline();
    void prepareForNewConnection();
    void scheduleAutoReconnectIfNeeded(const char *reason);
    static QString ensureSdpHasMid(const QString &sdp);
    static QString ensureSdpBundleGroup(const QString &sdp);
    static QString injectRecvonlyAudioVideoIfSingleMedia(const QString &sdp);
    static QString buildMinimalPlayOfferSdp();

    int m_retryCount = 0;
    bool m_offerSent = false;
    int m_reconnectCount = 0;
    int m_videoFrameLogCount = 0;
    int m_framesPendingInQueue = 0;
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
    int64_t m_lastRtpPacketTime = 0;
    int32_t m_lastFrameRtpTs = 0;
    int m_framesSinceLastStats = 0;
    QString m_streamUrl;
    QString m_serverUrl;
    QString m_app;
    QString m_stream;
    bool m_isConnected = false;
    QString m_statusText = QStringLiteral("未连接");

    QNetworkAccessManager *m_networkManager = nullptr;
    QNetworkReply *m_currentReply = nullptr;

    QString m_localSdp;
    QString m_remoteSdp;

#ifdef ENABLE_WEBRTC_LIBDATACHANNEL
    std::shared_ptr<rtc::PeerConnection> m_peerConnection;
    std::shared_ptr<rtc::DataChannel> m_dataChannel;
    std::shared_ptr<rtc::Track> m_videoTrack;
    std::shared_ptr<rtc::Track> m_audioTrack;
    void setupVideoDecoder();
#endif
#if defined(ENABLE_WEBRTC_LIBDATACHANNEL) && defined(ENABLE_FFMPEG)
    H264Decoder *m_h264Decoder = nullptr;
#endif
    QVideoSink *m_ownedSink = nullptr;
    /** VideoOutput 提供的 sink，随 QML 项销毁自动失效 */
    QPointer<QVideoSink> m_boundOutputSink;

    QVideoSink *activeSink() const;
};

#endif // WEBRTCCLIENT_H
