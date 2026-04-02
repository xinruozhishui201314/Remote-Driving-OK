#include "webrtcclient.h"
#include <QNetworkRequest>
#include <QUrlQuery>
#include <QDebug>
#include <QUrl>
#include <QTimer>
#include <QElapsedTimer>
#include <QDateTime>
#include <QRandomGenerator>
#include <QPointer>
#if defined(ENABLE_WEBRTC_LIBDATACHANNEL) && defined(ENABLE_FFMPEG)
#include "h264decoder.h"
#endif

#ifdef ENABLE_WEBRTC_LIBDATACHANNEL
#include <rtc/rtc.hpp>
#endif

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
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/sdp");
    request.setRawHeader("Accept", "application/json");

    m_currentReply = m_networkManager->post(request, offerToSend.toUtf8());

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
    // ── 诊断：记录 Answer 收到时刻 + 各阶段耗时 ────────────────────────────────
    m_answerReceivedTime = QDateTime::currentMSecsSinceEpoch();
    const int64_t t0 = m_connectStartTime > 0 ? m_connectStartTime : m_answerReceivedTime;
    const int64_t totalDelay = m_answerReceivedTime - t0;
    const int64_t offerDelay = (m_offerSentTime > 0) ? (m_answerReceivedTime - m_offerSentTime) : -1;
    qDebug() << "[Client][WebRTC] onSdpAnswerReceived() 开始 stream=" << m_stream << " reply=" << (void*)reply << " m_currentReply=" << (void*)m_currentReply;
    
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
    m_remoteSdp = answer;
    qDebug() << "[Client][WebRTC] processAnswer stream=" << m_stream << "开始 setRemoteDescription";
    
#ifdef ENABLE_WEBRTC_LIBDATACHANNEL
    try {
        // Pre-process SDP to ensure compatibility (Plan 3.2)
        QString processedAnswer = ensureSdpBundleGroup(ensureSdpHasMid(answer));
        
        if (m_peerConnection) {
            rtc::Description description(processedAnswer.toStdString(), rtc::Description::Type::Answer);
            m_peerConnection->setRemoteDescription(description);
            qDebug() << "[Client][WebRTC] setRemoteDescription 完成 stream=" << m_stream << "，等待 ICE 连接与 onTrack";
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

    // libdatachannel onMessage 回调在工作线程执行，FFmpeg AVCodecContext/SwsContext 非线程安全。
    // 通过 QueuedConnection 将数据拷贝传回主线程，彻底消除数据竞争，代价是一次 QByteArray 拷贝（~MTU）。
    m_videoTrack->onMessage([this](rtc::message_variant msg) {
        try {
            if (std::holds_alternative<rtc::binary>(msg)) {
                const auto &bin = std::get<rtc::binary>(msg);
                if (!m_h264Decoder || bin.empty()) return;
                QByteArray pkt(reinterpret_cast<const char *>(bin.data()), static_cast<qsizetype>(bin.size()));
                QMetaObject::invokeMethod(m_h264Decoder, [dec = m_h264Decoder, pkt = std::move(pkt), this]() {
                    // ── 诊断：RTP 包到达时记录，用于判断"收到包但没帧"vs"根本没收到包" ─
                    ++m_framesSinceLastStats;
                    try {
                        dec->feedRtp(reinterpret_cast<const uint8_t *>(pkt.constData()),
                                     static_cast<size_t>(pkt.size()));
                    } catch (const std::exception& e) {
                        qCritical() << "[Client][WebRTC][ERROR] feedRtp 异常 stream=" << m_stream
                                    << " pktSize=" << pkt.size() << " error=" << e.what();
                    } catch (...) {
                        qCritical() << "[Client][WebRTC][ERROR] feedRtp 未知异常 stream=" << m_stream
                                    << " pktSize=" << pkt.size();
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
                   " stream=" << m_stream << " frameId=" << frameId;
    }

    try {
        // ── 队列积压诊断：进入时计数，用于判断主线程是否被阻塞 ───────────────────
        ++m_framesPendingInQueue;
        const int64_t handlerEnterTime = QDateTime::currentMSecsSinceEpoch();

        if (image.isNull()) {
            qWarning() << "[Client][WebRTC][WARN] 收到空视频帧 stream=" << m_stream;
            --m_framesPendingInQueue;
            return;
        }

        m_videoFrameLogCount++;

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
            // 前 3 帧无条件 INFO，跨所有 stream，打在 emit 之前，
            // 用于在默认日志中即可确认「C++ emit → QML handler」不断链。
            if (m_videoFrameLogCount <= 3) {
                qInfo() << "[Client][WebRTC][emitReady] stream=" << m_stream
                         << " frame#=" << m_videoFrameLogCount
                         << " frameId=" << frameId
                         << " image=" << image.size() << " valid=" << !image.isNull()
                         << " w=" << image.width() << " h=" << image.height()
                         << " ★ 对比 QML onVideoFrameReady 日志 frameId 确认 emit→QML 链路";
            }
            // 带显式尺寸+frameId的信号：
            // - 尺寸参数解决 QImage 跨线程 QueuedConnection 到 QML 时 QVariant 包装导致
            //   image.width/height 方法调用返回 0 的 Qt 元对象边界问题。
            // - frameId 用于 C++ emit → QML handler 端到端追踪（必须与 emitDecoded 中的
            //   frameId 一致，日志中显示为 frameId=emitId/qmlId）。
            emit videoFrameReady(image, image.width(), image.height(), frameId);
            // ★★★ 关键诊断：emit 后直接检查是否有 QML 接收者 ★★★
            // 如果 receivers==0，说明 QML Connections.target 未绑定或 target 为 null
            // ★ 对比 QML console.log 输出确认 signal → QML handler 链路
            QObject* obj = this;
            int receivers = obj->receivers(SIGNAL(videoFrameReady(const QImage&, int, int, quint64)));
            if (m_videoFrameLogCount <= 5) {
                qInfo() << "[Client][WebRTC][emitDone] stream=" << m_stream << " frame#=" << m_videoFrameLogCount
                         << " frameId=" << frameId
                         << " emitCost=" << (QDateTime::currentMSecsSinceEpoch() - handlerEnterTime) << "ms"
                         << " signalReceivers=" << receivers
                         << " ★ signalReceivers>0=有接收者 | =0=QML Connections.target 可能为 null"
                         << " ★ 对比 QML console.log 输出确认 emit→QML 链路";
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

void WebRtcClient::updateStatus(const QString &status, bool connected)
{
    m_statusText = status;
    m_isConnected = connected;
    emit statusTextChanged(m_statusText);
    emit connectionStatusChanged(m_isConnected);
}
