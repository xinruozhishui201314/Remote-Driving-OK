#include "webrtcclient.h"
#include "presentation/renderers/VideoRenderer.h"
#include <QNetworkRequest>
#include <QUrlQuery>
#include <QDebug>
#include <QUrl>
#include <QTimer>
#include <QElapsedTimer>
#include <QDateTime>
#include <QRandomGenerator>
#include <QPointer>
#include <QMetaMethod>
#if defined(ENABLE_WEBRTC_LIBDATACHANNEL) && defined(ENABLE_FFMPEG)
#include "h264decoder.h"
#endif

#ifdef ENABLE_WEBRTC_LIBDATACHANNEL
#include <rtc/rtc.hpp>
#endif

/**
 * ═══════════════════════════════════════════════════════════════════════════════════
 * WebRTC 客户端 — 四路视频流完整链路（第4步详解）
 * ═══════════════════════════════════════════════════════════════════════════════════
 *
 * 【Step 4: WebRtcClient 接收解码帧】
 *
 *  调用链：
 *    H264Decoder::emitDecodedFrames()
 *      → emit frameReady(QImage, frameId)  [QueuedConnection]
 *      → WebRtcClient::onVideoFrameFromDecoder(QImage, frameId)
 *
 *  onVideoFrameFromDecoder 内部的双路调用：
 *
 *  路径A（C++ 直接调用，推荐）：
 *    → QMetaMethod::invoke(VideoRenderer, Qt::QueuedConnection)
 *      → VideoRenderer::setFrame(QImage, frameId)  [主线程]
 *      → deliverFrame(VideoFrame) → triggerRenderRefresh()
 *      → scheduleRenderJob → RenderThread update() → updatePaintNode()
 *
 *  路径B（QML Signal Fallback）：
 *    → emit videoFrameReady(QImage, w, h, frameId)  [信号]
 *      → QML Connections { target: streamClient }
 *      → onVideoFrameReady(frame, w, h, frameId)
 *      → videoRenderer.setFrame(frame, frameId)  [QML → C++]
 *
 *  关键设计：
 *  - 路径A 使用 QMetaMethod::invoke + QueuedConnection，将 setFrame 调用排队到主线程
 *  - 路径B 作为 fallback，若 VideoRenderer 未注册则走信号层
 *  - 两个路径都调用时，VideoRenderer 会过滤重复帧（m_lastFrameId 检查）
 *
 *  日志链路：
 *  [Client][WebRTC][emitDone] ★★★ 直接调用成功 ★★★ frameId=xxx signalReceivers=0（正常）
 *  [Client][VideoRenderer] ★★★ setFrame(QML→C++) 被调用 ★★★ frameId=xxx image.isNull=xxx
 *  [Client][VideoRenderer] deliverFrame 被调用 seq=xxx frameId=xxx window=0x...
 *  [Client][VideoRenderer][Refresh] ★★★ scheduleRenderJob 已投递 ★★★
 *
 *  诊断关键点：
 *  - 若 [emitDone] directInvokeSuccess=true → 路径A成功
 *  - 若 signalReceivers=0 但 directInvokeSuccess=true → 正常（C++ 直接调用绕过信号层）
 *  - 若 signalReceivers=0 且 directInvokeSuccess=false → FATAL，帧被丢弃
 *  - 若 onVideoFrameFromDecoder 从未出现日志 → H264Decoder → frameReady 链路断
 *  - 若 [RTP-Arrival] 有日志但 [emitDecodedFrames] 没有 → RTP → 解码 链路断
 *  - 若 [RTP-Arrival] 都没有 → WebRTC DataChannel → RTP 链路断
 *
 * ═══════════════════════════════════════════════════════════════════════════════════
 */

WebRtcClient::WebRtcClient(QObject *parent)
    : QObject(parent)
    , m_networkManager(new QNetworkAccessManager(this))
    , m_reconnectTimer(new QTimer(this))
{
    m_reconnectTimer->setSingleShot(true);
    // 默认 5s 重连间隔；可被指数退避覆盖
    m_reconnectTimer->setInterval(5000);
    connect(m_reconnectTimer, &QTimer::timeout, this, [this]() {
        try {
            m_reconnectScheduled = false;
            if (!m_manualDisconnect && !m_stream.isEmpty() && !m_serverUrl.isEmpty()) {
                m_offerSent = false;  // 重置 offer，允许重新 POST
                qInfo() << "[Client][WebRTC] 自动重连定时器触发，执行 doConnect stream=" << m_stream
                        << " reconnectCount=" << m_reconnectCount;
                doConnect();
            } else {
                qDebug() << "[Client][WebRTC] 自动重连定时器触发但已跳过 manual=" << m_manualDisconnect
                         << " streamEmpty=" << m_stream.isEmpty() << " serverEmpty=" << m_serverUrl.isEmpty();
            }
        } catch (const std::exception& e) {
            qCritical() << "[Client][WebRTC][ERROR] 自动重连定时器异常: stream=" << m_stream
                        << " error=" << e.what() << " — 重置标志并标记手动断开以防死循环";
            m_reconnectScheduled = false;
            m_manualDisconnect = true;
            updateStatus("定时器异常，已停止重连", false);
        } catch (...) {
            qCritical() << "[Client][WebRTC][ERROR] 自动重连定时器未知异常: stream=" << m_stream
                        << " — 重置标志并标记手动断开";
            m_reconnectScheduled = false;
            m_manualDisconnect = true;
            updateStatus("定时器异常，已停止重连", false);
        }
    });
}

WebRtcClient::~WebRtcClient()
{
    disconnect();
}

void WebRtcClient::setStreamUrl(const QString &url)
{
    if (m_streamUrl != url) {
        m_streamUrl = url;
        emit streamUrlChanged(m_streamUrl);
    }
}

void WebRtcClient::connectToStream(const QString &serverUrl, const QString &app, const QString &stream)
{
    qInfo() << "[Client][WebRTC] connectToStream 进入 serverUrl=" << serverUrl << " app=" << app
            << " stream=" << stream << " prevStream=" << m_stream << " wasConnected=" << m_isConnected;

    if (m_isConnected) {
        disconnect();
    }
    prepareForNewConnection();

    m_serverUrl = serverUrl;
    m_app = app;
    m_stream = stream;
    m_retryCount = 0;
    m_reconnectCount = 0;
    m_reconnectScheduled = false;
    m_manualDisconnect = false;  // 重置手动断开标志，允许自动重连
    m_connecting = true;       // 标记主动连接进行中，防止旧 PC 关闭回调触发定时器

    // ── 诊断：记录协商开始时间（毫秒）───────────────────────────────────────────
    m_connectStartTime = QDateTime::currentMSecsSinceEpoch();
    doConnect();
}

void WebRtcClient::doConnect()
{
    try {
        m_offerSent = false;
        updateStatus("正在连接...", false);
        qInfo() << "[Client][WebRTC] doConnect stream=" << m_stream << " server=" << m_serverUrl << " app=" << m_app
                << " retry(stream not found)=" << m_retryCount << " reconnect(after drop)=" << m_reconnectCount;
        qDebug() << "[Client][WebRTC] 环节: 发起拉流 stream=" << m_stream << "（若 stream not found 将最多重试 12 次，间隔 3s）";

        if (m_stream.isEmpty()) {
            qWarning() << "[Client][WebRTC][ERROR] doConnect: m_stream 为空，拒绝发起连接";
            updateStatus("流名称为空，无法连接", false);
            emit errorOccurred("流名称为空");
            return;
        }
        if (m_serverUrl.isEmpty()) {
            qWarning() << "[Client][WebRTC][ERROR] doConnect: m_serverUrl 为空，拒绝发起连接";
            updateStatus("服务器地址为空，无法连接", false);
            emit errorOccurred("服务器地址为空");
            return;
        }
        if (!m_networkManager) {
            qCritical() << "[Client][WebRTC][ERROR] doConnect: m_networkManager 为 nullptr，无法发起 HTTP 请求 stream=" << m_stream;
            updateStatus("网络管理器异常，无法连接", false);
            emit errorOccurred("网络管理器为空");
            return;
        }

        // 构建 ZLMediaKit WebRTC API URL
        // 格式: http://<ip>:<port>/index/api/webrtc?app=<app>&stream=<stream>&type=play
        QUrl apiUrl(m_serverUrl + "/index/api/webrtc");
        QUrlQuery query;
        query.addQueryItem("app", m_app);
        query.addQueryItem("stream", m_stream);
        query.addQueryItem("type", "play");
        apiUrl.setQuery(query);

        QString streamUrl = apiUrl.toString();
        setStreamUrl(streamUrl);
        qDebug() << "[Client][WebRTC] 环节: 拉流 URL stream=" << m_stream << " url=" << streamUrl;

        createOffer();
    } catch (const std::exception& e) {
        qCritical() << "[Client][WebRTC][ERROR] doConnect 异常: stream=" << m_stream
                    << " error=" << e.what();
        updateStatus("连接初始化异常: " + QString::fromLatin1(e.what()), false);
        emit errorOccurred("doConnect 异常: " + QString::fromLatin1(e.what()));
        m_connecting = false;
    } catch (...) {
        qCritical() << "[Client][WebRTC][ERROR] doConnect 未知异常: stream=" << m_stream;
        updateStatus("连接初始化异常", false);
        emit errorOccurred("doConnect 未知异常");
        m_connecting = false;
    }
}

QString WebRtcClient::buildMinimalPlayOfferSdp()
{
    // ZLM checkValid 要求：每个媒体段 direction 有效或 type==TrackApplication。
    // 仅 m=application 为 TrackApplication；m=audio/m=video 需有 recvonly/sendrecv 等。
    // 使用最小 play Offer：m=audio + m=video，均为 recvonly，带 a=mid，供 ZLM play 拉流。
    const QString ufrag = QStringLiteral("x") + QString::number(QRandomGenerator::global()->generate(), 16).left(7);
    const QString pwd   = QString::number(QRandomGenerator::global()->generate(), 16) +
                          QString::number(QRandomGenerator::global()->generate(), 16).left(6);
    const QString fingerprint(QStringLiteral("00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00"));
    QString sdp;
    sdp += QStringLiteral("v=0\r\n");
    sdp += QStringLiteral("o=- 0 0 IN IP4 0.0.0.0\r\n");
    sdp += QStringLiteral("s=-\r\n");
    sdp += QStringLiteral("t=0 0\r\n");
    sdp += QStringLiteral("a=group:BUNDLE 0 1\r\n");
    sdp += QStringLiteral("a=msid-semantic: WMS *\r\n");
    sdp += QStringLiteral("a=fingerprint:sha-256 ").append(fingerprint).append(QStringLiteral("\r\n"));
    // m=audio, recvonly
    sdp += QStringLiteral("m=audio 9 UDP/TLS/RTP/SAVPF 0\r\n");
    sdp += QStringLiteral("c=IN IP4 0.0.0.0\r\n");
    sdp += QStringLiteral("a=recvonly\r\n");
    sdp += QStringLiteral("a=mid:0\r\n");
    sdp += QStringLiteral("a=rtcp-mux\r\n");
    sdp += QStringLiteral("a=ice-ufrag:").append(ufrag).append(QStringLiteral("\r\n"));
    sdp += QStringLiteral("a=ice-pwd:").append(pwd).append(QStringLiteral("\r\n"));
    sdp += QStringLiteral("a=fingerprint:sha-256 ").append(fingerprint).append(QStringLiteral("\r\n"));
    sdp += QStringLiteral("a=setup:actpass\r\n");
    sdp += QStringLiteral("a=rtpmap:0 PCMU/8000\r\n");
    // m=video, recvonly
    sdp += QStringLiteral("m=video 9 UDP/TLS/RTP/SAVPF 96\r\n");
    sdp += QStringLiteral("c=IN IP4 0.0.0.0\r\n");
    sdp += QStringLiteral("a=recvonly\r\n");
    sdp += QStringLiteral("a=mid:1\r\n");
    sdp += QStringLiteral("a=rtcp-mux\r\n");
    sdp += QStringLiteral("a=ice-ufrag:").append(ufrag).append(QStringLiteral("\r\n"));
    sdp += QStringLiteral("a=ice-pwd:").append(pwd).append(QStringLiteral("\r\n"));
    sdp += QStringLiteral("a=fingerprint:sha-256 ").append(fingerprint).append(QStringLiteral("\r\n"));
    sdp += QStringLiteral("a=setup:actpass\r\n");
    sdp += QStringLiteral("a=rtpmap:96 H264/90000\r\n");
    sdp += QStringLiteral("a=fmtp:96 packetization-mode=1;profile-level-id=42e01f\r\n");
    return sdp;
}

