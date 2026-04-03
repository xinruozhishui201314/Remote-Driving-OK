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
#include <QThread>
#include <QMetaMethod>

static const char kStreamFront[] = "cam_front";
static const char kStreamRear[]  = "cam_rear";
static const char kStreamLeft[]  = "cam_left";
static const char kStreamRight[] = "cam_right";

WebRtcStreamManager::WebRtcStreamManager(QObject *parent)
    : QObject(parent)
{
    m_front = new WebRtcClient(this);
    m_rear  = new WebRtcClient(this);
    m_left  = new WebRtcClient(this);
    m_right = new WebRtcClient(this);

    // ── ★★★ 端到端追踪：四路 WebRtcClient 创建完成，QML 即将可见 ★★★ ─────────────
    qInfo() << "[StreamManager][init] ★ WebRtcClient×4 创建完成 ★"
             << " front=" << (void*)m_front << " rear=" << (void*)m_rear
             << " left=" << (void*)m_left << " right=" << (void*)m_right
             << " ★ 对比 DrivingInterface.qml streamClient 绑定日志"
             << " ★ 对比 QML console.log webrtcStreamManager.frontClient 检查绑定是否正常";

    auto maybeEmit = [this]() { emit anyConnectedChanged(); };
    connect(m_front, &WebRtcClient::connectionStatusChanged, this, maybeEmit);
    connect(m_rear,  &WebRtcClient::connectionStatusChanged, this, maybeEmit);
    connect(m_left,  &WebRtcClient::connectionStatusChanged, this, maybeEmit);
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

QString WebRtcStreamManager::getStreamDebugInfo() const
{
    // ★★★ 诊断：供 QML console 调用，验证 webrtcStreamManager.frontClient 等属性是否可访问 ★★★
    // 若 QML 中出现 "webrtcStreamManager.getStreamDebugInfo is not a function" → QML 未识别 Q_INVOKABLE
    // 若返回空字符串 → this 指针或成员访问异常
    QString info;
    QTextStream ts(&info);
    ts << "StreamManager Debug:\n";
    ts << "  front=" << (void*)m_front << " connected=" << (m_front ? m_front->isConnected() : -1) << "\n";
    ts << "  rear=" << (void*)m_rear << " connected=" << (m_rear ? m_rear->isConnected() : -1) << "\n";
    ts << "  left=" << (void*)m_left << " connected=" << (m_left ? m_left->isConnected() : -1) << "\n";
    ts << "  right=" << (void*)m_right << " connected=" << (m_right ? m_right->isConnected() : -1) << "\n";
    ts << "  vin=" << m_vin << " base=" << m_currentBase << "\n";

    // 检查 frontClient 的 videoFrameReady 信号接收者数（关键诊断）
    // 【消除 overload】：1 参数版本已移除，4 参数版本为唯一版本，不再有歧义
    if (m_front) {
        int receivers_4param = m_front->receiverCountVideoFrameReady();
        ts << "  front videoFrameReady receivers:\n";
        ts << "    4-param=" << receivers_4param << " (★ >0=QML 有接收 | 0=signal静默丢弃 ★)\n";
        ts << "  front streamUrl=" << m_front->streamUrl() << "\n";
        ts << "  front statusText=" << m_front->statusText() << "\n";
    }
    return info;
}

int WebRtcStreamManager::getQmlSignalReceiverCount() const
{
    // ★★★ 关键诊断：直接检查 QML 是否连接到 videoFrameReady 信号 ★★★
    // 返回值：
    //   0 = QML 完全没有连接到信号（根因！）
    //   >0 = QML 有连接（理论上视频应该显示）
    // 注意：只在 front 上检查，因为四路都走同样逻辑
    if (!m_front) return -1;
    return m_front->receiverCountVideoFrameReady();
}

int WebRtcStreamManager::getFrontSignalReceiverCount() const
{
    // ★★★ 诊断：各路独立接收者计数 ★★★
    // 用于判断"哪一路"没有 QML 连接，精准定位问题
    if (!m_front) return -1;
    int rc = m_front->receiverCountVideoFrameReady();
    qInfo() << "[StreamManager][Diag] getFrontSignalReceiverCount()=" << rc
             << " ★ 0=该路 QML Connections 未连接该信号，>0=有连接"
             << " front=" << (void*)m_front << " stream=" << (m_front ? m_front->property("m_stream").toString() : "N/A");
    return rc;
}

int WebRtcStreamManager::getRearSignalReceiverCount() const
{
    if (!m_rear) return -1;
    int rc = m_rear->receiverCountVideoFrameReady();
    qInfo() << "[StreamManager][Diag] getRearSignalReceiverCount()=" << rc
             << " ★ 0=该路 QML Connections 未连接，>0=有连接"
             << " rear=" << (void*)m_rear;
    return rc;
}

int WebRtcStreamManager::getLeftSignalReceiverCount() const
{
    if (!m_left) return -1;
    int rc = m_left->receiverCountVideoFrameReady();
    qInfo() << "[StreamManager][Diag] getLeftSignalReceiverCount()=" << rc
             << " ★ 0=该路 QML Connections 未连接，>0=有连接"
             << " left=" << (void*)m_left;
    return rc;
}

int WebRtcStreamManager::getRightSignalReceiverCount() const
{
    if (!m_right) return -1;
    int rc = m_right->receiverCountVideoFrameReady();
    qInfo() << "[StreamManager][Diag] getRightSignalReceiverCount()=" << rc
             << " ★ 0=该路 QML Connections 未连接，>0=有连接"
             << " right=" << (void*)m_right;
    return rc;
}

QString WebRtcStreamManager::getStreamSignalMetaInfo() const
{
    // ★★★ 诊断：暴露每个 WebRtcClient 的 videoFrameReady 信号元数据 ★★★
    // 用于在 QML 诊断中确认：
    // 1. C++ 信号是否正确声明（meta 存在）
    // 2. 参数数量是否匹配 QML Connections 中的 function 签名
    // 3. 参数类型名是否与 QML 看到的类型名一致
    QString info;
    QTextStream ts(&info);

    auto dumpClient = [&](const char *name, WebRtcClient *client) {
        if (!client) {
            ts << name << "=nullptr\n";
            return;
        }
        ts << name << "=ptr:" << QString::number((quintptr)client, 16) << " ";
        ts << "meta=" << client->videoFrameReadySignalMeta() << " ";
        ts << "rc=" << client->receiverCountVideoFrameReady() << "\n";
    };

    dumpClient("front", m_front);
    dumpClient("rear",  m_rear);
    dumpClient("left",  m_left);
    dumpClient("right", m_right);

    // 同时打印 Qt 元对象信息：确认 WebRtcClient 本身是否正确注册
    if (m_front) {
        const QMetaObject *mo = m_front->metaObject();
        ts << "front_className=" << mo->className() << " methodCount=" << mo->methodCount() << "\n";
        // 列出所有 signal
        ts << "front_signals: ";
        for (int i = mo->methodOffset(); i < mo->methodCount(); ++i) {
            QMetaMethod m = mo->method(i);
            if (m.methodType() == QMetaMethod::Signal) {
                ts << "sig" << i << "=" << QString::fromLatin1(m.methodSignature()) << " ";
            }
        }
        ts << "\n";
    }

    qInfo() << "[StreamManager][Diag] getStreamSignalMetaInfo:\n" << info;
    return info;
}

void WebRtcStreamManager::dumpStreamInfo() const
{
    // 直接打印到日志，用于运行时诊断
    qInfo() << "[StreamManager][Diag] dumpStreamInfo:";
    qInfo() << "  front=" << (void*)m_front << " connected=" << (m_front ? m_front->isConnected() : -1);
    qInfo() << "  rear=" << (void*)m_rear << " connected=" << (m_rear ? m_rear->isConnected() : -1);
    qInfo() << "  left=" << (void*)m_left << " connected=" << (m_left ? m_left->isConnected() : -1);
    qInfo() << "  right=" << (void*)m_right << " connected=" << (m_right ? m_right->isConnected() : -1);

    if (m_front) {
        int rc = m_front->receiverCountVideoFrameReady();
        qInfo() << "  front videoFrameReady receivers=" << rc
                 << " ★ 0=未连接QML，>0=有QML连接 ★";
        qInfo() << "  front streamUrl=" << m_front->streamUrl();
    }
}

void WebRtcStreamManager::forceRefreshAllRenderers()
{
    // ── 强制刷新机制（方案1）─────────────────────────────────────────────
    // 根因：Qt Scene Graph 在 VehicleSelectionDialog 显示期间可能阻塞渲染线程，
    // 导致 deliverFrame 收到帧但 updatePaintNode 不被调用。
    // 修复：在对话框关闭时强制刷新所有 VideoRenderer。
    // 实现：通过 WebRtcClient::forceRefresh() 方法访问其关联的 VideoRenderer，
    // 然后调用 VideoRenderer::forceRefresh()。
    const int64_t now = QDateTime::currentMSecsSinceEpoch();
    qInfo() << "[StreamManager] ★★★ forceRefreshAllRenderers 被调用 ★★★"
            << " now=" << now
            << " front=" << (void*)m_front << " rear=" << (void*)m_rear
            << " left=" << (void*)m_left << " right=" << (void*)m_right;

    // 遍历四路 WebRtcClient，调用其 forceRefresh 方法
    auto refreshClient = [&](const char* name, WebRtcClient* client) {
        if (client) {
            try {
                client->forceRefresh();
                qInfo() << "[StreamManager] forceRefresh:" << name << " 已调用";
            } catch (const std::exception& e) {
                qWarning() << "[StreamManager] forceRefresh:" << name << " 异常:" << e.what();
            } catch (...) {
                qWarning() << "[StreamManager] forceRefresh:" << name << " 未知异常";
            }
        } else {
            qWarning() << "[StreamManager] forceRefresh:" << name << " 为 nullptr，跳过";
        }
    };

    refreshClient("front", m_front);
    refreshClient("rear", m_rear);
    refreshClient("left", m_left);
    refreshClient("right", m_right);

    const int64_t doneTime = QDateTime::currentMSecsSinceEpoch();
    qInfo() << "[StreamManager] forceRefreshAllRenderers 完成，耗时=" << (doneTime - now) << "ms";
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
                << " vin=" << (m_vin.isEmpty() ? "(empty)" : m_vin)
                << " ★ 对比 QML connectFourStreams 调用日志"
                << " ★ 对比 PeerConnection state logs"
                << " ★ 对比 DrivingInterface.qml streamClient 绑定日志";

        // ── 诊断：Q_PROPERTY getter vs 成员指针一致性验证 ─────────────────────────
        // 若两者不一致 → Q_PROPERTY 绑定异常或对象被替换（极端情况下 QML 可能读到空指针）
        qInfo() << "[StreamManager][Diag] Q_PROPERTY vs 成员指针一致性验证:"
                 << " frontClient()=" << (void*)frontClient()
                 << " m_front=" << (void*)m_front
                 << " rearClient()=" << (void*)rearClient()
                 << " m_rear=" << (void*)m_rear
                 << " leftClient()=" << (void*)leftClient()
                 << " m_left=" << (void*)m_left
                 << " rightClient()=" << (void*)rightClient()
                 << " m_right=" << (void*)m_right
                 << " ★ 若指针不一致 → Q_PROPERTY 绑定异常，极端情况下 QML 可能读到空指针";

        // ── 诊断：下发前各路 videoFrameReady 接收者计数 ─────────────────────────
        // 对比下发后的 rc，可判断 QML 是否在 connectFourStreams 调用前就已经连接成功
        qInfo() << "[StreamManager][Diag] 下发前各路 videoFrameReady 接收者:"
                 << " front-rc=" << (m_front ? m_front->receiverCountVideoFrameReady() : -1)
                 << " rear-rc="  << (m_rear  ? m_rear->receiverCountVideoFrameReady()  : -1)
                 << " left-rc="  << (m_left  ? m_left->receiverCountVideoFrameReady()  : -1)
                 << " right-rc=" << (m_right ? m_right->receiverCountVideoFrameReady() : -1)
                 << " ★ rc=0 意味着 QML Connections 未成功绑定或语法错误 ★"
                 << " ★ rc>0 说明 QML 在 connectFourStreams 调用前已连接，后续若 rc 变化则说明信号有重绑定 ★";

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
        const int64_t sendTime = QDateTime::currentMSecsSinceEpoch();
        // ── 诊断：connectFourStreams 下发前，先 dump 信号接收者计数 ───────────
        qInfo() << "[StreamManager][Diag] connectFourStreams 下发前检查:"
                 << " front=" << (void*)m_front << " rear=" << (void*)m_rear
                 << " left=" << (void*)m_left << " right=" << (void*)m_right;

        if (m_front) {
            try {
                m_front->connectToStream(base, app, sFront);
                // ── 诊断：下发后检查 videoFrameReady 信号接收者数 ───────────────
                int rc = m_front->receiverCountVideoFrameReady();
                qInfo() << "[StreamManager][Diag] front.connectToStream 下发后:"
                         << " stream=" << sFront
                         << " videoFrameReady receivers=" << rc
                         << " ★ rc>0=QML有连接 | rc=0=信号静默丢弃，对比 QML console 日志确认";
            }
            catch (const std::exception& e) { qCritical() << "[StreamManager][ERROR] front.connectToStream 异常:" << e.what() << "base=" << base << " stream=" << sFront; }
            catch (...) { qCritical() << "[StreamManager][ERROR] front.connectToStream 未知异常 base=" << base << " stream=" << sFront; }
        } else { qWarning() << "[StreamManager][WARN] m_front 为 nullptr，跳过"; }

        if (m_rear) {
            try {
                m_rear->connectToStream(base, app, sRear);
                int rc = m_rear->receiverCountVideoFrameReady();
                qInfo() << "[StreamManager][Diag] rear.connectToStream 下发后:"
                         << " stream=" << sRear
                         << " videoFrameReady receivers=" << rc;
            }
            catch (const std::exception& e) { qCritical() << "[StreamManager][ERROR] rear.connectToStream 异常:" << e.what() << "base=" << base << " stream=" << sRear; }
            catch (...) { qCritical() << "[StreamManager][ERROR] rear.connectToStream 未知异常 base=" << base << " stream=" << sRear; }
        } else { qWarning() << "[StreamManager][WARN] m_rear 为 nullptr，跳过"; }

        if (m_left) {
            try {
                m_left->connectToStream(base, app, sLeft);
                int rc = m_left->receiverCountVideoFrameReady();
                qInfo() << "[StreamManager][Diag] left.connectToStream 下发后:"
                         << " stream=" << sLeft
                         << " videoFrameReady receivers=" << rc;
            }
            catch (const std::exception& e) { qCritical() << "[StreamManager][ERROR] left.connectToStream 异常:" << e.what() << "base=" << base << " stream=" << sLeft; }
            catch (...) { qCritical() << "[StreamManager][ERROR] left.connectToStream 未知异常 base=" << base << " stream=" << sLeft; }
        } else { qWarning() << "[StreamManager][WARN] m_left 为 nullptr，跳过"; }

        if (m_right) {
            try {
                m_right->connectToStream(base, app, sRight);
                int rc = m_right->receiverCountVideoFrameReady();
                qInfo() << "[StreamManager][Diag] right.connectToStream 下发后:"
                         << " stream=" << sRight
                         << " videoFrameReady receivers=" << rc;
            }
            catch (const std::exception& e) { qCritical() << "[StreamManager][ERROR] right.connectToStream 异常:" << e.what() << "base=" << base << " stream=" << sRight; }
            catch (...) { qCritical() << "[StreamManager][ERROR] right.connectToStream 未知异常 base=" << base << " stream=" << sRight; }
        } else { qWarning() << "[StreamManager][WARN] m_right 为 nullptr，跳过"; }

        const int64_t doneTime = QDateTime::currentMSecsSinceEpoch();
        qInfo() << "[Client][WebRTC][StreamManager] connectFourStreams 已下发四路"
                << " base=" << base << " app=" << app
                << " totalDuration=" << (doneTime - sendTime) << "ms";
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
    // ── ★★★ 端到端追踪：checkZlmStreamRegistration 进入 ★★★ ─────────────────
    const int64_t funcEnterTime = QDateTime::currentMSecsSinceEpoch();
    qInfo() << "[StreamManager][ZlmPoll] ★★★ checkZlmStreamRegistration ENTER ★★★"
            << " vin=" << (m_vin.isEmpty() ? "(empty)" : m_vin)
            << " base=" << (m_currentBase.isEmpty() ? "(empty)" : m_currentBase)
            << " enterTime=" << funcEnterTime;

    // 无 base URL 时跳过（未连接）
    if (m_currentBase.isEmpty()) {
        qDebug() << "[StreamManager][ZlmPoll] m_currentBase 为空，跳过 ZLM 流检查";
        return;
    }

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
    QNetworkReply *reply = nam.get(QNetworkRequest(url));
    // ── 诊断：验证 QNAM 和 reply 的线程亲缘性 ─────────────────────────────────
    qInfo() << "[StreamManager][ZlmPoll] QNAM thread=" << nam.thread()
             << " reply thread=" << reply->thread()
             << " this thread=" << this->thread()
             << " currentThread=" << QThread::currentThread()
             << " ★ 若线程不一致 → QNetworkAccessManager 跨线程使用，Qt 可能自动转发但有性能开销 ★";

    QObject::connect(reply, &QNetworkReply::finished, this, [this, reply, funcEnterTime]() {
        // ── 诊断：验证回调执行线程（QNetworkReply 在主线程发射信号，lambda 在主线程执行）──
        // 若此断言失败，说明回调跑到了错误的线程，可能访问已销毁的对象
        Q_ASSERT_X(QThread::currentThread() == this->thread(),
                   "checkZlmStreamRegistration callback",
                   "Callback in wrong thread — potential race condition!");
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
        const int64_t responseTime = QDateTime::currentMSecsSinceEpoch();
        const int64_t requestDuration = responseTime - funcEnterTime;
        qInfo() << "[StreamManager][ZlmPoll] ★★★ ZLM API 响应 ★★★"
                << " url=" << reply->url().toString()
                << " httpStatus=" << reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()
                << " dataSize=" << data.size() << " bytes"
                << " requestDuration=" << requestDuration << "ms"
                << "（>2s 说明 ZLM 负载高或网络慢）";

        QJsonParseError parseErr;
        QJsonDocument doc = QJsonDocument::fromJson(data, &parseErr);
        if (parseErr.error != QJsonParseError::NoError) {
            qWarning() << "[StreamManager][ZlmPoll][ERROR] JSON 解析失败:" << parseErr.errorString()
                       << " body=" << QString::fromUtf8(data).left(200);
            return;
        }

        qInfo() << "[StreamManager][ZlmPoll] JSON 解析成功 rootKeys=" << doc.object().keys()
                 << " dataCount=" << doc.object().value("data").toArray().size();

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

        // ── 诊断：解析完成，准备输出 ZLM 流状态汇总 ─────────────────────────────
        const int64_t parseDoneTime = QDateTime::currentMSecsSinceEpoch();
        const int64_t parseDuration = parseDoneTime - responseTime;
        qInfo() << "[StreamManager][ZlmPoll] 解析耗时=" << parseDuration << "ms"
                 << " foundStreams=" << foundStreams.size()
                 << " 四路流名:";
        for (const QString &s : foundStreams) {
            qInfo() << "[StreamManager][ZlmPoll]   ZLM流=" << s;
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
            const int64_t totalDuration = QDateTime::currentMSecsSinceEpoch() - funcEnterTime;
            if (anyPresent) {
                qInfo() << "[StreamManager][ZlmPoll] ★★★ ZLM 推流在册（四路检查完成）★★★"
                         << " vin=" << (m_vin.isEmpty() ? "(empty)" : m_vin)
                         << " summary=" << summary
                         << " totalDuration=" << totalDuration << "ms";
            } else {
                qWarning() << "[StreamManager][ZlmPoll][FATAL] ★★★ ZLM 推流失册！carla-bridge 断连或未收到 MQTT start_stream ★★★"
                           << " vin=" << (m_vin.isEmpty() ? "(empty)" : m_vin)
                           << " summary=" << summary
                           << " found=" << foundStreams
                           << " totalDuration=" << totalDuration << "ms";
            }
        }
    });
}
