#include "webrtcstreammanager.h"
#include <QUrl>
#include <QUrlQuery>
#include <QJsonParseError>
#include <QDebug>
#include <QDateTime>
#include <QProcessEnvironment>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTimer>

static const char kStreamFront[] = "cam_front";
static const char kStreamRear[]  = "cam_rear";
static const char kStreamLeft[]  = "cam_left";
static const char kStreamRight[] = "cam_right";

WebRtcStreamManager::WebRtcStreamManager(QObject *parent)
    : QObject(parent)
{
    m_front = new WebRtcClient(this);
    m_rear = new WebRtcClient(this);
    m_left = new WebRtcClient(this);
    m_right = new WebRtcClient(this);

    auto maybeEmit = [this]() { emit anyConnectedChanged(); };
    connect(m_front, &WebRtcClient::connectionStatusChanged, this, maybeEmit);
    connect(m_rear, &WebRtcClient::connectionStatusChanged, this, maybeEmit);
    connect(m_left, &WebRtcClient::connectionStatusChanged, this, maybeEmit);
    connect(m_right, &WebRtcClient::connectionStatusChanged, this, maybeEmit);

    // ── 诊断：四路状态变更时打印时间戳 + deltaMs ─────────────────────────────────
    connect(this, &WebRtcStreamManager::anyConnectedChanged, this, [this]() {
        static int64_t s_lastTimestamp = 0;
        const int64_t now = QDateTime::currentMSecsSinceEpoch();
        const int64_t delta = s_lastTimestamp > 0 ? (now - s_lastTimestamp) : -1;
        s_lastTimestamp = now;
        const bool f = m_front && m_front->isConnected();
        const bool rear = m_rear && m_rear->isConnected();
        const bool l = m_left && m_left->isConnected();
        const bool r = m_right && m_right->isConnected();
        qInfo().noquote() << "[StreamManager][Diag] anyConnected=" << anyConnected()
                          << " f=" << f << " rear=" << rear << " l=" << l << " r=" << r
                          << " deltaMs=" << delta
                          << " now=" << now << "【delta>0时说明四路在同ms内cascade断开了】";
    });

    // ── 诊断：周期性查询 ZLM API，验证推流是否在册 ───────────────────────────────
    // 若 ZLM getMediaList 中无 {vin}_cam_* 流，说明推流断连，客户端断流是正常行为
    // 若 ZLM getMediaList 中有流但客户端断流，说明 RTCP/ICE 问题
    m_zlmPollTimer = new QTimer(this);
    m_zlmPollTimer->setInterval(15000);  // 每 15s 查一次，避免频繁
    connect(m_zlmPollTimer, &QTimer::timeout, this, [this]() {
        try {
            checkZlmStreamRegistration();
        } catch (const std::exception& e) {
            qCritical() << "[StreamManager][Timer][ERROR] ZLM 流状态轮询定时器异常: stream=" << m_vin
                        << " error=" << e.what();
        } catch (...) {
            qCritical() << "[StreamManager][Timer][ERROR] ZLM 流状态轮询定时器未知异常: stream=" << m_vin;
        }
    });
    m_zlmPollTimer->start();
    qInfo() << "[StreamManager][Diag] ZLM 流状态轮询已启动，周期=15s";
}

WebRtcStreamManager::~WebRtcStreamManager()
{
    disconnectAll();
    if (m_zlmPollTimer) {
        m_zlmPollTimer->stop();
        m_zlmPollTimer->deleteLater();
        m_zlmPollTimer = nullptr;
    }
}

bool WebRtcStreamManager::anyConnected() const
{
    return (m_front && m_front->isConnected()) ||
           (m_rear && m_rear->isConnected()) ||
           (m_left && m_left->isConnected()) ||
           (m_right && m_right->isConnected());
}

QString WebRtcStreamManager::baseUrlFromWhep(const QString &whepUrl) const
{
    if (whepUrl.isEmpty()) return QString();
    // whep://host:80/index/api/webrtc?app=teleop&stream=xxx&type=play -> http://host:80
    QUrl u(whepUrl);
    if (u.scheme() == QStringLiteral("whep"))
        u.setScheme(QStringLiteral("http"));
    if (!u.isValid() || u.host().isEmpty()) return QString();
    QString base = QStringLiteral("%1://%2").arg(u.scheme().isEmpty() ? QStringLiteral("http") : u.scheme(),
                                                  u.host());
    if (u.port() > 0)
        base += QStringLiteral(":%1").arg(u.port());
    return base;
}