void WebRtcClient::createOffer()
{
    // ── ★★★ 端到端追踪：createOffer 进入 ★★★ ────────────────────────────────
    const int64_t funcEnterTime = QDateTime::currentMSecsSinceEpoch();
    qInfo() << "[Client][WebRTC][SDP] ★★★ createOffer ENTER ★★★"
            << " stream=" << m_stream
            << " m_peerConnection_existing=" << (bool)m_peerConnection
            << " enterTime=" << funcEnterTime;
#ifdef ENABLE_WEBRTC_LIBDATACHANNEL
    try {
        if (m_peerConnection) {
            qInfo() << "[Client][WebRTC] createOffer: 清理残留 PeerConnection，避免双 PC/ICE 资源争用 stream=" << m_stream;
            teardownMediaPipeline();
        }
        rtc::Configuration config;

        // STUN：仅用 Google 公开 STUN（NTP 同步不可靠时用）
        config.iceServers.emplace_back("stun:stun.l.google.com:19302");

        // TURN（可选）：从环境变量读取，支持 udp/tcp 两种协议
        // 示例：WEBRTC_TURN_URL=turn:user:pass@your-turn-server.com:3478
        const QString turnUrl = qEnvironmentVariable("WEBRTC_TURN_URL");
        if (!turnUrl.isEmpty()) {
            QUrl turnQUrl(turnUrl);
            if (turnQUrl.isValid()) {
                QString username = turnQUrl.userName();
                QString password = turnQUrl.password();
                QString host = turnQUrl.host();
                int port = turnQUrl.port(3478);
                // libdatachannel TURN: turn:user:password@host:port
                QString turnStr = QStringLiteral("turn:%1:%2@%3:%4")
                                      .arg(QString::fromUtf8(QUrl::toPercentEncoding(username)),
                                           QString::fromUtf8(QUrl::toPercentEncoding(password)),
                                           host,
                                           QString::number(port));
                config.iceServers.emplace_back(turnStr.toStdString());
                qInfo() << "[Client][WebRTC] TURN 已配置 url=" << turnUrl;
            } else {
                qWarning() << "[Client][WebRTC] WEBRTC_TURN_URL 无效，跳过 TURN:" << turnUrl;
            }
        }

        qInfo() << "[Client][WebRTC] ICE 配置: STUN=stun.l.google.com:19302 TURN_URL="
                << (turnUrl.isEmpty() ? QStringLiteral("未配置") : turnUrl)
                << " (pingInterval 由 libdatachannel 内部控制)";

        m_peerConnection = std::make_shared<rtc::PeerConnection>(config);
        m_videoFrameLogCount = 0;  // 新 PC/重连后重新打满前 10 帧 [Client][VideoFrame]

        // ★ 诊断：ICE candidate 完整信息（foundation / type / protocol / relay address）
    // 便于追踪断开时走的是 host/srflx/relay 哪条路径
    m_peerConnection->onLocalCandidate([this](rtc::Candidate candidate) {
        try {
            QString cand = QString::fromStdString(candidate.candidate());
            // 解析关键字段：foundation / component / type / protocol
            // 格式: candidate:foundation component transport type protocol ...
            QStringList parts = cand.split(' ');
            QString typeStr, protoStr;
            if (parts.size() >= 9) {
                typeStr = parts[7];  // host / srflx / relay
                protoStr = parts[4];  // UDP / TCP
            }
            qDebug() << "[Client][WebRTC][ICE] LocalCandidate stream=" << m_stream
                     << " type=" << typeStr << " proto=" << protoStr << " cand=" << cand;
        } catch (const std::exception& e) {
            qWarning() << "[Client][WebRTC][ICE][ERROR] onLocalCandidate 异常: stream=" << m_stream
                       << " error=" << e.what();
        } catch (...) {
            qWarning() << "[Client][WebRTC][ICE][ERROR] onLocalCandidate 未知异常: stream=" << m_stream;
        }
    });
        m_peerConnection->onTrack([this](std::shared_ptr<rtc::Track> track) {
            try {
                if (!track) {
                    qWarning() << "[Client][WebRTC][Track][WARN] onTrack 收到空 track: stream=" << m_stream;
                    return;
                }
                std::string kind = track->description().type();
                // ── 诊断：记录 onTrack 时刻 + 完整协商时间链 ─────────────────────
                const int64_t trackTime = QDateTime::currentMSecsSinceEpoch();
                m_trackReceivedTime = trackTime;
                const int64_t connDelay = (m_connectStartTime > 0) ? (trackTime - m_connectStartTime) : -1;
                const int64_t offerDelay = (m_offerSentTime > 0) ? (trackTime - m_offerSentTime) : -1;
                const int64_t answerDelay = (m_answerReceivedTime > 0) ? (trackTime - m_answerReceivedTime) : -1;
                qInfo() << "[Client][WebRTC][SDP] onTrack stream=" << m_stream << " kind=" << QString::fromStdString(kind)
                         << " 协商耗时: total=" << connDelay << "ms offer→Answer=" << offerDelay << "ms Answer→Track=" << answerDelay << "ms";
                qDebug() << "[Client][WebRTC] onTrack stream=" << m_stream << "kind=" << QString::fromStdString(kind);
                if (kind == "video") {
                    m_videoTrack = track;
#if defined(ENABLE_WEBRTC_LIBDATACHANNEL) && defined(ENABLE_FFMPEG)
                    QTimer::singleShot(0, this, &WebRtcClient::setupVideoDecoder);
#endif
                } else if (kind == "audio") {
                    m_audioTrack = track;
                }
            } catch (const std::exception& e) {
                qCritical() << "[Client][WebRTC][Track][ERROR] onTrack 异常: stream=" << m_stream
                            << " error=" << e.what();
            } catch (...) {
                qCritical() << "[Client][WebRTC][Track][ERROR] onTrack 未知异常: stream=" << m_stream;
            }
        });
        m_peerConnection->onStateChange([this](rtc::PeerConnection::State state) {
            try {
                const char *stateStr = "Unknown";
                switch (static_cast<int>(state)) {
                    case 0: stateStr = "New"; break;
                    case 1: stateStr = "Connecting"; break;
                    case 2: stateStr = "Connected"; break;
                    case 3: stateStr = "Disconnected"; break;
                    case 4: stateStr = "Failed"; break;
                    case 5: stateStr = "Closed"; break;
                }
                qInfo() << "[Client][WebRTC] PeerConnection state stream=" << m_stream << " state=" << stateStr
                        << " enum=" << static_cast<int>(state);

                // ★ onStateChange 在 libdatachannel 工作线程：切回主线程再碰 Qt / 定时器
                QTimer::singleShot(0, this, [this, state]() {
                    try {
                        if (state == rtc::PeerConnection::State::Connected) {
                            m_reconnectScheduled = false;
                            m_connecting = false;  // 主动连接完成，后续断连才可触发定时器
                            updateStatus("已连接", true);
                            m_isConnected = true;
                            m_reconnectCount = 0;
                            qInfo() << "[Client][WebRTC] 媒体面已连通 stream=" << m_stream << "（重连计数已清零）";
                            emit connectionStatusChanged(true);
                            return;
                        }

                        // Disconnected：多为 ICE/DTLS 瞬时抖动，随后常跟 Closed；此处只更新 UI/聚合状态，不排队自动重连，避免与 Closed 重复触发
                        if (state == rtc::PeerConnection::State::Disconnected) {
                            // ── 诊断：断开时打印帧接收状态（核心诊断点）────────────────────
                            const int64_t now = QDateTime::currentMSecsSinceEpoch();
                            const int64_t lastFrameAge = (m_lastFrameTime > 0) ? (now - m_lastFrameTime) : -1;
                            const int64_t connDuration = (m_connectStartTime > 0) ? (now - m_connectStartTime) : -1;
                            qWarning() << "[Client][WebRTC][State][Diag] PeerConnection Disconnected stream=" << m_stream
                                       << " 已连接时长=" << connDuration << "ms"
                                       << " 上帧距今=" << lastFrameAge << "ms"
                                       << " 本周期帧数=" << m_videoFrameLogCount
                                       << " framesSinceLast=" << m_framesSinceLastStats
                                       << " 【诊断结论】："
                                       << (m_videoFrameLogCount == 0 ? "ZLM 从未发帧(检查 ZLM 推流)" :
                                           lastFrameAge > 5000 ? "ZLM 已停发帧(检查 carla-bridge→ZLM 链路)" :
                                           "ZLM 正在发帧但 ICE/UDP 抖动(ZLM 或网络问题)");
                            qWarning() << "[Client][WebRTC] PeerConnection Disconnected stream=" << m_stream
                                       << "（不触发自动重连；若永久失败将进入 Failed/Closed 再重连）";
                            qInfo() << "[Client][WebRTC][Diag] stream=" << m_stream
                                    << " UDP/ICE 抖动排障: 宿主执行 ZLM 媒体列表 ./scripts/diag-zlm-streams.sh;"
                                    << "抓包 docker0 UDP 示例: sudo tcpdump -i docker0 -n udp and host zlmediakit";
                            updateStatus("连接不稳定…", false);
                            m_isConnected = false;
                            emit connectionStatusChanged(false);
                            return;
                        }

                        if (state == rtc::PeerConnection::State::Failed ||
                            state == rtc::PeerConnection::State::Closed) {
                            const char *reason = (state == rtc::PeerConnection::State::Failed) ? "Failed" : "Closed";
                            qWarning() << "[Client][WebRTC] PeerConnection 终结 reason=" << reason << " stream=" << m_stream
                                       << " manualDisconnect=" << m_manualDisconnect;
                            updateStatus(QStringLiteral("已断开 (%1)").arg(QLatin1String(reason)), false);
                            m_isConnected = false;
                            emit connectionStatusChanged(false);
                            scheduleAutoReconnectIfNeeded(reason);
                        }
                    } catch (const std::exception& e) {
                        qCritical() << "[Client][WebRTC][ERROR] onStateChange QTimer::singleShot 内异常: stream=" << m_stream
                                    << " state=" << static_cast<int>(state) << " error=" << e.what();
                        updateStatus("状态处理异常", false);
                        m_isConnected = false;
                        emit connectionStatusChanged(false);
                    } catch (...) {
                        qCritical() << "[Client][WebRTC][ERROR] onStateChange QTimer::singleShot 内未知异常: stream=" << m_stream
                                    << " state=" << static_cast<int>(state);
                        updateStatus("状态处理异常", false);
                        m_isConnected = false;
                        emit connectionStatusChanged(false);
                    }
                });
            } catch (const std::exception& e) {
                qCritical() << "[Client][WebRTC][ERROR] onStateChange 回调异常: stream=" << m_stream
                            << " error=" << e.what();
            } catch (...) {
                qCritical() << "[Client][WebRTC][ERROR] onStateChange 回调未知异常: stream=" << m_stream;
            }
        });

        // Play 拉流：库要求至少有一个 DataChannel 或 Track 才生成 Offer；RecvOnly 的 addTrack 不被视为可协商，
        // 故先创建占位 DataChannel，再添加 recvonly 音视频，由 onLocalDescription 发送。
        // onLocalDescription 在 libdatachannel 工作线程调用，QNetworkAccessManager 属于主线程，必须在主线程 post。
        m_peerConnection->onLocalDescription([this](rtc::Description description) {
            try {
                m_localSdp = QString::fromStdString(std::string(description));
                qDebug() << "[Client][WebRTC] play Offer 已生成 stream=" << m_stream << "，排队到主线程发送";
                QTimer::singleShot(0, this, [this]() {
                    try {
                        sendOfferToServer(m_localSdp);
                    } catch (const std::exception& e) {
                        qCritical() << "[Client][WebRTC][ERROR] sendOfferToServer 异常: stream=" << m_stream
                                    << " error=" << e.what();
                        updateStatus("Offer 发送失败: " + QString::fromLatin1(e.what()), false);
                        emit errorOccurred("Offer 发送异常: " + QString::fromLatin1(e.what()));
                    } catch (...) {
                        qCritical() << "[Client][WebRTC][ERROR] sendOfferToServer 未知异常: stream=" << m_stream;
                        updateStatus("Offer 发送异常", false);
                        emit errorOccurred("Offer 发送未知异常");
                    }
                });
            } catch (const std::exception& e) {
                qCritical() << "[Client][WebRTC][ERROR] onLocalDescription 回调异常: stream=" << m_stream
                            << " error=" << e.what();
            } catch (...) {
                qCritical() << "[Client][WebRTC][ERROR] onLocalDescription 回调未知异常: stream=" << m_stream;
            }
        });
        m_dataChannel = m_peerConnection->createDataChannel("control");
        rtc::Description::Video videoMedia("video", rtc::Description::Direction::RecvOnly);
        videoMedia.addH264Codec(96);
        (void) m_peerConnection->addTrack(videoMedia);
        rtc::Description::Audio audioMedia("audio", rtc::Description::Direction::RecvOnly);
        audioMedia.addOpusCodec(111);
        (void) m_peerConnection->addTrack(audioMedia);
        m_peerConnection->setLocalDescription(rtc::Description::Type::Offer);
    } catch (const std::exception& e) {
        QString error = QString("WebRTC error: %1").arg(e.what());
        qWarning() << error;
        emit errorOccurred(error);
        m_connecting = false;  // 连接失败，重置标志以允许后续重连
    }
#else
    // 无 libdatachannel 时仍发送 ZLM 可接受的 play Offer（audio+video recvonly, a=mid）
    m_localSdp = buildMinimalPlayOfferSdp();
    sendOfferToServer(m_localSdp);
#endif
}

QString WebRtcClient::ensureSdpHasMid(const QString &sdp)
{
    if (sdp.isEmpty()) return sdp;
    QString lineEnd = sdp.contains("\r\n") ? QStringLiteral("\r\n") : QStringLiteral("\n");
    QStringList lines = sdp.split(lineEnd, Qt::KeepEmptyParts);
    QStringList out;
    int midIndex = 0;
    bool inMedia = false;
    bool midFound = false;
    for (int i = 0; i < lines.size(); ++i) {
        const QString &line = lines[i];
        if (line.startsWith(QLatin1String("m="))) {
            if (inMedia && !midFound) {
                int insertAt = out.size();
                for (int k = out.size() - 1; k >= 0; --k) {
                    if (out[k].startsWith(QLatin1String("m="))) {
                        insertAt = k + 1;
                        break;
                    }
                }
                out.insert(insertAt, QStringLiteral("a=mid:%1").arg(midIndex - 1));
            }
            inMedia = true;
            midFound = false;
            out.append(line);
            midIndex++;
            continue;
        }
        if (inMedia && line.startsWith(QLatin1String("a=mid"))) {
            QString rest = line.mid(5).trimmed();
            if (rest.isEmpty() || rest == QLatin1String(":")) {
                out.append(QStringLiteral("a=mid:%1").arg(midIndex - 1));
            } else {
                out.append(line);
            }
            midFound = true;
            continue;
        }
        out.append(line);
    }
    if (inMedia && !midFound) {
        int insertAt = out.size();
        for (int k = out.size() - 1; k >= 0; --k) {
            if (out[k].startsWith(QLatin1String("m="))) {
                insertAt = k + 1;
                break;
            }
        }
        out.insert(insertAt, QStringLiteral("a=mid:%1").arg(midIndex - 1));
    }
    return out.join(lineEnd);
}

QString WebRtcClient::injectRecvonlyAudioVideoIfSingleMedia(const QString &sdp)
{
    if (sdp.isEmpty()) return sdp;
    QString lineEnd = sdp.contains("\r\n") ? QStringLiteral("\r\n") : QStringLiteral("\n");
    QStringList lines = sdp.split(lineEnd, Qt::KeepEmptyParts);
    int mediaCount = 0;
    for (const QString &line : lines)
        if (line.startsWith(QLatin1String("m="))) ++mediaCount;
    if (mediaCount != 1) return sdp;

    // 从第一个 m= 段解析 ice-ufrag / ice-pwd / fingerprint，用于注入的 audio/video 段（BUNDLE 同 transport）
    QString ufrag = QStringLiteral("x") + QString::number(QRandomGenerator::global()->generate(), 16).left(7);
    QString pwd   = QString::number(QRandomGenerator::global()->generate(), 16) +
                    QString::number(QRandomGenerator::global()->generate(), 16).left(6);
    QString fingerprint(QStringLiteral("00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00"));
    bool inFirst = false;
    for (int i = 0; i < lines.size(); ++i) {
        const QString &line = lines[i];
        if (line.startsWith(QLatin1String("m="))) {
            if (!inFirst) { inFirst = true; continue; }
            break;
        }
        if (!inFirst) continue;
        if (line.startsWith(QLatin1String("a=ice-ufrag:")))
            ufrag = line.mid(12).trimmed();
        else if (line.startsWith(QLatin1String("a=ice-pwd:")))
            pwd = line.mid(10).trimmed();
        else if (line.startsWith(QLatin1String("a=fingerprint:sha-256 ")))
            fingerprint = line.mid(22).trimmed();
    }

    QString extra;
    extra += lineEnd + QStringLiteral("m=audio 9 UDP/TLS/RTP/SAVPF 0") + lineEnd;
    extra += QStringLiteral("c=IN IP4 0.0.0.0") + lineEnd;
    extra += QStringLiteral("a=recvonly") + lineEnd;
    extra += QStringLiteral("a=mid:1") + lineEnd;
    extra += QStringLiteral("a=rtcp-mux") + lineEnd;
    extra += QStringLiteral("a=ice-ufrag:") + ufrag + lineEnd;
    extra += QStringLiteral("a=ice-pwd:") + pwd + lineEnd;
    extra += QStringLiteral("a=fingerprint:sha-256 ") + fingerprint + lineEnd;
    extra += QStringLiteral("a=setup:actpass") + lineEnd;
    extra += QStringLiteral("a=rtpmap:0 PCMU/8000") + lineEnd;
    extra += QStringLiteral("m=video 9 UDP/TLS/RTP/SAVPF 96") + lineEnd;
    extra += QStringLiteral("c=IN IP4 0.0.0.0") + lineEnd;
    extra += QStringLiteral("a=recvonly") + lineEnd;
    extra += QStringLiteral("a=mid:2") + lineEnd;
    extra += QStringLiteral("a=rtcp-mux") + lineEnd;
    extra += QStringLiteral("a=ice-ufrag:") + ufrag + lineEnd;
    extra += QStringLiteral("a=ice-pwd:") + pwd + lineEnd;
    extra += QStringLiteral("a=fingerprint:sha-256 ") + fingerprint + lineEnd;
    extra += QStringLiteral("a=setup:actpass") + lineEnd;
    extra += QStringLiteral("a=rtpmap:96 H264/90000") + lineEnd;
    extra += QStringLiteral("a=fmtp:96 packetization-mode=1;profile-level-id=42e01f") + lineEnd;

    QStringList out;
    bool bundleReplaced = false;
    for (int i = 0; i < lines.size(); ++i) {
        const QString &line = lines[i];
        if (line.startsWith(QLatin1String("a=group:BUNDLE"))) {
            if (!bundleReplaced) {
                out.append(QStringLiteral("a=group:BUNDLE 0 1 2"));
                bundleReplaced = true;
            }
            continue;
        }
        out.append(line);
    }
    if (!bundleReplaced) {
        int insertAt = 0;
        for (int i = 0; i < out.size(); ++i) {
            if (out[i].startsWith(QLatin1String("a=msid-semantic")) || out[i].startsWith(QLatin1String("a=fingerprint"))) {
                insertAt = i + 1;
                break;
            }
        }
        out.insert(insertAt, QStringLiteral("a=group:BUNDLE 0 1 2"));
    }
    QString result = out.join(lineEnd) + extra;
    qDebug() << "[Client][WebRTC] injectRecvonlyAudioVideoIfSingleMedia: 已注入 m=audio( mid 1 ) + m=video( mid 2 )，BUNDLE 0 1 2";
    return result;
}