QString WebRtcStreamManager::appFromWhep(const QString &whepUrl) const
{
    if (whepUrl.isEmpty()) return m_app;
    QUrl u(whepUrl);
    QUrlQuery q(u.query());
    QString a = q.queryItemValue(QStringLiteral("app"));
    return a.isEmpty() ? m_app : a;
}

QString WebRtcStreamManager::resolveBaseUrl(const QString &whepUrl) const
{
    if (!whepUrl.isEmpty()) {
        return baseUrlFromWhep(whepUrl);
    }
    return QProcessEnvironment::systemEnvironment().value(QStringLiteral("ZLM_VIDEO_URL"));
}

void WebRtcStreamManager::connectFourStreams(const QString &whepUrl)
{
    try {
        const QString resolvedBase = resolveBaseUrl(whepUrl);
        qInfo() << "[Client][WebRTC][StreamManager] connectFourStreams 开始"
                << " whepUrlEmpty=" << whepUrl.isEmpty()
                << " resolvedBase=" << resolvedBase
                << " currentBase=" << m_currentBase
                << " anyConnected=" << anyConnected()
                << " vin=" << (m_vin.isEmpty() ? "(empty)" : m_vin);

        // 若 base URL 不变且已有连接，跳过断连重连，避免约 10s 无视频窗口
        if (!resolvedBase.isEmpty() && resolvedBase == m_currentBase && anyConnected()) {
            qInfo() << "[Client][WebRTC][StreamManager] base URL 不变且已有连接，跳过断连重连"
                    << " streams 保持原状";
            return;
        }

        // base 变更或无连接：断开旧连接
        if (anyConnected()) {
            qInfo() << "[Client][WebRTC][StreamManager] base 变更或无连接，执行断连重建"
                    << " oldBase=" << m_currentBase << " newBase=" << resolvedBase;
        }
        m_currentBase = resolvedBase;

        // 统一在 try 块内执行断连，确保异常时仍能打日志
        try {
            disconnectAll();
        } catch (const std::exception& e) {
            qWarning() << "[StreamManager] disconnectAll 异常:" << e.what();
        } catch (...) {
            qWarning() << "[StreamManager] disconnectAll 未知异常";
        }

        QString base = resolvedBase;
        if (base.isEmpty()) {
            qCritical() << "[CLIENT][WebRTC] ZLM_VIDEO_URL 未设置且 whepUrl 为空，无法连接视频流。"
                           "请设置环境变量 ZLM_VIDEO_URL=http://<zlm-host>:80 或在创建会话时传入 whepUrl。";
            return;
        }
        QString app = appFromWhep(whepUrl);

        // 多车隔离：流名加 VIN 前缀，格式 {vin}_cam_front；VIN 为空时退化为 cam_front
        QString vinPrefix = m_vin.isEmpty() ? QString() : (m_vin + QStringLiteral("_"));
        QString sFront = vinPrefix + QString::fromUtf8(kStreamFront);
        QString sRear  = vinPrefix + QString::fromUtf8(kStreamRear);
        QString sLeft  = vinPrefix + QString::fromUtf8(kStreamLeft);
        QString sRight = vinPrefix + QString::fromUtf8(kStreamRight);

        // ── 诊断：打印完整四路 URL，便于在 ZLM 日志中搜索确认每个流是否被拉 ─────────
        QString fUrl = QString("%1/index/api/webrtc?app=%2&stream=%3&type=play").arg(base).arg(app).arg(sFront);
        QString rUrl = QString("%1/index/api/webrtc?app=%2&stream=%3&type=play").arg(base).arg(app).arg(sRear);
        QString lUrl = QString("%1/index/api/webrtc?app=%2&stream=%3&type=play").arg(base).arg(app).arg(sLeft);
        QString rtUrl = QString("%1/index/api/webrtc?app=%2&stream=%3&type=play").arg(base).arg(app).arg(sRight);
        qInfo() << "[Client][WebRTC][StreamManager][Diag] 四路 URL:";
        qInfo() << "[StreamManager]   front:" << fUrl;
        qInfo() << "[StreamManager]   rear: " << rUrl;
        qInfo() << "[StreamManager]   left: " << lUrl;
        qInfo() << "[StreamManager]   right:" << rtUrl;
        qInfo() << "[Client][WebRTC][StreamManager] 连接四路流 base=" << base << " app=" << app
                << " streams=" << sFront << "," << sRear << "," << sLeft << "," << sRight;

        // 逐路下发并捕获异常，防止一路崩导致其他路未下发
        if (m_front) {
            try { m_front->connectToStream(base, app, sFront); }
            catch (const std::exception& e) { qCritical() << "[StreamManager][ERROR] front.connectToStream 异常:" << e.what() << "base=" << base << " stream=" << sFront; }
            catch (...) { qCritical() << "[StreamManager][ERROR] front.connectToStream 未知异常 base=" << base << " stream=" << sFront; }
        } else { qWarning() << "[StreamManager][WARN] m_front 为 nullptr，跳过"; }

        if (m_rear) {
            try { m_rear->connectToStream(base, app, sRear); }
            catch (const std::exception& e) { qCritical() << "[StreamManager][ERROR] rear.connectToStream 异常:" << e.what() << "base=" << base << " stream=" << sRear; }
            catch (...) { qCritical() << "[StreamManager][ERROR] rear.connectToStream 未知异常 base=" << base << " stream=" << sRear; }
        } else { qWarning() << "[StreamManager][WARN] m_rear 为 nullptr，跳过"; }

        if (m_left) {
            try { m_left->connectToStream(base, app, sLeft); }
            catch (const std::exception& e) { qCritical() << "[StreamManager][ERROR] left.connectToStream 异常:" << e.what() << "base=" << base << " stream=" << sLeft; }
            catch (...) { qCritical() << "[StreamManager][ERROR] left.connectToStream 未知异常 base=" << base << " stream=" << sLeft; }
        } else { qWarning() << "[StreamManager][WARN] m_left 为 nullptr，跳过"; }

        if (m_right) {
            try { m_right->connectToStream(base, app, sRight); }
            catch (const std::exception& e) { qCritical() << "[StreamManager][ERROR] right.connectToStream 异常:" << e.what() << "base=" << base << " stream=" << sRight; }
            catch (...) { qCritical() << "[StreamManager][ERROR] right.connectToStream 未知异常 base=" << base << " stream=" << sRight; }
        } else { qWarning() << "[StreamManager][WARN] m_right 为 nullptr，跳过"; }

        qInfo() << "[Client][WebRTC][StreamManager] connectFourStreams 已下发四路 connect base=" << base << " app=" << app;
    } catch (const std::exception& e) {
        qCritical() << "[StreamManager][ERROR] connectFourStreams 异常:"
                    << " vin=" << (m_vin.isEmpty() ? "(empty)" : m_vin)
                    << " error=" << e.what();
    } catch (...) {
        qCritical() << "[StreamManager][ERROR] connectFourStreams 未知异常 vin=" << (m_vin.isEmpty() ? "(empty)" : m_vin);
    }
}