QString WebRtcClient::ensureSdpBundleGroup(const QString &sdp)
{
    if (sdp.isEmpty()) return sdp;
    QString lineEnd = sdp.contains("\r\n") ? QStringLiteral("\r\n") : QStringLiteral("\n");
    QStringList lines = sdp.split(lineEnd, Qt::KeepEmptyParts);
    int mediaCount = 0;
    for (const QString &line : lines)
        if (line.startsWith(QLatin1String("m="))) ++mediaCount;
    if (mediaCount == 0) return sdp;
    // ZLM checkValid: group.mids.size() <= media.size()；注入后为 3 个 media，BUNDLE 0 1 2
    int bundleSize = mediaCount;
    QStringList bundleMids;
    for (int i = 0; i < bundleSize; ++i)
        bundleMids.append(QString::number(i));
    QString bundleLine = QStringLiteral("a=group:BUNDLE ") + bundleMids.join(QLatin1Char(' '));
    qDebug() << "[Client][WebRTC] ensureSdpBundleGroup: mediaCount=" << mediaCount << "bundleSize=" << bundleSize << "bundleLine=" << bundleLine;
    QStringList out;
    bool bundleReplaced = false;
    for (int i = 0; i < lines.size(); ++i) {
        const QString &line = lines[i];
        if (line.startsWith(QLatin1String("a=group:BUNDLE"))) {
            if (!bundleReplaced) {
                out.append(bundleLine);
                bundleReplaced = true;
            }
            continue;
        }
        out.append(line);
    }
    if (!bundleReplaced) {
        int insertAt = 0;
        for (int i = 0; i < out.size(); ++i) {
            if (out[i].startsWith(QLatin1String("a=msid-semantic")) || out[i].startsWith(QLatin1String("a=fingerprint"))) {
                insertAt = i + 1;
                break;
            }
        }
        out.insert(insertAt, bundleLine);
    }
    return out.join(lineEnd);
}

void WebRtcClient::sendOfferToServer(const QString &offer)
{
    // ── ★★★ 端到端追踪：sendOfferToServer 进入 ★★★ ────────────────────────────
    const int64_t funcEnterTime = QDateTime::currentMSecsSinceEpoch();
    qInfo() << "[Client][WebRTC][sendOffer] ★★★ sendOfferToServer ENTER ★★★"
            << " stream=" << m_stream
            << " m_offerSent=" << m_offerSent
            << " m_streamEmpty=" << m_stream.isEmpty()
            << " m_serverUrlEmpty=" << m_serverUrl.isEmpty()
            << " enterTime=" << funcEnterTime;
    if (m_stream.isEmpty() || m_serverUrl.isEmpty()) {
        qWarning() << "[Client][WebRTC][sendOffer] ★★★ 参数校验失败，提前返回 ★★★"
                   << " stream=" << m_stream << " serverUrl=" << m_serverUrl;
        return;
    }

    if (m_offerSent) {
        qDebug() << "[Client][WebRTC] 已发送过 Offer，跳过重复 POST stream=" << m_stream;
        return;
    }
    m_offerSent = true;
    // ── 诊断：记录 Offer 发送时刻 ──────────────────────────────────────────────
    m_offerSentTime = QDateTime::currentMSecsSinceEpoch();

    // Task 1: SDP Preprocessing chain
    QString offerToSend = ensureSdpBundleGroup(injectRecvonlyAudioVideoIfSingleMedia(ensureSdpHasMid(offer)));

    QUrl apiUrl(m_serverUrl + "/index/api/webrtc");
    QUrlQuery query;
    query.addQueryItem("app", m_app);
    query.addQueryItem("stream", m_stream);
    query.addQueryItem("type", "play");
    apiUrl.setQuery(query);

    setStreamUrl(apiUrl.toString());
    qDebug() << "[Client][WebRTC] 环节: 发起拉流 stream=" << m_stream << "（若 stream not found 将最多重试 12 次，间隔 3s）";

    QString urlString = apiUrl.toString();
    qDebug() << "[Client][WebRTC] 主线程发送 POST stream=" << m_stream << "url=" << urlString;

    // 快速重连时 abort 上一轮 reply，防止多个 reply 并发回调 onSdpAnswerReceived
    if (m_currentReply) {
        m_currentReply->abort();
        m_currentReply->deleteLater();
        m_currentReply = nullptr;
    }

    QNetworkRequest request(apiUrl);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/session_description_protocol");
    request.setRawHeader("Accept", "application/json");

    m_currentReply = m_networkManager->post(request, offerToSend.toUtf8());
    qInfo() << "[Client][WebRTC][sendOffer] POST 已发送 stream=" << m_stream
            << " offerLen=" << offerToSend.size()
            << " url=" << urlString
            << " enterToPostMs=" << (QDateTime::currentMSecsSinceEpoch() - funcEnterTime)
            << " ★ 对比 onSdpAnswerReceived 进入时间，确认信令往返耗时";

    connect(m_currentReply, &QNetworkReply::finished, this, [this]() {
        onSdpAnswerReceived(m_currentReply);
    });

    connect(m_currentReply, &QNetworkReply::errorOccurred, this, [this](QNetworkReply::NetworkError error) {
        try {
            Q_UNUSED(error)
            QString err = m_currentReply ? m_currentReply->errorString() : QStringLiteral("Unknown");
            qWarning() << "[Client][WebRTC][ERROR] 请求失败 stream=" << m_stream << "error=" << err;
            updateStatus("连接失败: " + err, false);
            emit errorOccurred(err);
        } catch (const std::exception& e) {
            qCritical() << "[Client][WebRTC][ERROR] errorOccurred 回调内异常: stream=" << m_stream
                        << " error=" << e.what();
        } catch (...) {
            qCritical() << "[Client][WebRTC][ERROR] errorOccurred 回调内未知异常: stream=" << m_stream;
        }
    });
}

void WebRtcClient::onSdpAnswerReceived(QNetworkReply *reply)
{
    // ── ★★★ 端到端追踪：onSdpAnswerReceived 进入 ★★★ ─────────────────────────
    const int64_t funcEnterTime = QDateTime::currentMSecsSinceEpoch();
    const int64_t postToAnswerMs = funcEnterTime - m_offerSentTime;
    qInfo() << "[Client][WebRTC][Answer] ★★★ onSdpAnswerReceived ENTER ★★★"
            << " stream=" << m_stream
            << " reply=" << (void*)reply
            << " m_currentReply=" << (void*)m_currentReply
            << " enterTime=" << funcEnterTime
            << " postToAnswerMs=" << postToAnswerMs
            << " ★ 信令耗时（应 <500ms，超过 2s 说明 ZLM 或网络慢）";
    
    // ── 诊断：记录 Answer 收到时刻 + 各阶段耗时 ────────────────────────────────
    m_answerReceivedTime = QDateTime::currentMSecsSinceEpoch();
    const int64_t t0 = m_connectStartTime > 0 ? m_connectStartTime : m_answerReceivedTime;
    const int64_t totalDelay = m_answerReceivedTime - t0;
    const int64_t offerDelay = (m_offerSentTime > 0) ? (m_answerReceivedTime - m_offerSentTime) : -1;

    // 检查 reply 是否仍然有效
    if (!reply || reply != m_currentReply) {
        qWarning() << "[Client][WebRTC] onSdpAnswerReceived() reply 无效或已改变，忽略 stream=" << m_stream;
        if (reply) {
            reply->deleteLater();
        }
        return;
    }
    
    QByteArray data = reply->readAll();
    int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    if (reply->error() != QNetworkReply::NoError) {
        qWarning() << "[Client][WebRTC] 接收失败 stream=" << m_stream << "httpStatus=" << httpStatus
                   << "error=" << reply->errorString() << "body=" << data;
        updateStatus("接收 SDP Answer 失败", false);
        emit errorOccurred(reply->errorString());
        // ★ 清理 m_currentReply 引用，避免 disconnect() 时访问已删除的对象
        m_currentReply = nullptr;
        reply->deleteLater();
        return;
    }

    qDebug() << "[Client][WebRTC] 环节: 收到 ZLM 响应 stream=" << m_stream << " httpStatus=" << httpStatus << " bodySize=" << data.size();

    // ── 诊断：SDP Answer 协商完整时间链 ─────────────────────────────────────────
    qInfo() << "[Client][WebRTC][SDP] Answer 收到 stream=" << m_stream
             << " httpStatus=" << httpStatus << " bodySize=" << data.size()
             << " totalDelay=" << totalDelay << "ms offer→Answer=" << offerDelay << "ms"
             << " ZLM协商到Track延迟将在 onTrack 时打印";

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);

    if (parseError.error != QJsonParseError::NoError) {
        qWarning() << "[Client][WebRTC] JSON 解析失败 stream=" << m_stream << "raw=" << data;
        updateStatus("解析服务器响应失败", false);
        emit errorOccurred("JSON 解析错误: " + parseError.errorString());
        // ★ 清理 m_currentReply 引用
        m_currentReply = nullptr;
        reply->deleteLater();
        return;
    }

    QJsonObject json = doc.object();
    if (json.contains("sdp")) {
        m_retryCount = 0;
        m_remoteSdp = json["sdp"].toString();
        qDebug() << "[Client][WebRTC] 环节: ✓ 拉流成功 stream=" << m_stream << " sdpLen=" << m_remoteSdp.size();
        processAnswer(m_remoteSdp);
    } else if (json.contains("code") && json["code"].toInt() != 0) {
        int code = json["code"].toInt();
        QString errorMsg = json.contains("msg") ? json["msg"].toString() : "未知错误";
        qWarning() << "[Client][WebRTC] 环节: ZLM 返回错误 stream=" << m_stream << " code=" << code
                   << " msg=" << errorMsg << "（-400=stream not found，请确认车端已推流）";
        if (code == -400 && m_retryCount < 12) {
            m_retryCount++;
            int remaining = 12 - m_retryCount;
            qDebug() << "[Client][WebRTC] stream not found，第" << m_retryCount << "次尝试拉流（最多 12 次重试），还剩" << remaining << "次，3s 后重试 stream=" << m_stream;
            updateStatus(QString("流尚未就绪，%1s 后第 %2 次重试…").arg(3).arg(m_retryCount + 1), false);
            QTimer::singleShot(3000, this, [this]() { doConnect(); });
        } else {
            if (code == -400)
                m_retryCount = 0;
            updateStatus("流不存在或等待车端推流", false);
            emit errorOccurred(errorMsg);
        }
    } else {
        qWarning() << "[Client][WebRTC] 响应无 sdp 且无 code stream=" << m_stream << "fullBody=" << QString::fromUtf8(data);
        updateStatus("流不存在或等待车端推流", false);
        emit errorOccurred("响应格式异常");
    }

    // ★ 清理 m_currentReply 引用，避免 disconnect() 时访问已删除的对象
    m_currentReply = nullptr;
    reply->deleteLater();
    qDebug() << "[Client][WebRTC] onSdpAnswerReceived() 完成，已清理 m_currentReply stream=" << m_stream;
}

void WebRtcClient::processAnswer(const QString &answer)
{
    const int64_t funcEnterTime = QDateTime::currentMSecsSinceEpoch();
    m_remoteSdp = answer;
    // ── ★★★ 端到端追踪：processAnswer 进入 ★★★ ───────────────────────────────
    qInfo() << "[Client][WebRTC][Answer] ★★★ processAnswer ENTER ★★★"
            << " stream=" << m_stream
            << " answerLen=" << answer.size()
            << " answerHead=" << QString(answer.left(200)).replace('\r', ' ').replace('\n', ' ')
            << " m_peerConnection=" << (void*)m_peerConnection.get()
            << " enterToProcessMs=" << (QDateTime::currentMSecsSinceEpoch() - funcEnterTime);
    
#ifdef ENABLE_WEBRTC_LIBDATACHANNEL
    try {
        // Pre-process SDP to ensure compatibility (Plan 3.2)
        QString processedAnswer = ensureSdpBundleGroup(ensureSdpHasMid(answer));
        
        if (m_peerConnection) {
            rtc::Description description(processedAnswer.toStdString(), rtc::Description::Type::Answer);
            m_peerConnection->setRemoteDescription(description);
            qInfo() << "[Client][WebRTC][Answer] ★★★ setRemoteDescription 完成 ★★★"
                    << " stream=" << m_stream
                    << " ★ 对比 onTrack 进入时间，确认 ICE 协商耗时"
                    << " processAnswer耗时=" << (QDateTime::currentMSecsSinceEpoch() - funcEnterTime) << "ms";
        } else {
            qWarning() << "[Client][WebRTC] processAnswer stream=" << m_stream << "m_peerConnection 为空，跳过";
        }
    } catch (const std::exception& e) {
        QString error = QString("Failed to process SDP Answer: %1").arg(e.what());
        qWarning() << "[Client][WebRTC] processAnswer 异常 stream=" << m_stream << "error=" << error;
        emit errorOccurred(error);
        m_connecting = false;  // 异常时重置，避免残留标志屏蔽后续被动断连
        m_offerSent = false;   // 重置 Offer 发送标志，使 1s 后的 doConnect 能重发 Offer
        updateStatus("SDP 协商失败，重连中...", false);
        QTimer::singleShot(1000, this, &WebRtcClient::doConnect);
    }
#else
    // 无 libdatachannel 时仅收到 SDP，未建立真实 WebRTC 连接，无法收流；不设 isConnected 避免界面误显示「视频已连接」
    updateStatus("信令成功，需 WebRTC 库以接收视频", false);
    // m_isConnected 保持 false，界面显示上述 statusText 而非「视频已连接」
#endif
}

void WebRtcClient::disconnect()
{
    qDebug() << "[Client][WebRTC] disconnect() 开始 stream=" << m_stream;
    
    // ★ 首先停止所有定时器，防止竞态条件
    if (m_reconnectTimer && m_reconnectTimer->isActive()) {
        qDebug() << "[Client][WebRTC] disconnect() 停止重连定时器 stream=" << m_stream;
        m_reconnectTimer->stop();
    }
    
    // ★ 标记为手动断开，防止自动重连
    m_manualDisconnect = true;
    m_reconnectCount = 0;  // 重置重连计数
    m_reconnectScheduled = false;
    qInfo() << "[Client][WebRTC] disconnect() 已标记手动断开 stream=" << m_stream;
    
    // ★ 安全地断开网络回复连接
    QNetworkReply *reply = m_currentReply;
    qDebug() << "[Client][WebRTC] disconnect() m_currentReply=" << (void*)reply << " stream=" << m_stream;
    m_currentReply = nullptr;
    
    if (reply) {
        qDebug() << "[Client][WebRTC] disconnect() 准备断开 reply 连接 stream=" << m_stream;
        
        // ★ 检查对象是否仍然有效（避免访问已删除的对象）
        // QObject::disconnect() 在对象已删除时会崩溃，需要先检查
        bool isValid = false;
        try {
            // 尝试访问对象的父对象或线程来验证对象是否仍然有效
            if (reply->parent() || reply->thread()) {
                isValid = true;
                qDebug() << "[Client][WebRTC] disconnect() reply 对象有效，准备断开连接 stream=" << m_stream;
            }
        } catch (...) {
            qWarning() << "[Client][WebRTC] disconnect() reply 对象已无效（异常）stream=" << m_stream;
            isValid = false;
        }
        
        if (isValid) {
            // 使用 QPointer 包装以确保安全访问
            QPointer<QNetworkReply> safeReply(reply);
            if (safeReply) {
                qDebug() << "[Client][WebRTC] disconnect() 断开 reply 信号连接 stream=" << m_stream;
                // 断开所有连接到 this 的信号
                safeReply->disconnect(this);
                qDebug() << "[Client][WebRTC] disconnect() 中止 reply 请求 stream=" << m_stream;
                safeReply->abort();
                qDebug() << "[Client][WebRTC] disconnect() 安排 reply 延迟删除 stream=" << m_stream;
                safeReply->deleteLater();
            } else {
                qWarning() << "[Client][WebRTC] disconnect() QPointer 检查失败，reply 已删除 stream=" << m_stream;
            }
        } else {
            qWarning() << "[Client][WebRTC] disconnect() reply 对象无效，跳过断开操作 stream=" << m_stream;
            // 即使对象无效，也尝试 deleteLater（Qt 会安全处理）
            reply->deleteLater();
        }
    } else {
        qDebug() << "[Client][WebRTC] disconnect() 无活动的 reply 连接 stream=" << m_stream;
    }

    teardownMediaPipeline();

    m_isConnected = false;
    qDebug() << "[Client][WebRTC] disconnect() 更新状态为已断开 stream=" << m_stream;
    updateStatus("已断开", false);
    emit connectionStatusChanged(false);
    qInfo() << "[Client][WebRTC] disconnect() 完成 stream=" << m_stream;
}

void WebRtcClient::teardownMediaPipeline()
{
#ifdef ENABLE_WEBRTC_LIBDATACHANNEL
#if defined(ENABLE_FFMPEG)
    if (m_h264Decoder) {
        m_h264Decoder->deleteLater();
        m_h264Decoder = nullptr;
    }
#endif
    m_videoTrack.reset();
    m_audioTrack.reset();
    if (m_peerConnection) {
        qDebug() << "[Client][WebRTC] teardownMediaPipeline: close PeerConnection stream=" << m_stream;
        try {
            m_peerConnection->close();
        } catch (...) {
            qWarning() << "[Client][WebRTC] teardownMediaPipeline: peerConnection->close() 异常 stream=" << m_stream;
        }
        m_peerConnection.reset();
    }
    m_dataChannel.reset();
#else
    Q_UNUSED(this);
#endif
}

void WebRtcClient::prepareForNewConnection()
{
    m_videoFrameLogCount = 0;
    m_lastRtpPacketTime = 0;  // 重置 RTP 包到达时间，避免旧时间干扰诊断
    if (m_reconnectTimer && m_reconnectTimer->isActive()) {
        qDebug() << "[Client][WebRTC] prepareForNewConnection: 停止挂起的重连定时器 stream=" << m_stream;
        m_reconnectTimer->stop();
    }
    m_reconnectScheduled = false;

    if (m_currentReply) {
        qInfo() << "[Client][WebRTC] prepareForNewConnection: abort 进行中的信令 HTTP stream=" << m_stream;
        QNetworkReply *reply = m_currentReply;
        m_currentReply = nullptr;
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }

    teardownMediaPipeline();
}

void WebRtcClient::scheduleAutoReconnectIfNeeded(const char *reason)
{
    if (m_manualDisconnect) {
        qDebug() << "[Client][WebRTC] scheduleAutoReconnect: 跳过（手动断开） reason=" << reason
                 << " stream=" << m_stream;
        return;
    }
    if (m_connecting) {
        // 主动连接（disconnectAll+connectToStream）同步执行期间，旧 PC 的 Closed 回调会排队。
        // 此时已有新连接在途，无需再排队定时器；真正的重连由 connectToStream 失败时自行处理。
        qDebug() << "[Client][WebRTC] scheduleAutoReconnect: 跳过（主动连接进行中） reason=" << reason
                 << " stream=" << m_stream;
        return;
    }
    if (m_stream.isEmpty() || m_serverUrl.isEmpty()) {
        qWarning() << "[Client][WebRTC] scheduleAutoReconnect: 跳过（无 stream/server） reason=" << reason;
        return;
    }
    if (m_reconnectScheduled) {
        qDebug() << "[Client][WebRTC] scheduleAutoReconnect: 已排队，忽略重复 reason=" << reason
                 << " stream=" << m_stream;
        return;
    }
    constexpr int kMaxReconnectAttempts = 5;
    if (m_reconnectCount >= kMaxReconnectAttempts) {
        // 达到上限后，等待 60s 再重置计数器（防止无限循环高频重连）
        if (!m_reconnectTimer) return;
        qWarning() << "[Client][WebRTC] 自动重连次数已达上限（" << kMaxReconnectAttempts
                   << "）stream=" << m_stream << " lastReason=" << reason
                   << " — 60s 后自动重置计数器并重试";
        updateStatus(QStringLiteral("连接持续断开，60s 后自动恢复…"), false);
        m_reconnectTimer->setInterval(60000);
        m_reconnectTimer->start();
        // 将计数器延迟重置（在下一次定时器触发时）
        connect(m_reconnectTimer, &QTimer::timeout, this, [this]() {
            m_reconnectCount = 0;
            qInfo() << "[Client][WebRTC] 重连计数器已重置，等待下次触发";
        }, Qt::SingleShotConnection);
        return;
    }

    // ── 诊断：重连触发时打印完整状态上下文 ─────────────────────────────────────
    const int64_t now = QDateTime::currentMSecsSinceEpoch();
    const int64_t connDuration = (m_connectStartTime > 0) ? (now - m_connectStartTime) : -1;
    const int64_t lastFrameAge = (m_lastFrameTime > 0) ? (now - m_lastFrameTime) : -1;
    qWarning() << "[Client][WebRTC][State][Diag] PeerConnection Closed/Failed stream=" << m_stream
               << " reason=" << reason
               << " 已连接时长=" << connDuration << "ms"
               << " 上帧距今=" << lastFrameAge << "ms"
               << " 本周期帧数=" << m_videoFrameLogCount
               << " 【诊断结论】："
               << (m_videoFrameLogCount == 0 ? "ZLM 从未发帧(检查 ZLM 推流)" :
                   lastFrameAge > 5000 ? "ZLM 已停发帧(检查 carla-bridge→ZLM 链路)" :
                   "ICE/UDP 连接断开(ZLM 或网络抖动)");

    m_reconnectScheduled = true;
    m_reconnectCount++;
    // 指数退避：5s → 10s → 20s → 40s，上限 30s
    int delayMs = qMin(30000, 5000 * (1 << (m_reconnectCount - 1)));
    const int remaining = kMaxReconnectAttempts - m_reconnectCount;
    qWarning() << "[Client][WebRTC] 安排自动重连 reason=" << reason << " attempt=" << m_reconnectCount
               << "/" << kMaxReconnectAttempts << " remainingAfterThis=" << remaining
               << " delayMs=" << delayMs << " stream=" << m_stream;
    updateStatus(QString("连接断开(%1)，%2ms后第%3次重连…")
                     .arg(QLatin1String(reason))
                     .arg(delayMs)
                     .arg(m_reconnectCount),
                 false);
    if (m_reconnectTimer) {
        m_reconnectTimer->setInterval(delayMs);
        m_reconnectTimer->start();
    }
}