void WebRtcStreamManager::disconnectAll()
{
    qInfo() << "[Client][WebRTC][StreamManager] disconnectAll 四路";
    try {
        if (m_front) { try { m_front->disconnect(); } catch (...) { qWarning() << "[StreamManager][WARN] front->disconnect() 异常"; } }
        if (m_rear)  { try { m_rear->disconnect();  } catch (...) { qWarning() << "[StreamManager][WARN] rear->disconnect() 异常"; } }
        if (m_left)  { try { m_left->disconnect();  } catch (...) { qWarning() << "[StreamManager][WARN] left->disconnect() 异常"; } }
        if (m_right) { try { m_right->disconnect(); } catch (...) { qWarning() << "[StreamManager][WARN] right->disconnect() 异常"; } }
    } catch (const std::exception& e) {
        qCritical() << "[StreamManager][ERROR] disconnectAll 异常:" << e.what();
    } catch (...) {
        qCritical() << "[StreamManager][ERROR] disconnectAll 未知异常";
    }
}

void WebRtcStreamManager::checkZlmStreamRegistration()
{
    // 无 base URL 时跳过（未连接）
    if (m_currentBase.isEmpty()) return;

    const QString apiPath = m_currentBase + "/index/api/getMediaList";
    // ZLM API secret 从环境变量读取（与 docker-compose.yml 中 ZLM_API_SECRET 一致）
    // 注意：若 docker-compose.yml 挂载了 deploy/zlm/config.ini 且 secret 不同，ZLM 会生成随机 secret，
    // 导致 getMediaList 等 API 调用失败；此时请确保 ZLM_API_SECRET 环境变量与 ZLM 实际使用的一致。
    const QString secret = QProcessEnvironment::systemEnvironment().value(
        QStringLiteral("ZLM_API_SECRET"),
        QStringLiteral("j9uH7zT0mawXzTrvqRythA48QvZ8rO2Y"));  // ZLM volume 默认
    const QString url = apiPath + "?secret=" + secret;

    // 使用函数内 static QNAM，避免频繁 new/delete，且在主线程调用是安全的
    static QNetworkAccessManager nam(nullptr);
    QNetworkReply *reply = nam.get(QNetworkRequest(QUrl(url)));
    if (!reply) {
        qWarning() << "[StreamManager][ZlmPoll][ERROR] QNetworkAccessManager get 失败，跳过本次轮询";
        return;
    }

    QObject::connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        try {
            reply->deleteLater();
        } catch (...) {
            qWarning() << "[StreamManager][ZlmPoll][WARN] reply->deleteLater() 异常";
        }

        if (reply->error() != QNetworkReply::NoError) {
            qWarning() << "[StreamManager][ZlmPoll][ERROR] ZLM API 请求失败:" << reply->errorString()
                       << " url=" << reply->url().toString();
            return;
        }

        QByteArray data = reply->readAll();
        QJsonParseError parseErr;
        QJsonDocument doc = QJsonDocument::fromJson(data, &parseErr);
        if (parseErr.error != QJsonParseError::NoError) {
            qWarning() << "[StreamManager][ZlmPoll][ERROR] ZLM API 返回非 JSON，url=" << reply->url().toString()
                       << " body=" << QString::fromUtf8(data).left(200);
            return;
        }

        // 提取所有流名
        QStringList foundStreams;
        QJsonArray mediaList = doc.object().value("data").toArray();
        for (const QJsonValue &v : mediaList) {
            try {
                QString stream = v.toObject().value("stream").toString();
                QString schema = v.toObject().value("schema").toString();
                qint64 aliveSec = v.toObject().value("aliveSecond").toVariant().toLongLong();
                if (!stream.isEmpty()) {
                    foundStreams << stream;
                    // 每 5 分钟打一条 alive 日志（stream+schema+alive），平时静默
                    static QHash<QString, qint64> s_lastAliveLog;
                    qint64 lastLog = s_lastAliveLog.value(stream, 0);
                    if (QDateTime::currentMSecsSinceEpoch() - lastLog > 300000) {
                        qInfo() << "[StreamManager][ZlmPoll] ZLM 流 alive stream=" << stream
                                << " schema=" << schema << " aliveSec=" << aliveSec;
                        s_lastAliveLog[stream] = QDateTime::currentMSecsSinceEpoch();
                    }
                }
            } catch (const std::exception& e) {
                qWarning() << "[StreamManager][ZlmPoll][ERROR] 解析流条目异常:" << e.what();
            } catch (...) {
                qWarning() << "[StreamManager][ZlmPoll][ERROR] 解析流条目未知异常";
            }
        }

        // 检查四路相机流是否在册
        QStringList camStreams;
        for (const QString &s : {
            (m_vin.isEmpty() ? QString() : m_vin + "_") + "cam_front",
            (m_vin.isEmpty() ? QString() : m_vin + "_") + "cam_rear",
            (m_vin.isEmpty() ? QString() : m_vin + "_") + "cam_left",
            (m_vin.isEmpty() ? QString() : m_vin + "_") + "cam_right"
        }) {
            camStreams << (foundStreams.contains(s) ? "✓" + s : "✗" + s);
        }
        QString summary = camStreams.join(" ");
        // 仅在状态变化时打日志，避免刷屏
        if (summary != m_lastZlmStreamsSeen) {
            m_lastZlmStreamsSeen = summary;
            // 判断四路中至少有一路在册（允许部分相机断）
            bool anyPresent = false;
            for (const QString &expected : {
                (m_vin.isEmpty() ? QString() : m_vin + "_") + "cam_front",
                (m_vin.isEmpty() ? QString() : m_vin + "_") + "cam_rear",
                (m_vin.isEmpty() ? QString() : m_vin + "_") + "cam_left",
                (m_vin.isEmpty() ? QString() : m_vin + "_") + "cam_right"
            }) {
                if (foundStreams.contains(expected)) { anyPresent = true; break; }
            }
            if (anyPresent) {
                qInfo() << "[StreamManager][ZlmPoll] ZLM 推流在册:" << summary;
            } else {
                qWarning() << "[StreamManager][ZlmPoll][ERROR] ZLM 推流失册! 可能 carla-bridge 断连或未收到 MQTT start_stream:" << summary
                           << " found=" << foundStreams;
            }
        }
    });
}