#if defined(ENABLE_WEBRTC_LIBDATACHANNEL) && defined(ENABLE_FFMPEG)
void WebRtcClient::setupVideoDecoder()
{
    if (!m_videoTrack || m_h264Decoder) return;
    // ── 诊断：记录从 onTrack 到 decoder setup 的延迟 ───────────────────────────
    const int64_t now = QDateTime::currentMSecsSinceEpoch();
    const int64_t trackDelay = (m_trackReceivedTime > 0) ? (now - m_trackReceivedTime) : -1;
    const int64_t connDelay = (m_connectStartTime > 0) ? (now - m_connectStartTime) : -1;
    qInfo() << "[Client][WebRTC][SDP] setupVideoDecoder stream=" << m_stream
             << " onTrack→setupDelay=" << trackDelay << "ms"
             << " connectStart→setupDelay=" << connDelay << "ms"
             << " framesSinceLast=" << m_framesSinceLastStats
             << " lastFrameAge=" << (m_lastFrameTime > 0 ? (now - m_lastFrameTime) : -1) << "ms";

    try {
        m_h264Decoder = new H264Decoder(m_stream, this);
        qInfo() << "[Client][WebRTC][setupVideoDecoder] 创建 H264Decoder stream=" << m_stream;
        connect(m_h264Decoder, &H264Decoder::frameReady, this, &WebRtcClient::onVideoFrameFromDecoder, Qt::QueuedConnection);
    } catch (const std::exception& e) {
        qCritical() << "[Client][WebRTC][ERROR] setupVideoDecoder: 创建/连接 H264Decoder 异常:"
                    << " stream=" << m_stream << " error=" << e.what();
        return;
    } catch (...) {
        qCritical() << "[Client][WebRTC][ERROR] setupVideoDecoder: 创建/连接 H264Decoder 未知异常 stream=" << m_stream;
        return;
    }

        // ── ★★★ 诊断：setupVideoDecoder 后立即检查 frameReady 信号是否有接收者 ★★★
        // 对比 H264Decoder::emitDecodedFrames 中的 "emit frameReady 完成" 日志
        int frameReadyReceivers = 0;
        if (m_h264Decoder) {
            frameReadyReceivers = m_h264Decoder->receiverCountFrameReady();
        }
        qInfo() << "[Client][WebRTC][setupVideoDecoder] stream=" << m_stream
                 << " m_h264Decoder=" << (void*)m_h264Decoder
                 << " frameReadyReceivers=" << frameReadyReceivers
                 << " ★ frameReadyReceivers=0 → H264Decoder::emit frameReady 信号无接收者，"
                 << " 对比 'emit frameReady 完成' 日志中 queuedConnections=" << frameReadyReceivers;

        // libdatachannel onMessage 回调在工作线程执行，FFmpeg AVCodecContext/SwsContext 非线程安全。
    // 通过 QueuedConnection 将数据拷贝传回主线程，彻底消除数据竞争，代价是一次 QByteArray 拷贝（~MTU）。
    // ── ★★★ 端到端追踪：RTP 包在 libdatachannel 工作线程到达 ★★★ ───────────
    // 对比 feedRtp 进入时间（主线程），可测量 "工作线程→主线程" 队列延迟
    static QAtomicInt s_rtpArrivalLogCount{0};
    static QAtomicInt s_camFrontLogCount{0};
    m_videoTrack->onMessage([this](rtc::message_variant msg) {
        try {
            // ── v3 新增：分配 lifecycleId（端到端帧追踪）────────────────────
            const quint64 lifecycleId = VideoFrame::nextLifecycleId();

            // ── 诊断：cam_front RTP 包到达计数（前 50 个都打印）────────────────────
            // 用于确认 cam_front 的 onMessage 是否被调用（若从未出现日志 → track.onMessage 未建立）
            const int64_t onMsgTime = QDateTime::currentMSecsSinceEpoch();
            const int camFrontSeq = ++s_camFrontLogCount;
            if (camFrontSeq <= 50) {
                qInfo() << "[Client][WebRTC][onMessage] ★★★ onMessage 工作线程回调 ★★★"
                        << " camFrontSeq=" << camFrontSeq
                        << " stream=" << m_stream
                        << " hasVariant=" << msg.index()
                        << " lifecycleId=" << lifecycleId
                        << " time=" << onMsgTime
                        << "（若 cam_front 从未出现此日志 → track.onMessage 回调未建立或未触发）";
            }
            if (std::holds_alternative<rtc::binary>(msg)) {
                const auto &bin = std::get<rtc::binary>(msg);
                if (!m_h264Decoder || bin.empty()) return;
                const int64_t rtpArrivalTime = QDateTime::currentMSecsSinceEpoch();
                // ★★★ RTP 包到达诊断：用于对比 feedRtp（主线程）进入时间 ★★★
                const int64_t frameGap = (m_lastRtpPacketTime > 0) ? (rtpArrivalTime - m_lastRtpPacketTime) : -1;
                m_lastRtpPacketTime = rtpArrivalTime;
                const int seq = ++s_rtpArrivalLogCount;
                // ★★★ 诊断：打印每个流的 RTP 包到达（不限数量，直到 feedRtp 正常工作）★★★
                // 用于确认 cam_front/rear/left/right 各自是否收到 RTP 包
                if (seq <= 20 || frameGap > 100 || frameGap < 0 || m_stream.contains("cam_front")) {
                    qInfo() << "[Client][WebRTC][RTP-Arrival] ★★★ RTP包到达(libdatachannel工作线程) ★★★"
                            << " seq=" << seq
                            << " stream=" << m_stream
                            << " lifecycleId=" << lifecycleId
                            << " pktSize=" << bin.size()
                            << " rtpArrival=" << rtpArrivalTime
                            << " frameGapFromLast=" << frameGap << "ms"
                            << "（>100ms=发帧慢或网络抖动，<0=首次包）"
                            << " ★ 对比 feedRtp ENTER 日志中的 rtpArrival，确认 cam_front 是否从工作线程到达 feedRtp";
                }
                QByteArray pkt(reinterpret_cast<const char *>(bin.data()), static_cast<qsizetype>(bin.size()));
                QMetaObject::invokeMethod(m_h264Decoder, [dec = m_h264Decoder, pkt = std::move(pkt), rtpArrivalTime, lifecycleId, this]() {
                    // ── 诊断：RTP 包到达主线程时的队列延迟（工作线程→主线程）────────────────
                    const int64_t mainThreadTime = QDateTime::currentMSecsSinceEpoch();
                    const int64_t queueDelay = mainThreadTime - rtpArrivalTime;
                    // 仅首20帧或延迟异常时打印
                    static QAtomicInt s_queueDelayLogCount{0};
                    const int logSeq = ++s_queueDelayLogCount;
                    if (logSeq <= 20 || queueDelay > 50) {
                        qInfo() << "[Client][WebRTC][RTP-Queue] ★ feedRtp ENTER(主线程) ★"
                                << " seq=" << logSeq
                                << " stream=" << m_stream
                                << " lifecycleId=" << lifecycleId
                                << " rtpArrival=" << rtpArrivalTime
                                << " mainThread=" << mainThreadTime
                                << " queueDelay=" << queueDelay << "ms"
                                << "（>50ms=主线程阻塞，<10ms=正常）";
                    }
                    // ── 诊断：RTP 包到达时记录，用于判断"收到包但没帧"vs"根本没收到包" ─
                    ++m_framesSinceLastStats;
                    try {
                        dec->feedRtp(reinterpret_cast<const uint8_t *>(pkt.constData()),
                                     static_cast<size_t>(pkt.size()), lifecycleId);
                    } catch (const std::exception& e) {
                        qCritical() << "[Client][WebRTC][ERROR] feedRtp 异常 stream=" << m_stream
                                    << " pktSize=" << pkt.size() << " lifecycleId=" << lifecycleId << " error=" << e.what();
                    } catch (...) {
                        qCritical() << "[Client][WebRTC][ERROR] feedRtp 未知异常 stream=" << m_stream
                                    << " pktSize=" << pkt.size() << " lifecycleId=" << lifecycleId;
                    }
                }, Qt::QueuedConnection);
            }
        } catch (const std::exception& e) {
            qCritical() << "[Client][WebRTC][onMessage][ERROR] 异常 stream=" << m_stream << " error=" << e.what();
        } catch (...) {
            qCritical() << "[Client][WebRTC][onMessage][ERROR] 未知异常 stream=" << m_stream;
        }
    });
    qDebug() << "[Client][WebRTC] video track RTP -> H264 decoder -> videoFrameReady 已连接 stream=" << m_stream;
}

void WebRtcClient::onVideoFrameFromDecoder(const QImage &image, quint64 frameId)
{
    // ★★★ 关键诊断：确认 onVideoFrameFromDecoder 被调用（信号从 H264Decoder 到达）★★★
    // ★★★ 如果此日志不出现 → H264Decoder::emit frameReady 信号断链，或 QueuedConnection 失败 ★★★
    static QSet<QString> s_loggedStreams;
    if (!s_loggedStreams.contains(m_stream)) {
        s_loggedStreams.insert(m_stream);
        qInfo() << "[Client][WebRTC][onVideoFrameFromDecoder] ★★★ 函数被调用（信号从 H264Decoder 到达）★★★"
                   " stream=" << m_stream << " frameId=" << frameId
                   << " lifecycleId=" << (m_h264Decoder ? m_h264Decoder->currentLifecycleId() : 0);
    }

    // ── v3 新增：从 H264Decoder 获取 lifecycleId ──────────────────────────
    const quint64 lifecycleId = m_h264Decoder ? m_h264Decoder->currentLifecycleId() : 0;

    try {
        // ── 队列积压诊断：进入时计数，用于判断主线程是否被阻塞 ───────────────────
        ++m_framesPendingInQueue;
        const int64_t handlerEnterTime = QDateTime::currentMSecsSinceEpoch();

        if (image.isNull()) {
            // ★★★ FrameDiag：空帧根因诊断 ★★★
            qCritical() << "[WebRTC][FrameDiag] ★★★ 收到空视频帧！frameId=" << frameId
                        << " stream=" << m_stream
                        << " lifecycleId=" << lifecycleId
                        << " ★ 此帧将被静默丢弃！检查 sws_scale/m_frameBuffer/IDR 等待 ★"
                        << " ★ 若持续出现 → ZLM 侧断流或解码器需要重新初始化 ★";
            --m_framesPendingInQueue;
            return;
        }

        m_videoFrameLogCount++;

        // ★★★ FrameDiag：renderer 注册状态诊断 ★★★
        // 用于区分「帧到达但 renderer 未注册导致静默丢弃」vs「其他原因丢帧」
        {
            static int s_rendererDiagLogged = 0;
            if (s_rendererDiagLogged < 4) {
                ++s_rendererDiagLogged;
                qInfo() << "[WebRTC][FrameDiag] renderer注册状态"
                        << " stream=" << m_stream
                        << " frame#=" << m_videoFrameLogCount
                        << " m_videoRenderer.isNull=" << m_videoRenderer.isNull()
                        << " rendererClass=" << (m_videoRenderer ?
                            QString::fromLatin1(m_videoRenderer->metaObject()->className()) : "N/A")
                        << " directInvokeWillRun=" << (bool)m_videoRenderer
                        << " ★ m_videoRenderer.isNull=true → 帧将只走 QML signal 路径 ★"
                        << " ★ 对比 VideoRenderer setFrame 日志确认 C++→QML 链路是否通 ★";
            }
        }

        // ── 诊断：帧时间戳追踪（用于断开时判断"根本没收到帧"还是"收到了但卡在解码"） ──
        if (m_lastFrameTime > 0) {
            const int64_t frameInterval = handlerEnterTime - m_lastFrameTime;
            if (frameInterval > 2000) {
                qWarning() << "[Client][WebRTC][FrameGap] stream=" << m_stream
                             << " 帧间隔异常 gap=" << frameInterval << "ms"
                             << " (正常应 ~33ms @30fps)，可能在 ZLM 侧或 UDP 传输层断了"
                             << " framesSinceLast=" << m_framesSinceLastStats;
            }
        }
        m_lastFrameTime = handlerEnterTime;
        m_framesSinceLastStats = 0;

        // 每路每连接周期：前 10 帧必打 INFO，便于确认「解码→emit→QML」不断链（无需额外开关）
        if (m_videoFrameLogCount <= 10) {
            qInfo() << "[Client][VideoFrame] stream=" << m_stream << " frame#" << m_videoFrameLogCount
                    << " size=" << image.size() << " format=" << static_cast<int>(image.format())
                    << " queuePending=" << m_framesPendingInQueue
                    << "（前10帧；之后设 ENABLE_VIDEO_FRAME_LOG=1 可看节流日志）";
        }

        // 节流日志：每 120 帧打一次（默认关闭）
        if (qEnvironmentVariableIntValue("ENABLE_VIDEO_FRAME_LOG") > 0 && m_videoFrameLogCount > 10
            && (m_videoFrameLogCount % 120 == 0)) {
            qDebug() << "[Client][VideoFrame] stream=" << m_stream << " frame#" << m_videoFrameLogCount
                     << " size=" << image.size() << " format=" << image.format()
                     << " queuePending=" << m_framesPendingInQueue;
        }

        // 队列积压警告：积压 >2 说明主线程被阻塞
        if (m_framesPendingInQueue > 2 && m_videoFrameLogCount <= 20) {
            qWarning() << "[Client][WebRTC][Queue] stream=" << m_stream
                        << " 帧队列积压=" << m_framesPendingInQueue
                        << "（>2 说明主线程被阻塞，导致视频卡顿）"
                        << " frame#=" << m_videoFrameLogCount;
        }

        try {
            const int64_t emitEnterTime = QDateTime::currentMSecsSinceEpoch();
            const bool isFirstFrames = (m_videoFrameLogCount <= 3);

            // ════════════════════════════════════════════════════════════════════
            // ★★★ 核心修复路径 1：直接方法调用（绕过 QML Connections 信号层）★★★
            // ════════════════════════════════════════════════════════════════════
            // 调用链：WebRtcClient::onVideoFrameFromDecoder → QMetaMethod::invoke → VideoRenderer.setFrame
            //         → deliverFrame → window()->update() → updatePaintNode（渲染线程）
            //
            // 为什么用 QueuedConnection：
            //   - onVideoFrameFromDecoder 在解码器线程通过 QueuedConnection 被调用
            //   - VideoRenderer.setFrame 必须从主线程执行（QQuickItem/QQuickWindow 操作必须在主线程）
            //   - QueuedConnection 将调用排队到主线程事件队列，安全跨线程
            //   - Qt::QueuedConnection 参数会被自动拷贝和序列化（QImage 会走隐式共享拷贝）
            //
            // 为什么不用 Qt::DirectConnection：
            //   - 会从解码器线程直接调用 VideoRenderer::setFrame
            //   - QQuickItem 和 QQuickWindow API 都不是线程安全的
            //   - 渲染线程和主线程的同步问题会导致数据竞争
            //
            // 防御性设计（多层安全网）：
            //   1. QPointer<QQuickItem>：VideoRenderer 在 QML 销毁时自动置 null
            //   2. isFirstFrames 时打印诊断信息
            //   3. invoke 失败时自动 fallback 到 emit videoFrameReady
            //   4. QTimer::singleShot 兜底：invoke 失败时在主线程再次尝试
            // ════════════════════════════════════════════════════════════════════

            bool directInvokeSuccess = false;

            if (m_videoRenderer) {
                // 前3帧：打印直接调用诊断
                if (isFirstFrames) {
                    qInfo() << "[Client][WebRTC][DirectCall] ★★★ 直接调用路径检查 ★★★"
                             << " stream=" << m_stream << " frameId=" << frameId
                             << " renderer=" << QString::number((quintptr)m_videoRenderer.data(), 16)
                             << " rendererClass=" << (m_videoRenderer->metaObject()
                                 ? QString::fromLatin1(m_videoRenderer->metaObject()->className())
                                 : "N/A")
                             << " rendererStreamName=" << m_rendererStreamName
                             << " ★ 对比 setVideoRenderer 日志中的 rendererPtr 确认是同一对象";
                }

                // 查找 setFrame 方法
                const QMetaObject* mo = m_videoRenderer->metaObject();
                QMetaMethod setFrameMethod;
                bool methodFound = false;

                for (int i = mo->methodOffset(); i < mo->methodCount(); ++i) {
                    QMetaMethod m = mo->method(i);
                    if (m.methodType() == QMetaMethod::Method &&
                        QString::fromLatin1(m.name()) == QLatin1String("setFrame")) {
                        setFrameMethod = m;
                        methodFound = true;
                        break;
                    }
                }

                if (!methodFound) {
                    // 方法不存在 → fallback
                    qCritical() << "[Client][WebRTC][DirectCall][FATAL] stream=" << m_stream
                                 << " frameId=" << frameId
                                 << " ★★★ VideoRenderer 没有 setFrame 方法！fallback 到 emit ★★★"
                                 << " rendererClass=" << (mo ? QString::fromLatin1(mo->className()) : "N/A");
                } else {
                    // QMetaMethod::invoke（QueuedConnection → 主线程安全）
                    // Qt::QueuedConnection：参数自动序列化，调用排队到主线程事件队列，
                    // 解码器线程立即返回，不阻塞解码流水线。
                    // 参数类型：
                    //   - QImage：隐式共享拷贝（refcount+1，数据共享，写时复制）
                    //     解码器侧的原始 image 在函数返回后仍安全有效
                    //   - quint64：值拷贝（64位整数，直接序列化）
                    // ── v3 新增：lifecycleId 用于端到端追踪 ──────────────────────
                    quint64 lid = lifecycleId;
                    bool ok = setFrameMethod.invoke(
                        m_videoRenderer.data(),
                        Qt::QueuedConnection,
                        QGenericArgument(QMetaType::typeName(qMetaTypeId<QImage>()), &image),
                        QGenericArgument(QMetaType::typeName(qMetaTypeId<quint64>()), &frameId),
                        QGenericArgument(QMetaType::typeName(qMetaTypeId<quint64>()), &lid)
                    );

                    if (ok) {
                        directInvokeSuccess = true;
                        if (isFirstFrames) {
                            const int64_t cost = QDateTime::currentMSecsSinceEpoch() - emitEnterTime;
                            qInfo() << "[Client][WebRTC][DirectCall] ★★★ 直接调用成功 ★★★"
                                     << " stream=" << m_stream << " frameId=" << frameId
                                     << " lifecycleId=" << lifecycleId
                                     << " method=" << QString::fromLatin1(setFrameMethod.methodSignature())
                                     << " invokeCost≈0ms(QueuedConnection) frame#=" << m_videoFrameLogCount
                                     << " ★ 对比 VideoRenderer setFrame 日志确认 frameId";
                        }
                    } else {
                        // invoke 失败 → fallback
                        // 常见原因：VideoRenderer 正在 QML 销毁过程中、GL 上下文失效
                        qCritical() << "[Client][WebRTC][DirectCall][ERROR] stream=" << m_stream
                                     << " frameId=" << frameId
                                     << " lifecycleId=" << lifecycleId
                                     << " ★★★ QMetaMethod::invoke 失败！fallback 到 emit ★★★"
                                     << " error=invoke returned false"
                                     << " renderer=" << QString::number((quintptr)m_videoRenderer.data(), 16)
                                     << " method=" << QString::fromLatin1(setFrameMethod.methodSignature());

                        // 兜底：invoke 失败后用 QTimer::singleShot 在主线程再次尝试
                        // 解码器线程不能直接调用主线程 API，用定时器将调用推迟到主线程
                        QTimer::singleShot(0, this, [self = QPointer<WebRtcClient>(this),
                                                     renderer = QPointer<QObject>(m_videoRenderer.data()),
                                                     img = image, fid = frameId, stream = m_stream]() {
                            if (!self || !renderer) {
                                qWarning() << "[Client][WebRTC][DirectCall][Fallback] WebRtcClient 或 renderer 已销毁，跳过"
                                           << " stream=" << stream;
                                return;
                            }
                            const QMetaObject* mo2 = renderer->metaObject();
                            for (int i = mo2->methodOffset(); i < mo2->methodCount(); ++i) {
                                QMetaMethod m = mo2->method(i);
                                if (m.methodType() == QMetaMethod::Method &&
                                    QString::fromLatin1(m.name()) == QLatin1String("setFrame")) {
                                    bool retryOk = m.invoke(renderer.data(), Qt::AutoConnection,
                                        QGenericArgument(QMetaType::typeName(qMetaTypeId<QImage>()), &img),
                                        QGenericArgument(QMetaType::typeName(qMetaTypeId<quint64>()), &fid)
                                    );
                                    if (!retryOk) {
                                        qCritical() << "[Client][WebRTC][DirectCall][Fallback][ERROR]"
                                                     << " stream=" << stream << " frameId=" << fid
                                                     << " ★★★ retry invoke 也失败了，帧被丢弃 ★★★";
                                    } else {
                                        qInfo() << "[Client][WebRTC][DirectCall][Fallback] ★ retry invoke 成功 ★"
                                                 << " stream=" << stream << " frameId=" << fid;
                                    }
                                    return;
                                }
                            }
                            qCritical() << "[Client][WebRTC][DirectCall][Fallback][ERROR] stream=" << stream
                                         << " frameId=" << fid << " setFrame 方法不存在（retry）";
                        });
                    }
                }
            } else {
                // renderer 未注册
                if (isFirstFrames) {
                    qInfo() << "[Client][WebRTC][DirectCall] ★ renderer 未注册，跳过直接调用 ★"
                             << " stream=" << m_stream << " frame#=" << m_videoFrameLogCount
                             << " → fallback 到 emit videoFrameReady"
                             << " ★ 检查 VideoRenderer Component.onCompleted 是否调用了 setVideoRenderer";
                }
            }

            // ════════════════════════════════════════════════════════════════════
            // ★★★ Fallback 路径 2：emit videoFrameReady（QML Connections 旧路径）★★★
            // ════════════════════════════════════════════════════════════════════
            // 两个路径都调用时，VideoRenderer.setFrame 会被调用两次。
            // 这是有意设计：若 direct invoke 成功则帧已处理；若失败则 emit 确保不丢帧。
            // 直接调用成功时 signalReceivers 仍为 0 是正常的（信号层被绕过）。

            emit videoFrameReady(image, image.width(), image.height(), frameId);

            // ── 端到端追踪：emit 完成 ────────────────────────────────────────────
            const int64_t emitCost = QDateTime::currentMSecsSinceEpoch() - emitEnterTime;
            int signalReceiverCount = this->receivers(SIGNAL(videoFrameReady(const QImage&, int, int, quint64)));
            if (isFirstFrames || signalReceiverCount == 0) {
                qInfo() << "[Client][WebRTC][emitDone] stream=" << m_stream << " frame#=" << m_videoFrameLogCount
                         << " frameId=" << frameId
                         << " lifecycleId=" << lifecycleId
                         << " emitCost=" << emitCost << "ms"
                         << " signalReceivers=" << signalReceiverCount
                         << " directInvokeSuccess=" << directInvokeSuccess
                         << " thisPtr=" << QString::number((quintptr)this, 16)
                         << " m_stream=" << m_stream
                         << " ★ directInvokeSuccess=true → 直接路径工作；false → fallback"
                         << " ★ signalReceivers>0=QML Connections 已连接"
                         << " ★ 对比 VideoRenderer setFrame 日志确认 frameId 是否到达";
            }
            if (Q_UNLIKELY(signalReceiverCount == 0 && !directInvokeSuccess)) {
                // 两个路径都失败 → 帧丢失（已降级到 FATAL）
                qCritical() << "[Client][WebRTC][FATAL][emitDone] ★★★ 两个传递路径全部失败！帧被静默丢弃！★★★"
                           << " stream=" << m_stream
                           << " frameId=" << frameId
                           << " lifecycleId=" << lifecycleId
                           << " thisPtr=" << QString::number((quintptr)this, 16)
                           << " signalReceivers=" << signalReceiverCount
                           << " directInvokeSuccess=" << directInvokeSuccess
                           << " rendererNull=" << m_videoRenderer.isNull()
                           << " ★ 立即检查 VideoRenderer.setFrame 日志和 QML Connections ★"
                           << " ★ 常见原因：VideoRenderer 未调用 setVideoRenderer() ★";
            } else if (Q_UNLIKELY(signalReceiverCount == 0 && directInvokeSuccess)) {
                // 直接调用成功但信号无接收者 → 正常（直接路径绕过了信号机制）
                qInfo() << "[Client][WebRTC][emitDone] stream=" << m_stream
                         << " frameId=" << frameId
                         << " directInvokeSuccess=true 但 signalReceivers=0（正常：直接路径绕过了信号机制）";
            }
        } catch (const std::exception& e) {
            qCritical() << "[Client][WebRTC][ERROR] emit videoFrameReady 异常 stream=" << m_stream
                        << " error=" << e.what() << " image.size=" << image.size();
        } catch (...) {
            qCritical() << "[Client][WebRTC][ERROR] emit videoFrameReady 未知异常 stream=" << m_stream
                        << " image.size=" << image.size();
        }

        // emit 后立即退出（QML handler 在 emit 返回后异步执行）。
        // 注意：QueuedConnection 的 handler 执行时刻无法在 C++ 侧直接感知。
        --m_framesPendingInQueue;
        m_lastHandlerDoneTime = QDateTime::currentMSecsSinceEpoch();
    } catch (const std::exception& e) {
        qCritical() << "[Client][WebRTC][ERROR] onVideoFrameFromDecoder 总异常: stream=" << m_stream
                    << " error=" << e.what();
        --m_framesPendingInQueue;
    } catch (...) {
        qCritical() << "[Client][WebRTC][ERROR] onVideoFrameFromDecoder 未知异常: stream=" << m_stream;
        --m_framesPendingInQueue;
    }
}
#endif

void WebRtcClient::sendDataChannelMessage(const QByteArray &data)
{
    if (!m_isConnected) {
        qWarning() << "[Client][WebRTC][WARN] Cannot send message: not connected";
        return;
    }

#ifdef ENABLE_WEBRTC_LIBDATACHANNEL
    if (m_dataChannel && m_dataChannel->isOpen()) {
        try {
            m_dataChannel->send(std::string(data.data(), data.length()));
            qDebug() << "[Client][WebRTC] Sent data channel message:" << data;
        } catch (const std::exception& e) {
            qCritical() << "[Client][WebRTC][ERROR] Failed to send data channel message:" << e.what();
        } catch (...) {
            qCritical() << "[Client][WebRTC][ERROR] Failed to send data channel message: unknown exception";
        }
    } else {
        qWarning() << "[Client][WebRTC][WARN] DataChannel not open, cannot send message";
    }
#else
    qDebug() << "Sending data channel message (simulated):" << data;
#endif
}

QString WebRtcClient::videoFrameReadySignalMeta() const
{
    const QMetaObject *mo = metaObject();
    // 查找 videoFrameReady 信号（overload 已消除，现为唯一 4 参数版本）
    QStringList results;
    for (int i = mo->methodOffset(); i < mo->methodCount(); ++i) {
        QMetaMethod method = mo->method(i);
        if (method.methodType() == QMetaMethod::Signal &&
            QString::fromLatin1(method.name()) == QLatin1String("videoFrameReady")) {
            QString sig = QString::fromLatin1(method.methodSignature());
            results.append(QStringLiteral("index=%1 params=%2 sig=%3")
                .arg(i)
                .arg(method.parameterCount())
                .arg(sig));
        }
    }
    return results.isEmpty() ? QStringLiteral("NOT FOUND") : results.join(QLatin1String(" | "));
}

int WebRtcClient::receiverCountVideoFrameReady() const
{
    return receivers(SIGNAL(videoFrameReady(const QImage&, int, int, quint64)));
}

void WebRtcClient::setVideoRenderer(QObject* renderer, const QString& streamName)
{
    // ── 防御性检查：renderer 必须非空 ─────────────────────────────────────────
    if (!renderer) {
        qWarning() << "[Client][WebRTC][setVideoRenderer] ★ renderer 为 null，拒绝注册！streamName=" << streamName
                   << " this=" << QString::number((quintptr)this, 16);
        m_videoRenderer = nullptr;
        m_rendererStreamName.clear();
        return;
    }

    // 避免重复注册同一对象（跨 VideoPanel 复用 WebRtcClient 时可能发生）
    if (m_videoRenderer.data() == renderer) {
        qInfo() << "[Client][WebRTC][setVideoRenderer] ★ 重复注册相同 renderer，忽略 streamName=" << streamName
                << " renderer=" << QString::number((quintptr)renderer, 16);
        return;
    }

    m_videoRenderer = qobject_cast<QQuickItem*>(renderer);
    m_rendererStreamName = streamName;
    emit videoRendererChanged(renderer);
    qInfo() << "[Client][WebRTC][setVideoRenderer] ★★★ renderer 注册成功 ★★★"
            << " streamName=" << streamName
            << " rendererPtr=" << QString::number((quintptr)renderer, 16)
            << " rendererClass=" << (renderer->metaObject() ? QString::fromLatin1(renderer->metaObject()->className()) : "N/A")
            << " this=" << QString::number((quintptr)this, 16)
            << " currentStream=" << m_stream
            << " ★ 对比 VideoRenderer Component.onCompleted 日志确认注册时序";

    // ── 诊断：立即验证 VideoRenderer 有无 setFrame 方法 ───────────────────────
    const QMetaObject* mo = renderer->metaObject();
    bool hasSetFrame = false;
    for (int i = mo->methodOffset(); i < mo->methodCount(); ++i) {
        QMetaMethod m = mo->method(i);
        if (m.methodType() == QMetaMethod::Method &&
            QString::fromLatin1(m.name()) == QLatin1String("setFrame")) {
            hasSetFrame = true;
            qInfo() << "[Client][WebRTC][setVideoRenderer] ★ setFrame 方法验证成功 ★"
                    << " methodType=Method"
                    << " paramCount=" << m.parameterCount()
                    << " signature=" << QString::fromLatin1(m.methodSignature())
                    << " 参数类型: " << [m]() -> QString {
                QStringList types;
                for (int j = 0; j < m.parameterCount(); ++j) {
                    types.append(QString::fromLatin1(m.parameterTypes()[j]));
                }
                return types.join(QLatin1String(", "));
            }()
                    << " streamName=" << streamName;
            break;
        }
    }
    if (!hasSetFrame) {
        qCritical() << "[Client][WebRTC][setVideoRenderer] ★★★ FATAL: renderer 没有 setFrame 方法！★★★"
                    << " rendererClass=" << (mo ? QString::fromLatin1(mo->className()) : "N/A")
                    << " streamName=" << streamName
                    << " → 直接渲染路径失效，fallback 到 emit videoFrameReady";
    }
}

void WebRtcClient::forceRefresh()
{
    // ── 强制刷新机制（方案1）─────────────────────────────────────────────
    // 根因：Qt Scene Graph 在 VehicleSelectionDialog 显示期间可能阻塞渲染线程，
    // 导致 deliverFrame 收到帧但 updatePaintNode 不被调用。
    // 修复：通过 m_videoRenderer 指针调用 VideoRenderer::forceRefresh()。
    const int64_t now = QDateTime::currentMSecsSinceEpoch();
    qInfo() << "[Client][WebRTC] ★★★ forceRefresh 被调用 ★★★"
            << " now=" << now
            << " stream=" << m_stream
            << " renderer=" << (void*)m_videoRenderer.data()
            << " rendererStreamName=" << m_rendererStreamName;

    if (m_videoRenderer) {
        // 尝试将 QQuickItem* 转换为 VideoRenderer*，然后调用 forceRefresh()
        VideoRenderer* vr = qobject_cast<VideoRenderer*>(m_videoRenderer.data());
        if (vr) {
            // 调用 VideoRenderer::forceRefresh()
            QMetaObject::invokeMethod(vr, "forceRefresh", Qt::DirectConnection);
            qInfo() << "[Client][WebRTC] forceRefresh: VideoRenderer::forceRefresh() 已调用"
                    << " stream=" << m_stream;
        } else {
            qWarning() << "[Client][WebRTC] forceRefresh: m_videoRenderer 不是 VideoRenderer 类型，跳过"
                        << " stream=" << m_stream
                        << " actualClass=" << (m_videoRenderer->metaObject() ? QString::fromLatin1(m_videoRenderer->metaObject()->className()) : "N/A");
        }
    } else {
        qWarning() << "[Client][WebRTC] forceRefresh: m_videoRenderer 为空，跳过"
                    << " stream=" << m_stream;
    }
}

void WebRtcClient::updateStatus(const QString &status, bool connected)
{
    m_statusText = status;
    m_isConnected = connected;
    emit statusTextChanged(m_statusText);
    emit connectionStatusChanged(m_isConnected);
}
