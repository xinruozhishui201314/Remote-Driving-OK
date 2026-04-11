#include "webrtcclient.h"

#include <QDateTime>
#include <QDebug>
#include <QElapsedTimer>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaMethod>
#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <QNetworkRequest>
#include <QPointer>
#include <QQuickWindow>
#include <QQmlProperty>
#include <QRandomGenerator>
#include <QSet>
#include <QSize>
#include <QString>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QVideoFrame>

#include <algorithm>
#include <atomic>
#include <mutex>
#if defined(ENABLE_WEBRTC_LIBDATACHANNEL) && defined(ENABLE_FFMPEG)
#include "h264decoder.h"
#include "infrastructure/media/IHardwareDecoder.h"  // VideoFrame::nextLifecycleId()（与 Qt QVideoFrame 区分）
#include "media/ClientMediaBudget.h"
#include "media/RtcpCompoundParser.h"
#include "media/RtpIngressTypes.h"
#include "media/RtpPacketSpscQueue.h"
#include "media/RtpStreamClockContext.h"
#include "media/ClientVideoDiagCache.h"
#include "media/ClientVideoStreamHealth.h"
#include "media/CpuVideoRgba8888Frame.h"
#include "media/VideoFrameEvidenceDiag.h"
#include "media/VideoFrameFingerprintCache.h"
#include "ui/VideoFramePresentWorker.h"
#include "core/eventbus.h"
#endif

#include "core/metricscollector.h"

#ifdef ENABLE_WEBRTC_LIBDATACHANNEL
#include <rtc/rtc.hpp>
#endif

/** WebRTC 客户端：解码帧 → QVideoSink::setVideoFrame；并 emit videoFrameReady(宽高,id) 供 QML
 * 占位/诊断（无 QImage 封送）。 */

#if defined(ENABLE_WEBRTC_LIBDATACHANNEL) && defined(ENABLE_FFMPEG)
namespace {
/**
 * Track::onMessage 二进制内容分类（RFC 3550 RTP vs RTCP），便于与 H264Decoder 侧丢弃日志对照。
 * 全量日志：CLIENT_WEBRTC_PACKET_CLASSIFY_TRACE=1
 */
void logWebRtcIngressPacketClassify(const QString &stream, const uint8_t *p, size_t len,
                                    quint64 lifecycleId) {
  static QHash<QString, int> s_perStreamCount;
  const int cn = ++s_perStreamCount[stream];
  const bool trace = qEnvironmentVariableIntValue("CLIENT_WEBRTC_PACKET_CLASSIFY_TRACE") != 0;

  if (len < 2) {
    if (cn <= 24 || trace)
      qWarning() << "[Client][WebRTC][Ingress] stream=" << stream << " class=too_short len=" << len
                 << " lifecycleId=" << lifecycleId << " n=" << cn;
    return;
  }
  const unsigned v = static_cast<unsigned>(p[0]) >> 6;
  const uint8_t rb1 = p[1];
  const int pt7 = rb1 & 0x7F;
  const bool mbit = (rb1 & 0x80) != 0;
  const unsigned cc = static_cast<unsigned>(p[0]) & 0x0FU;
  const bool ext = (static_cast<unsigned>(p[0]) & 0x10U) != 0;
  const bool rtcpStd = (rb1 >= 200 && rb1 <= 204);
  // Qt 文档：qEnvironmentVariableIntValue(name) 在变量「未设置」时返回 0，无法与「显式设为
  // 0」区分。 旧逻辑把未设置当成 expectPt=0，而 WebRTC/H264 动态负载类型通常为 96，导致 100% 误报
  // Classify WARN、 巨量同步写日志拖慢 GUI 线程（见 h264decoder.cpp 对「日志→事件循环→VideoOutput
  // 不均匀」的说明）。
  bool envPtOk = false;
  const int envPt = qEnvironmentVariableIntValue("CLIENT_H264_RTP_PAYLOAD_TYPE", &envPtOk);
  const int expectPt = (envPtOk && envPt >= 0 && envPt <= 127) ? envPt : 96;

  static std::once_flag s_classifyConfigLog;
  std::call_once(s_classifyConfigLog, [envPtOk, envPt, expectPt]() {
    qInfo()
        << "[Client][WebRTC][Ingress][Classify-config] CLIENT_H264_RTP_PAYLOAD_TYPE"
        << (envPtOk ? QStringLiteral("set") : QStringLiteral("unset")) << " raw=" << envPt
        << " expectPt=" << expectPt
        << " ★ 参考 Qt qEnvironmentVariableIntValue(..., bool *ok)；未设置时必须默认 96 勿用 raw=0";
  });

  const bool anomaly = (len < 12) || (v != 2) || (pt7 != expectPt) || rtcpStd || (cc > 0) || ext;

  if (!anomaly) {
    if (trace && (cn % 500) == 0)
      qInfo() << "[Client][WebRTC][Ingress][OK] stream=" << stream << " n=" << cn << " len=" << len
              << " pt=" << pt7 << " cc=" << cc << " ext=" << ext;
    return;
  }

  if (cn <= 80 || trace || (cn % 200) == 0) {
    QString hex;
    const int hn = qMin(static_cast<int>(len), 20);
    for (int i = 0; i < hn; ++i)
      hex += QString::asprintf("%02X ", p[i]);
    qWarning() << "[Client][WebRTC][Ingress][Classify] stream=" << stream << " n=" << cn
               << " len=" << len << " v=" << v << " cc=" << cc << " ext=" << (ext ? 1 : 0)
               << " M=" << (mbit ? 1 : 0) << " pt7=" << pt7 << " expectPt=" << expectPt
               << (rtcpStd ? " ★RFC3550 RTCP(200-204)混入视频Track二进制回调→勿送H264" : "")
               << (len < 12 ? " ★len<12非完整RTP头" : "") << (v != 2 ? " ★version!=2" : "")
               << ((cc > 0 || ext) ? " ★CSRC/Extension 头→解码器须用动态 payload 偏移" : "")
               << " hex[" << hn << "]=" << hex.trimmed() << " lifecycleId=" << lifecycleId;
  }
}

QString metricCodeSuffixForVideo(const QString &code) {
  QString s;
  s.reserve(code.size());
  for (QChar c : code) {
    const bool alnum = (c >= QLatin1Char('A') && c <= QLatin1Char('Z')) ||
                       (c >= QLatin1Char('a') && c <= QLatin1Char('z')) ||
                       (c >= QLatin1Char('0') && c <= QLatin1Char('9'));
    s += alnum ? c : QLatin1Char('_');
  }
  return s.isEmpty() ? QStringLiteral("UNKNOWN") : s.mid(0, 96);
}
}  // namespace
#endif

namespace {
const char *webRtcPresentBackendTag(WebRtcPresentBackend b) {
  switch (b) {
    case WebRtcPresentBackend::InternalPlaceholderSink:
      return "InternalPlaceholderSink";
    case WebRtcPresentBackend::VideoOutput:
      return "VideoOutput";
    case WebRtcPresentBackend::RemoteSurface:
      return "RemoteSurface";
    case WebRtcPresentBackend::InvalidBothBound:
      return "InvalidBothBound";
  }
  return "WebRtcPresentBackend?";
}
}  // namespace

WebRtcClient::WebRtcClient(QObject *parent)
    : QObject(parent),
      m_networkManager(new QNetworkAccessManager(this)),
      m_reconnectTimer(new QTimer(this)),
      m_ownedSink(new QVideoSink(this)) {
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
                 << " streamEmpty=" << m_stream.isEmpty()
                 << " serverEmpty=" << m_serverUrl.isEmpty();
      }
    } catch (const std::exception &e) {
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

WebRtcClient::~WebRtcClient() { disconnect(); }

QVideoSink *WebRtcClient::activeSink() const {
  return m_boundOutputSink ? m_boundOutputSink.data() : m_ownedSink;
}

QVideoSink *WebRtcClient::videoSink() const { return activeSink(); }

WebRtcPresentBackend WebRtcClient::computePresentBackend() const {
  RemoteVideoSurface *const rs = m_boundRemoteSurface.data();
  QVideoSink *const vo = m_boundOutputSink.data();
  if (rs && vo)
    return WebRtcPresentBackend::InvalidBothBound;
  if (rs)
    return WebRtcPresentBackend::RemoteSurface;
  if (vo)
    return WebRtcPresentBackend::VideoOutput;
  return WebRtcPresentBackend::InternalPlaceholderSink;
}

void WebRtcClient::syncPresentBackendStateMachine(const char *ctx) {
  const WebRtcPresentBackend now = computePresentBackend();
  if (now == WebRtcPresentBackend::InvalidBothBound) {
    if (m_presentBackendSnapshot != WebRtcPresentBackend::InvalidBothBound) {
      MetricsCollector::instance().increment(
          QStringLiteral("client.video.present_backend_mutex_violation_total"));
      qCritical().noquote()
          << "[Client][WebRTC][PresentBackend][VIOLATION] ctx=" << QLatin1String(ctx)
          << " stream=" << m_stream
          << " boundQVideoSinkPtr=" << static_cast<const void *>(m_boundOutputSink.data())
          << " boundRemoteSurfacePtr=" << static_cast<const void *>(m_boundRemoteSurface.data())
          << " ★ 不变式：bindVideoOutput 与 bindVideoSurface 不得同时持有存活对象";
      m_presentBackendSnapshot = WebRtcPresentBackend::InvalidBothBound;
    } else {
      static std::atomic<int> s_stillViol{0};
      const int sn = s_stillViol.fetch_add(1, std::memory_order_relaxed);
      if (sn < 4 || (sn % 500) == 0) {
        qCritical().noquote()
            << "[Client][WebRTC][PresentBackend][VIOLATION_STILL] ctx=" << QLatin1String(ctx)
            << " stream=" << m_stream << " n=" << (sn + 1)
            << " ★ 仍处于双绑定非法态，直至 QML 换绑";
      }
    }
    return;
  }
  if (now != m_presentBackendSnapshot) {
    qInfo().noquote()
        << "[Client][WebRTC][PresentBackend] ctx=" << QLatin1String(ctx) << " stream=" << m_stream
        << " transition=" << webRtcPresentBackendTag(m_presentBackendSnapshot) << "->"
        << webRtcPresentBackendTag(now)
        << " outSinkPtr=" << static_cast<const void *>(m_boundOutputSink.data())
        << " remoteSurfPtr=" << static_cast<const void *>(m_boundRemoteSurface.data())
        << " activeSinkPtr=" << static_cast<const void *>(activeSink())
        << " ownedSinkPtr=" << static_cast<const void *>(m_ownedSink)
        << " ★ 用于检测换绑或 QML 销毁导致的呈现路径变化";
    m_presentBackendSnapshot = now;
  }
}

void WebRtcClient::bindVideoOutput(QObject *videoOutputItem) {
  m_boundRemoteSurface = nullptr;

  static std::atomic<int> s_bindSeq{0};
  const int bindSeq = ++s_bindSeq;
  QVideoSink *const prevSink = m_boundOutputSink.data();
  QVideoSink *sink = nullptr;
  if (videoOutputItem) {
    const QQmlProperty prop(videoOutputItem, QStringLiteral("videoSink"));
    if (!prop.isValid()) {
      qWarning()
          << "[Client][WebRTC][VideoSink] bindVideoOutput: item has no readable videoSink stream="
          << m_stream << " item=" << videoOutputItem;
    } else {
      const QVariant v = prop.read();
      QObject *obj = qvariant_cast<QObject *>(v);
      sink = qobject_cast<QVideoSink *>(obj);
      if (!sink) {
        qWarning()
            << "[Client][WebRTC][VideoSink] bindVideoOutput: videoSink not QVideoSink* stream="
            << m_stream << " type=" << (v.isValid() ? v.metaType().name() : "invalid");
      }
    }
  }
  m_boundOutputSink = sink;
  Q_ASSERT_X(!(m_boundOutputSink.data() && m_boundRemoteSurface.data()),
             "WebRtcClient::bindVideoOutput", "mutex: both output sink and remote surface set");
  ++m_bindVideoOutputCallCount;
  ++m_bindVideoOutputLifetimeCallCount;
  syncPresentBackendStateMachine("bindVideoOutput");
  qInfo()
      << "[Client][WebRTC][VideoSink] bindVideoOutput seq=" << bindSeq << " stream=" << m_stream
      << " thread=" << reinterpret_cast<quintptr>(QThread::currentThreadId())
      << " hasQmlOutput=" << (videoOutputItem != nullptr) << " usingBoundSink=" << (sink != nullptr)
      << " prevBoundSink=" << static_cast<const void *>(prevSink)
      << " newBoundSink=" << static_cast<const void *>(sink)
      << " activeSink=" << static_cast<const void *>(activeSink())
      << " bindsThisConn=" << m_bindVideoOutputCallCount
      << " bindsLifetime=" << m_bindVideoOutputLifetimeCallCount
      << " videoFrameReadyRc=" << receiverCountVideoFrameReady()
      << " presentBackend=" << webRtcPresentBackendTag(m_presentBackendSnapshot)
      << " ★ bindsThisConn 在单次连接内持续增加 → QML 反复 rebind；bindsLifetime 在重连后仍累加";
  emit videoSinkChanged();
}

void WebRtcClient::bindVideoSurface(QObject *surfaceItem) {
  m_boundOutputSink = nullptr;

  static std::atomic<int> s_bindSeq{0};
  const int bindSeq = ++s_bindSeq;
  RemoteVideoSurface *const prev = m_boundRemoteSurface.data();
#if defined(ENABLE_WEBRTC_LIBDATACHANNEL) && defined(ENABLE_FFMPEG)
  if (prev) {
    QObject::disconnect(prev, &RemoteVideoSurface::dmaBufSceneGraphFailed, this,
                        &WebRtcClient::onRemoteSurfaceDmaBufSceneGraphFailed);
  }
#endif
  RemoteVideoSurface *surf =
      surfaceItem ? qobject_cast<RemoteVideoSurface *>(surfaceItem) : nullptr;
  if (surfaceItem && !surf) {
    qWarning() << "[Client][WebRTC][RemoteSurface] bindVideoSurface: item is not "
                  "RemoteVideoSurface stream="
               << m_stream << " item=" << surfaceItem;
  }
#if defined(ENABLE_WEBRTC_LIBDATACHANNEL) && defined(ENABLE_FFMPEG)
  if (surf) {
    connect(surf, &RemoteVideoSurface::dmaBufSceneGraphFailed, this,
            &WebRtcClient::onRemoteSurfaceDmaBufSceneGraphFailed, Qt::QueuedConnection);
  }
#endif
  m_boundRemoteSurface = surf;
  Q_ASSERT_X(!(m_boundOutputSink.data() && m_boundRemoteSurface.data()),
             "WebRtcClient::bindVideoSurface", "mutex: both output sink and remote surface set");
  ++m_bindVideoSurfaceCallCount;
  ++m_bindVideoSurfaceLifetimeCallCount;
  syncPresentBackendStateMachine("bindVideoSurface");
  const QString panelTag = surf ? surf->panelLabel() : QString();
  qreal winDpr = -1.0;
  if (surf && surf->window())
    winDpr = surf->window()->effectiveDevicePixelRatio();
  qInfo() << "[Client][WebRTC][RemoteSurface] bindVideoSurface seq=" << bindSeq
          << " stream=" << m_stream
          << " panelLabel=" << panelTag
          << " thread=" << reinterpret_cast<quintptr>(QThread::currentThreadId())
          << " surface=" << static_cast<const void *>(surf)
          << " prevSurface=" << static_cast<const void *>(prev)
          << " surfWinDpr=" << winDpr
          << " bindsThisConn=" << m_bindVideoSurfaceCallCount
          << " bindsLifetime=" << m_bindVideoSurfaceLifetimeCallCount
          << " presentBackend=" << webRtcPresentBackendTag(m_presentBackendSnapshot)
          << " ★ 与 bindVideoOutput 互斥；Scene Graph 纹理；CLIENT_VIDEO_DECOUPLED_PRESENT "
             "控制合帧/限频线程；panelLabel 与 [VideoBind]/PNG 对齐 QML";
  if (surf && !panelTag.isEmpty()) {
    qInfo().noquote() << QStringLiteral("[Client][UI][VideoBind] stream=%1 panelLabel=\"%2\" surfacePtr=%3 "
                                        "expectedLogMatch=grep两字段一致即绑定正确")
                             .arg(m_stream, panelTag,
                                  QStringLiteral("0x%1").arg(reinterpret_cast<quintptr>(surf), 0, 16));
  }
  emit videoSinkChanged();
}

QSize WebRtcClient::diagnosticPresentSize() const {
  if (m_lastPresentedImageSize.isValid())
    return m_lastPresentedImageSize;
  if (QVideoSink *sk = activeSink()) {
    const QSize vs = sk->videoSize();
    if (vs.isValid())
      return vs;
  }
  return QSize();
}

WebRtcPresentSecondStats WebRtcClient::drainPresentSecondStats() {
  WebRtcPresentSecondStats s;
  s.framesToSink = m_presentSecFrames;
  s.nullSink = m_presentSecNullSink;
  s.invalidVf = m_presentSecInvalidVf;
  s.slowPresent = m_presentSecSlow;
  s.maxPending = m_presentSecMaxPending;
  s.maxHandlerUs = m_presentSecMaxHandlerUs;
  s.skippedPresentRateLimit = m_presentSecSkippedRateLimit;
  // ★ 新增：事件队列延迟 + coalescing 丢帧
  s.maxQueuedLagMs = m_presentSecMaxQueuedLagMs;
  s.avgQueuedLagMs = (m_presentSecQueuedLagSamples > 0)
                         ? (m_presentSecTotalQueuedLagMs / m_presentSecQueuedLagSamples)
                         : 0;
  s.coalescedDrops = m_presentSecCoalescedDrops;
  s.videoSlotEntries = m_presentSecVideoSlotEntries;
  s.flushCoalescedCount = m_presentSecFlushCoalescedCount;
  m_presentSecFrames = 0;
  m_presentSecNullSink = 0;
  m_presentSecInvalidVf = 0;
  m_presentSecSlow = 0;
  m_presentSecMaxPending = 0;
  m_presentSecMaxHandlerUs = 0;
  m_presentSecSkippedRateLimit = 0;
  m_presentSecMaxQueuedLagMs = 0;
  m_presentSecTotalQueuedLagMs = 0;
  m_presentSecQueuedLagSamples = 0;
  m_presentSecCoalescedDrops = 0;
  m_presentSecVideoSlotEntries = 0;
  m_presentSecFlushCoalescedCount = 0;
  return s;
}

int WebRtcClient::drainDecoderFrameReadyEmitDiagCount() {
#if defined(ENABLE_WEBRTC_LIBDATACHANNEL) && defined(ENABLE_FFMPEG)
  if (!m_h264Decoder)
    return -1;
  return m_h264Decoder->takeAndResetFrameReadyEmitDiagCount();
#else
  return -1;
#endif
}

void WebRtcClient::setStreamUrl(const QString &url) {
  if (m_streamUrl != url) {
    m_streamUrl = url;
    emit streamUrlChanged(m_streamUrl);
  }
}

void WebRtcClient::connectToStream(const QString &serverUrl, const QString &app,
                                   const QString &stream) {
  qInfo() << "[Client][WebRTC] connectToStream 进入 serverUrl=" << serverUrl << " app=" << app
          << " stream=" << stream << " prevStream=" << m_stream
          << " wasConnected=" << m_isConnected;

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
  m_connecting = true;  // 标记主动连接进行中，防止旧 PC 关闭回调触发定时器

  // ── 诊断：记录协商开始时间（毫秒）───────────────────────────────────────────
  m_connectStartTime = QDateTime::currentMSecsSinceEpoch();
  doConnect();
}

void WebRtcClient::doConnect() {
  try {
    m_offerSent = false;
    updateStatus("正在连接...", false);
    qInfo() << "[Client][WebRTC] doConnect stream=" << m_stream << " server=" << m_serverUrl
            << " app=" << m_app << " retry(stream not found)=" << m_retryCount
            << " reconnect(after drop)=" << m_reconnectCount;
    qDebug() << "[Client][WebRTC] 环节: 发起拉流 stream=" << m_stream
             << "（若 stream not found 将最多重试 12 次，间隔 3s）";

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
      qCritical() << "[Client][WebRTC][ERROR] doConnect: m_networkManager 为 nullptr，无法发起 "
                     "HTTP 请求 stream="
                  << m_stream;
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
  } catch (const std::exception &e) {
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

QString WebRtcClient::buildMinimalPlayOfferSdp() {
  // ZLM checkValid 要求：每个媒体段 direction 有效或 type==TrackApplication。
  // 仅 m=application 为 TrackApplication；m=audio/m=video 需有 recvonly/sendrecv 等。
  // 使用最小 play Offer：m=audio + m=video，均为 recvonly，带 a=mid，供 ZLM play 拉流。
  const QString ufrag =
      QStringLiteral("x") + QString::number(QRandomGenerator::global()->generate(), 16).left(7);
  const QString pwd = QString::number(QRandomGenerator::global()->generate(), 16) +
                      QString::number(QRandomGenerator::global()->generate(), 16).left(6);
  const QString fingerprint(
      QStringLiteral("00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:"
                     "00:00:00:00:00:00:00"));
  QString sdp;
  sdp += QStringLiteral("v=0\r\n");
  sdp += QStringLiteral("o=- 0 0 IN IP4 0.0.0.0\r\n");
  sdp += QStringLiteral("s=-\r\n");
  sdp += QStringLiteral("t=0 0\r\n");
  sdp += QStringLiteral("a=group:BUNDLE 0 1\r\n");
  sdp += QStringLiteral("a=msid-semantic: WMS *\r\n");
  sdp +=
      QStringLiteral("a=fingerprint:sha-256 ").append(fingerprint).append(QStringLiteral("\r\n"));
  // m=audio, recvonly
  sdp += QStringLiteral("m=audio 9 UDP/TLS/RTP/SAVPF 0\r\n");
  sdp += QStringLiteral("c=IN IP4 0.0.0.0\r\n");
  sdp += QStringLiteral("a=recvonly\r\n");
  sdp += QStringLiteral("a=mid:0\r\n");
  sdp += QStringLiteral("a=rtcp-mux\r\n");
  sdp += QStringLiteral("a=ice-ufrag:").append(ufrag).append(QStringLiteral("\r\n"));
  sdp += QStringLiteral("a=ice-pwd:").append(pwd).append(QStringLiteral("\r\n"));
  sdp +=
      QStringLiteral("a=fingerprint:sha-256 ").append(fingerprint).append(QStringLiteral("\r\n"));
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
  sdp +=
      QStringLiteral("a=fingerprint:sha-256 ").append(fingerprint).append(QStringLiteral("\r\n"));
  sdp += QStringLiteral("a=setup:actpass\r\n");
  sdp += QStringLiteral("a=rtpmap:96 H264/90000\r\n");
  sdp += QStringLiteral("a=fmtp:96 packetization-mode=1;profile-level-id=42e01f\r\n");
  return sdp;
}

void WebRtcClient::createOffer() {
  // ── ★★★ 端到端追踪：createOffer 进入 ★★★ ────────────────────────────────
  const int64_t funcEnterTime = QDateTime::currentMSecsSinceEpoch();
  qInfo() << "[Client][WebRTC][SDP] ★★★ createOffer ENTER ★★★"
          << " stream=" << m_stream << " m_peerConnection_existing=" << (bool)m_peerConnection
          << " enterTime=" << funcEnterTime;
#ifdef ENABLE_WEBRTC_LIBDATACHANNEL
  try {
    if (m_peerConnection) {
      qInfo()
          << "[Client][WebRTC] createOffer: 清理残留 PeerConnection，避免双 PC/ICE 资源争用 stream="
          << m_stream;
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
                                   QString::fromUtf8(QUrl::toPercentEncoding(password)), host,
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
    m_lastPresentWallMs = 0;

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
          typeStr = parts[7];   // host / srflx / relay
          protoStr = parts[4];  // UDP / TCP
        }
        qDebug() << "[Client][WebRTC][ICE] LocalCandidate stream=" << m_stream
                 << " type=" << typeStr << " proto=" << protoStr << " cand=" << cand;
      } catch (const std::exception &e) {
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
        const int64_t answerDelay =
            (m_answerReceivedTime > 0) ? (trackTime - m_answerReceivedTime) : -1;
        qInfo() << "[Client][WebRTC][SDP] onTrack stream=" << m_stream
                << " kind=" << QString::fromStdString(kind) << " 协商耗时: total=" << connDelay
                << "ms offer→Answer=" << offerDelay << "ms Answer→Track=" << answerDelay << "ms";
        qDebug() << "[Client][WebRTC] onTrack stream=" << m_stream
                 << "kind=" << QString::fromStdString(kind);
        if (kind == "video") {
          m_videoTrack = track;
#if defined(ENABLE_WEBRTC_LIBDATACHANNEL) && defined(ENABLE_FFMPEG)
          QTimer::singleShot(0, this, &WebRtcClient::setupVideoDecoder);
#endif
        } else if (kind == "audio") {
          m_audioTrack = track;
        }
      } catch (const std::exception &e) {
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
          case 0:
            stateStr = "New";
            break;
          case 1:
            stateStr = "Connecting";
            break;
          case 2:
            stateStr = "Connected";
            break;
          case 3:
            stateStr = "Disconnected";
            break;
          case 4:
            stateStr = "Failed";
            break;
          case 5:
            stateStr = "Closed";
            break;
        }
        qInfo() << "[Client][WebRTC] PeerConnection state stream=" << m_stream
                << " state=" << stateStr << " enum=" << static_cast<int>(state);

        // ★ onStateChange 在 libdatachannel 工作线程：切回主线程再碰 Qt / 定时器
        QTimer::singleShot(0, this, [this, state]() {
          // 400ms 内多路 PeerConnection 同批次 Disconnected/Closed → 同一 waveId，便于与 ZLM/MQTT
          // 日志对齐
          int disconnectWaveId = -1;
          if (state == rtc::PeerConnection::State::Disconnected ||
              state == rtc::PeerConnection::State::Failed ||
              state == rtc::PeerConnection::State::Closed) {
            static QMutex s_waveMx;
            static int s_wave = 0;
            static qint64 s_waveT0 = 0;
            QMutexLocker locker(&s_waveMx);
            const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
            if (s_waveT0 == 0 || nowMs - s_waveT0 > 400) {
              ++s_wave;
              s_waveT0 = nowMs;
            }
            disconnectWaveId = s_wave;
          }
          try {
            if (state == rtc::PeerConnection::State::Connected) {
              m_reconnectScheduled = false;
              m_connecting = false;  // 主动连接完成，后续断连才可触发定时器
              updateStatus("已连接", true);
              m_isConnected = true;
              m_reconnectCount = 0;
              qInfo() << "[Client][WebRTC] 媒体面已连通 stream=" << m_stream
                      << "（重连计数已清零）";
              emit connectionStatusChanged(true);
#if defined(ENABLE_WEBRTC_LIBDATACHANNEL) && defined(ENABLE_FFMPEG)
              QTimer::singleShot(0, this, &WebRtcClient::trySendProactiveEncoderDisplayContractHint);
#endif
              return;
            }

            // Disconnected：多为 ICE/DTLS 瞬时抖动，随后常跟 Closed；此处只更新
            // UI/聚合状态，不排队自动重连，避免与 Closed 重复触发
            if (state == rtc::PeerConnection::State::Disconnected) {
              // ── 诊断：断开时打印帧接收状态（核心诊断点）────────────────────
              const int64_t now = QDateTime::currentMSecsSinceEpoch();
              const int64_t lastFrameAge = (m_lastFrameTime > 0) ? (now - m_lastFrameTime) : -1;
              const int64_t connDuration =
                  (m_connectStartTime > 0) ? (now - m_connectStartTime) : -1;
              qWarning() << "[Client][WebRTC][State][Diag] PeerConnection Disconnected stream="
                         << m_stream << " disconnectWaveId=" << disconnectWaveId
                         << " 已连接时长=" << connDuration << "ms"
                         << " 上帧距今=" << lastFrameAge << "ms"
                         << " 本周期帧数=" << m_videoFrameLogCount
                         << " framesSinceLast=" << m_framesSinceLastStats.load()
                         << " 【诊断结论】："
                         << (m_videoFrameLogCount == 0 ? "ZLM 从未发帧(检查 ZLM 推流)"
                             : lastFrameAge > 5000 ? "ZLM 已停发帧(检查 carla-bridge→ZLM 链路)"
                                                   : "ZLM 正在发帧但 ICE/UDP 抖动(ZLM 或网络问题)");
              qWarning() << "[Client][WebRTC] PeerConnection Disconnected stream=" << m_stream
                         << " disconnectWaveId=" << disconnectWaveId
                         << "（不触发自动重连；若永久失败将进入 Failed/Closed 再重连）";
              qInfo()
                  << "[Client][WebRTC][Diag] stream=" << m_stream
                  << " UDP/ICE 抖动排障: 宿主执行 ZLM 媒体列表 ./scripts/diag-zlm-streams.sh;"
                  << "抓包 docker0 UDP 示例: sudo tcpdump -i docker0 -n udp and host zlmediakit";
              updateStatus("连接不稳定…", false);
              m_isConnected = false;
              emit connectionStatusChanged(false);
              if (disconnectWaveId >= 0)
                emit zlmSnapshotRequested(disconnectWaveId, m_stream, static_cast<int>(state));
              return;
            }

            if (state == rtc::PeerConnection::State::Failed ||
                state == rtc::PeerConnection::State::Closed) {
              const char *reason =
                  (state == rtc::PeerConnection::State::Failed) ? "Failed" : "Closed";
              qWarning() << "[Client][WebRTC] PeerConnection 终结 reason=" << reason
                         << " stream=" << m_stream << " disconnectWaveId=" << disconnectWaveId
                         << " manualDisconnect=" << m_manualDisconnect;
              updateStatus(QStringLiteral("已断开 (%1)").arg(QLatin1String(reason)), false);
              m_isConnected = false;
              emit connectionStatusChanged(false);
              if (disconnectWaveId >= 0)
                emit zlmSnapshotRequested(disconnectWaveId, m_stream, static_cast<int>(state));
              scheduleAutoReconnectIfNeeded(reason, disconnectWaveId);
            }
          } catch (const std::exception &e) {
            qCritical()
                << "[Client][WebRTC][ERROR] onStateChange QTimer::singleShot 内异常: stream="
                << m_stream << " state=" << static_cast<int>(state) << " error=" << e.what();
            updateStatus("状态处理异常", false);
            m_isConnected = false;
            emit connectionStatusChanged(false);
          } catch (...) {
            qCritical()
                << "[Client][WebRTC][ERROR] onStateChange QTimer::singleShot 内未知异常: stream="
                << m_stream << " state=" << static_cast<int>(state);
            updateStatus("状态处理异常", false);
            m_isConnected = false;
            emit connectionStatusChanged(false);
          }
        });
      } catch (const std::exception &e) {
        qCritical() << "[Client][WebRTC][ERROR] onStateChange 回调异常: stream=" << m_stream
                    << " error=" << e.what();
      } catch (...) {
        qCritical() << "[Client][WebRTC][ERROR] onStateChange 回调未知异常: stream=" << m_stream;
      }
    });

    // Play 拉流：库要求至少有一个 DataChannel 或 Track 才生成 Offer；RecvOnly 的 addTrack
    // 不被视为可协商， 故先创建占位 DataChannel，再添加 recvonly 音视频，由 onLocalDescription
    // 发送。 onLocalDescription 在 libdatachannel 工作线程调用，QNetworkAccessManager
    // 属于主线程，必须在主线程 post。
    m_peerConnection->onLocalDescription([this](rtc::Description description) {
      try {
        m_localSdp = QString::fromStdString(std::string(description));
        qDebug() << "[Client][WebRTC] play Offer 已生成 stream=" << m_stream
                 << "，排队到主线程发送";
        QTimer::singleShot(0, this, [this]() {
          try {
            sendOfferToServer(m_localSdp);
          } catch (const std::exception &e) {
            qCritical() << "[Client][WebRTC][ERROR] sendOfferToServer 异常: stream=" << m_stream
                        << " error=" << e.what();
            updateStatus("Offer 发送失败: " + QString::fromLatin1(e.what()), false);
            emit errorOccurred("Offer 发送异常: " + QString::fromLatin1(e.what()));
          } catch (...) {
            qCritical() << "[Client][WebRTC][ERROR] sendOfferToServer 未知异常: stream="
                        << m_stream;
            updateStatus("Offer 发送异常", false);
            emit errorOccurred("Offer 发送未知异常");
          }
        });
      } catch (const std::exception &e) {
        qCritical() << "[Client][WebRTC][ERROR] onLocalDescription 回调异常: stream=" << m_stream
                    << " error=" << e.what();
      } catch (...) {
        qCritical() << "[Client][WebRTC][ERROR] onLocalDescription 回调未知异常: stream="
                    << m_stream;
      }
    });
    m_dataChannel = m_peerConnection->createDataChannel("control");
    rtc::Description::Video videoMedia("video", rtc::Description::Direction::RecvOnly);
    videoMedia.addH264Codec(96);
    (void)m_peerConnection->addTrack(videoMedia);
    rtc::Description::Audio audioMedia("audio", rtc::Description::Direction::RecvOnly);
    audioMedia.addOpusCodec(111);
    (void)m_peerConnection->addTrack(audioMedia);
    m_peerConnection->setLocalDescription(rtc::Description::Type::Offer);
  } catch (const std::exception &e) {
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

QString WebRtcClient::ensureSdpHasMid(const QString &sdp) {
  if (sdp.isEmpty())
    return sdp;
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

QString WebRtcClient::injectRecvonlyAudioVideoIfSingleMedia(const QString &sdp) {
  if (sdp.isEmpty())
    return sdp;
  QString lineEnd = sdp.contains("\r\n") ? QStringLiteral("\r\n") : QStringLiteral("\n");
  QStringList lines = sdp.split(lineEnd, Qt::KeepEmptyParts);
  int mediaCount = 0;
  for (const QString &line : lines)
    if (line.startsWith(QLatin1String("m=")))
      ++mediaCount;
  if (mediaCount != 1)
    return sdp;

  // 从第一个 m= 段解析 ice-ufrag / ice-pwd / fingerprint，用于注入的 audio/video 段（BUNDLE 同
  // transport）
  QString ufrag =
      QStringLiteral("x") + QString::number(QRandomGenerator::global()->generate(), 16).left(7);
  QString pwd = QString::number(QRandomGenerator::global()->generate(), 16) +
                QString::number(QRandomGenerator::global()->generate(), 16).left(6);
  QString fingerprint(
      QStringLiteral("00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:"
                     "00:00:00:00:00:00:00"));
  bool inFirst = false;
  for (int i = 0; i < lines.size(); ++i) {
    const QString &line = lines[i];
    if (line.startsWith(QLatin1String("m="))) {
      if (!inFirst) {
        inFirst = true;
        continue;
      }
      break;
    }
    if (!inFirst)
      continue;
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
      if (out[i].startsWith(QLatin1String("a=msid-semantic")) ||
          out[i].startsWith(QLatin1String("a=fingerprint"))) {
        insertAt = i + 1;
        break;
      }
    }
    out.insert(insertAt, QStringLiteral("a=group:BUNDLE 0 1 2"));
  }
  QString result = out.join(lineEnd) + extra;
  qDebug() << "[Client][WebRTC] injectRecvonlyAudioVideoIfSingleMedia: 已注入 m=audio( mid 1 ) + "
              "m=video( mid 2 )，BUNDLE 0 1 2";
  return result;
}

QString WebRtcClient::ensureSdpBundleGroup(const QString &sdp) {
  if (sdp.isEmpty())
    return sdp;
  QString lineEnd = sdp.contains("\r\n") ? QStringLiteral("\r\n") : QStringLiteral("\n");
  QStringList lines = sdp.split(lineEnd, Qt::KeepEmptyParts);
  int mediaCount = 0;
  for (const QString &line : lines)
    if (line.startsWith(QLatin1String("m=")))
      ++mediaCount;
  if (mediaCount == 0)
    return sdp;
  // ZLM checkValid: group.mids.size() <= media.size()；注入后为 3 个 media，BUNDLE 0 1 2
  int bundleSize = mediaCount;
  QStringList bundleMids;
  for (int i = 0; i < bundleSize; ++i)
    bundleMids.append(QString::number(i));
  QString bundleLine = QStringLiteral("a=group:BUNDLE ") + bundleMids.join(QLatin1Char(' '));
  qDebug() << "[Client][WebRTC] ensureSdpBundleGroup: mediaCount=" << mediaCount
           << "bundleSize=" << bundleSize << "bundleLine=" << bundleLine;
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
      if (out[i].startsWith(QLatin1String("a=msid-semantic")) ||
          out[i].startsWith(QLatin1String("a=fingerprint"))) {
        insertAt = i + 1;
        break;
      }
    }
    out.insert(insertAt, bundleLine);
  }
  return out.join(lineEnd);
}

void WebRtcClient::sendOfferToServer(const QString &offer) {
  // ── ★★★ 端到端追踪：sendOfferToServer 进入 ★★★ ────────────────────────────
  const int64_t funcEnterTime = QDateTime::currentMSecsSinceEpoch();
  qInfo() << "[Client][WebRTC][sendOffer] ★★★ sendOfferToServer ENTER ★★★"
          << " stream=" << m_stream << " m_offerSent=" << m_offerSent
          << " m_streamEmpty=" << m_stream.isEmpty()
          << " m_serverUrlEmpty=" << m_serverUrl.isEmpty() << " enterTime=" << funcEnterTime;
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
  QString offerToSend =
      ensureSdpBundleGroup(injectRecvonlyAudioVideoIfSingleMedia(ensureSdpHasMid(offer)));

  QUrl apiUrl(m_serverUrl + "/index/api/webrtc");
  QUrlQuery query;
  query.addQueryItem("app", m_app);
  query.addQueryItem("stream", m_stream);
  query.addQueryItem("type", "play");
  apiUrl.setQuery(query);

  setStreamUrl(apiUrl.toString());
  qDebug() << "[Client][WebRTC] 环节: 发起拉流 stream=" << m_stream
           << "（若 stream not found 将最多重试 12 次，间隔 3s）";

  QString urlString = apiUrl.toString();
  qDebug() << "[Client][WebRTC] 主线程发送 POST stream=" << m_stream << "url=" << urlString;

  // 快速重连时结束上一轮 reply。注意：QNetworkReply::abort() 会 emit finished()（见 Qt 官方文档），
  // 若与槽直连则可能同步重入 onSdpAnswerReceived；后者会 m_currentReply=null +
  // reply->deleteLater()。 若此处仍对 m_currentReply->deleteLater() 则会对 nullptr
  // 解引用崩溃（与日志中 sendOfferToServer 栈一致）。
  if (m_currentReply) {
    QNetworkReply *oldReply = m_currentReply;
    m_currentReply = nullptr;
    oldReply->disconnect(this);
    oldReply->abort();
    oldReply->deleteLater();
  }

  QNetworkRequest request(apiUrl);
  request.setHeader(QNetworkRequest::ContentTypeHeader, "application/session_description_protocol");
  request.setRawHeader("Accept", "application/json");

  m_currentReply = m_networkManager->post(request, offerToSend.toUtf8());
  qInfo() << "[Client][WebRTC][sendOffer] POST 已发送 stream=" << m_stream
          << " offerLen=" << offerToSend.size() << " url=" << urlString
          << " enterToPostMs=" << (QDateTime::currentMSecsSinceEpoch() - funcEnterTime)
          << " ★ 对比 onSdpAnswerReceived 进入时间，确认信令往返耗时";

  // HTTP 首包时延：区分「ZLM/网络慢」与「客户端事件循环阻塞」（Qt QNetworkReply 官方文档：
  // finished 在完整响应到达后发出；readyRead 在首段 body 可读时发出）
  {
    QPointer<QNetworkReply> rp(m_currentReply);
    connect(rp.data(), &QNetworkReply::readyRead, this, [this, rp]() {
      if (!rp)
        return;
      if (rp->property("zlmFirstByteLogged").toBool())
        return;
      rp->setProperty("zlmFirstByteLogged", true);
      const qint64 ms =
          (m_offerSentTime > 0) ? (QDateTime::currentMSecsSinceEpoch() - m_offerSentTime) : -1;
      qInfo() << "[Client][WebRTC][HTTP] readyRead 首包可读 stream=" << m_stream
              << " bytesAvail=" << rp->bytesAvailable() << " msSinceOfferPost=" << ms
              << " ★ 若 finished 仍很晚 → 多为服务端 hold 连接或 body 分片迟；若首包已快而 "
                 "finished 慢 → 对照 ZLM 日志";
    });
  }

  // 必须用 sender / 捕获指针，禁止 finished 槽里读 m_currentReply：新一轮 POST 已可能覆盖成员，
  // 会导致「旧 reply 的 finished 误处理新 reply」→ 提前 deleteLater 新 reply → 崩溃或 device not
  // open。
  QNetworkReply *postedReply = m_currentReply;
  connect(postedReply, &QNetworkReply::finished, this,
          [this, rp = QPointer<QNetworkReply>(postedReply)]() {
            QNetworkReply *finishedReply = rp.data();
            if (!finishedReply) {
              qWarning() << "[Client][WebRTC][Answer] finished 槽: QPointer 已失效（reply "
                            "已销毁），忽略 stream="
                         << m_stream;
              return;
            }
            qDebug() << "[Client][WebRTC][Answer] finished 槽: senderReply="
                     << (void *)finishedReply << " m_currentReply=" << (void *)m_currentReply
                     << " stream=" << m_stream;
            onSdpAnswerReceived(finishedReply);
          });

  connect(postedReply, &QNetworkReply::errorOccurred, this,
          [this, rp = QPointer<QNetworkReply>(postedReply)](QNetworkReply::NetworkError error) {
            try {
              Q_UNUSED(error)
              QNetworkReply *er = rp.data();
              QString err = er ? er->errorString() : QStringLiteral("Unknown");
              qWarning() << "[Client][WebRTC][ERROR] 请求失败 stream=" << m_stream
                         << " reply=" << (void *)er << " m_currentReply=" << (void *)m_currentReply
                         << " error=" << err;
              updateStatus("连接失败: " + err, false);
              emit errorOccurred(err);
            } catch (const std::exception &e) {
              qCritical() << "[Client][WebRTC][ERROR] errorOccurred 回调内异常: stream=" << m_stream
                          << " error=" << e.what();
            } catch (...) {
              qCritical() << "[Client][WebRTC][ERROR] errorOccurred 回调内未知异常: stream="
                          << m_stream;
            }
          });
}

void WebRtcClient::onSdpAnswerReceived(QNetworkReply *reply) {
  // ── ★★★ 端到端追踪：onSdpAnswerReceived 进入 ★★★ ─────────────────────────
  const int64_t funcEnterTime = QDateTime::currentMSecsSinceEpoch();
  const int64_t postToAnswerMs = funcEnterTime - m_offerSentTime;
  qInfo() << "[Client][WebRTC][Answer] ★★★ onSdpAnswerReceived ENTER ★★★"
          << " stream=" << m_stream << " reply=" << (void *)reply
          << " m_currentReply=" << (void *)m_currentReply << " enterTime=" << funcEnterTime
          << " postToAnswerMs=" << postToAnswerMs
          << " ★ 信令耗时（应 <500ms，超过 2s 说明 ZLM 或网络慢）";

  // ── 诊断：记录 Answer 收到时刻 + 各阶段耗时 ────────────────────────────────
  m_answerReceivedTime = QDateTime::currentMSecsSinceEpoch();
  const int64_t t0 = m_connectStartTime > 0 ? m_connectStartTime : m_answerReceivedTime;
  const int64_t totalDelay = m_answerReceivedTime - t0;
  const int64_t offerDelay = (m_offerSentTime > 0) ? (m_answerReceivedTime - m_offerSentTime) : -1;

  // 仅处理「当前」信令请求：旧 reply 在 abort/换 POST 后仍可能排队 finished（已 disconnect
  // 则不应到达）。
  if (!reply) {
    qWarning() << "[Client][WebRTC] onSdpAnswerReceived() reply 为空，忽略 stream=" << m_stream;
    return;
  }
  if (reply != m_currentReply) {
    qWarning() << "[Client][WebRTC] onSdpAnswerReceived() 陈旧 reply（≠m_currentReply），仅 "
                  "deleteLater，不解析 SDP stream="
               << m_stream << " reply=" << (void *)reply
               << " m_currentReply=" << (void *)m_currentReply;
    reply->deleteLater();
    return;
  }

  QByteArray data = reply->readAll();
  int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  qInfo() << "[Client][WebRTC][Answer] QNetworkReply 完成 stream=" << m_stream
          << " networkError=" << static_cast<int>(reply->error())
          << " errorString=" << reply->errorString() << " httpStatus=" << httpStatus
          << " bodySize=" << data.size()
          << " ★ Qt 文档：NetworkError 与 HTTP 状态独立；HTTP 200 仍可能有业务 code≠0 JSON";

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

  qDebug() << "[Client][WebRTC] 环节: 收到 ZLM 响应 stream=" << m_stream
           << " httpStatus=" << httpStatus << " bodySize=" << data.size();

  // ── 诊断：SDP Answer 协商完整时间链 ─────────────────────────────────────────
  qInfo() << "[Client][WebRTC][SDP] Answer 收到 stream=" << m_stream << " httpStatus=" << httpStatus
          << " bodySize=" << data.size() << " totalDelay=" << totalDelay
          << "ms offer→Answer=" << offerDelay << "ms"
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
    qDebug() << "[Client][WebRTC] 环节: ✓ 拉流成功 stream=" << m_stream
             << " sdpLen=" << m_remoteSdp.size();
    processAnswer(m_remoteSdp);
  } else if (json.contains("code") && json["code"].toInt() != 0) {
    int code = json["code"].toInt();
    QString errorMsg = json.contains("msg") ? json["msg"].toString() : "未知错误";
    qWarning() << "[Client][WebRTC] 环节: ZLM 返回错误 stream=" << m_stream << " code=" << code
               << " msg=" << errorMsg << "（-400=stream not found，请确认车端已推流）";
    // 完整 body 通常很小（几十字节），便于与 ZLM 文档/抓包对照；HTTP 200 仍可有业务 code≠0（见 Qt
    // QNetworkReply 文档）
    qInfo() << "[Client][WebRTC][ZlmBiz] 错误响应原始 JSON body=" << QString::fromUtf8(data)
            << "★ 对照 [StreamManager][ZlmWave] getMediaList：若本 stream 不在列表 → "
               "根因在上游推流/VIN 命名，非解码/UI";
    if (code == -400 && m_retryCount < 12) {
      m_retryCount++;
      int remaining = 12 - m_retryCount;
      qDebug() << "[Client][WebRTC] stream not found，第" << m_retryCount
               << "次尝试拉流（最多 12 次重试），还剩" << remaining
               << "次，3s 后重试 stream=" << m_stream;
      updateStatus(QString("流尚未就绪，%1s 后第 %2 次重试…").arg(3).arg(m_retryCount + 1), false);
      QTimer::singleShot(3000, this, [this]() { doConnect(); });
    } else {
      if (code == -400)
        m_retryCount = 0;
      updateStatus("流不存在或等待车端推流", false);
      emit errorOccurred(errorMsg);
    }
  } else {
    qWarning() << "[Client][WebRTC] 响应无 sdp 且无 code stream=" << m_stream
               << "fullBody=" << QString::fromUtf8(data);
    updateStatus("流不存在或等待车端推流", false);
    emit errorOccurred("响应格式异常");
  }

  // ★ 清理 m_currentReply 引用，避免 disconnect() 时访问已删除的对象
  m_currentReply = nullptr;
  reply->deleteLater();
  qDebug() << "[Client][WebRTC] onSdpAnswerReceived() 完成，已清理 m_currentReply stream="
           << m_stream;
}

void WebRtcClient::processAnswer(const QString &answer) {
  const int64_t funcEnterTime = QDateTime::currentMSecsSinceEpoch();
  m_remoteSdp = answer;
  // ── ★★★ 端到端追踪：processAnswer 进入 ★★★ ───────────────────────────────
  qInfo() << "[Client][WebRTC][Answer] ★★★ processAnswer ENTER ★★★"
          << " stream=" << m_stream << " answerLen=" << answer.size()
          << " answerHead=" << QString(answer.left(200)).replace('\r', ' ').replace('\n', ' ')
          << " m_peerConnection=" << (void *)m_peerConnection.get()
          << " enterToProcessMs=" << (QDateTime::currentMSecsSinceEpoch() - funcEnterTime);

#ifdef ENABLE_WEBRTC_LIBDATACHANNEL
  try {
    // Pre-process SDP to ensure compatibility (Plan 3.2)
    QString processedAnswer = ensureSdpBundleGroup(ensureSdpHasMid(answer));

    if (m_peerConnection) {
      rtc::Description description(processedAnswer.toStdString(), rtc::Description::Type::Answer);
      m_peerConnection->setRemoteDescription(description);
      qInfo() << "[Client][WebRTC][Answer] ★★★ setRemoteDescription 完成 ★★★"
              << " stream=" << m_stream << " ★ 对比 onTrack 进入时间，确认 ICE 协商耗时"
              << " processAnswer耗时=" << (QDateTime::currentMSecsSinceEpoch() - funcEnterTime)
              << "ms";
    } else {
      qWarning() << "[Client][WebRTC] processAnswer stream=" << m_stream
                 << "m_peerConnection 为空，跳过";
    }
  } catch (const std::exception &e) {
    QString error = QString("Failed to process SDP Answer: %1").arg(e.what());
    qWarning() << "[Client][WebRTC] processAnswer 异常 stream=" << m_stream << "error=" << error;
    emit errorOccurred(error);
    m_connecting = false;  // 异常时重置，避免残留标志屏蔽后续被动断连
    m_offerSent = false;   // 重置 Offer 发送标志，使 1s 后的 doConnect 能重发 Offer
    updateStatus("SDP 协商失败，重连中...", false);
    QTimer::singleShot(1000, this, &WebRtcClient::doConnect);
  }
#else
  // 无 libdatachannel 时仅收到 SDP，未建立真实 WebRTC 连接，无法收流；不设 isConnected
  // 避免界面误显示「视频已连接」
  updateStatus("信令成功，需 WebRTC 库以接收视频", false);
  // m_isConnected 保持 false，界面显示上述 statusText 而非「视频已连接」
#endif
}

void WebRtcClient::disconnect() {
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
  qDebug() << "[Client][WebRTC] disconnect() m_currentReply=" << (void *)reply
           << " stream=" << m_stream;
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
        qDebug() << "[Client][WebRTC] disconnect() reply 对象有效，准备断开连接 stream="
                 << m_stream;
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
        qWarning() << "[Client][WebRTC] disconnect() QPointer 检查失败，reply 已删除 stream="
                   << m_stream;
      }
    } else {
      qWarning() << "[Client][WebRTC] disconnect() reply 对象无效，跳过断开操作 stream="
                 << m_stream;
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

void WebRtcClient::teardownMediaPipeline() {
#ifdef ENABLE_WEBRTC_LIBDATACHANNEL
#if defined(ENABLE_FFMPEG)
  m_decodePipelineActive.store(false, std::memory_order_release);
  stopPresentPipeline();
#endif
  m_videoTrack.reset();
  m_audioTrack.reset();
#if defined(ENABLE_FFMPEG)
  VideoFrameFingerprintCache::instance().clearStream(m_stream);
  if (m_h264Decoder) {
    H264Decoder *dec = m_h264Decoder;
    m_h264Decoder = nullptr;
    if (m_decodeThread && m_decodeThread->isRunning()) {
      const bool invoked = QMetaObject::invokeMethod(
          dec,
          [dec]() {
            dec->clearIngressQueue();
            dec->disconnect();
            dec->reset();
            delete dec;
          },
          Qt::BlockingQueuedConnection);
      if (!invoked)
        qCritical() << "[Client][WebRTC][ERROR] decoder BlockingQueued shutdown failed stream="
                    << m_stream;
    } else {
      dec->clearIngressQueue();
      dec->disconnect();
      dec->reset();
      delete dec;
    }
  }
#if defined(ENABLE_WEBRTC_LIBDATACHANNEL) && defined(ENABLE_FFMPEG)
  if (m_mediaBudgetSlot >= 0 && m_mediaBudgetSlot < ClientMediaBudget::kMaxSlots)
    ClientMediaBudget::instance().setSlotEnabled(m_mediaBudgetSlot, false);
  m_rtpIngressRing.reset();
  m_ingressDrainPosted.store(false, std::memory_order_release);
#endif
  if (m_decodeThread) {
    m_decodeThread->quit();
    if (!m_decodeThread->wait(8000)) {
      qWarning() << "[Client][WebRTC] decode thread wait timeout, terminate stream=" << m_stream;
      m_decodeThread->terminate();
      m_decodeThread->wait(2000);
    }
    delete m_decodeThread;
    m_decodeThread = nullptr;
  }
  m_rtpClock.reset();
  m_rtpVideoSsrc.store(0, std::memory_order_relaxed);
#endif
  if (m_peerConnection) {
    qDebug() << "[Client][WebRTC] teardownMediaPipeline: close PeerConnection stream=" << m_stream;
    try {
      m_peerConnection->close();
    } catch (...) {
      qWarning() << "[Client][WebRTC] teardownMediaPipeline: peerConnection->close() 异常 stream="
                 << m_stream;
    }
    m_peerConnection.reset();
  }
  m_dataChannel.reset();
#else
  Q_UNUSED(this);
#endif
}

void WebRtcClient::prepareForNewConnection() {
  m_videoFrameLogCount = 0;
  // ── 视频呈现绑定：刻意不在此清空 m_boundRemoteSurface / m_boundOutputSink ─────────
  // 原因（产品语义）：connectToStream() 复用同一 WebRtcClient 实例时，QML 侧 streamClient
  // 引用不变 → VideoPanel.onStreamClientChanged / RemoteVideoSurface.onCompleted 不会再次
  // bindVideoSurface/bindVideoOutput。若此处置空绑定，重连后帧会落到 m_ownedSink 且 UI
  // 仍只监听 RemoteVideoSurface → 黑屏，直到用户离开再进远驾页或额外接 connection 信号重绑。
  // QPointer 在 QML 项销毁时会自动变 null，不会长期悬挂；若需「计数与真实路径严格一致」，
  // 应改 QML/StreamManager 在 media ready 时显式重绑，而不是仅清 C++ 指针。
  qInfo() << "[Client][WebRTC][VideoBind] prepareForNewConnection: reset bindsThisConn (was out="
          << m_bindVideoOutputCallCount << " surf=" << m_bindVideoSurfaceCallCount << ")"
          << " streamTag=" << m_stream
          << " bindsLifetime out=" << m_bindVideoOutputLifetimeCallCount
          << " surf=" << m_bindVideoSurfaceLifetimeCallCount
          << " boundQVideoSink=" << static_cast<const void *>(m_boundOutputSink.data())
          << " boundQVideoSinkNull=" << m_boundOutputSink.isNull()
          << " boundRemoteSurface=" << static_cast<const void *>(m_boundRemoteSurface.data())
          << " boundRemoteSurfaceNull=" << m_boundRemoteSurface.isNull()
          << " activeSinkPtr=" << static_cast<const void *>(activeSink())
          << " presentBackend=" << webRtcPresentBackendTag(computePresentBackend())
          << " ★ bind*Cnt 将清零；绑定 QPointer 保留至 QML 销毁或下次 bind* 覆盖";
  m_bindVideoOutputCallCount = 0;
  m_bindVideoSurfaceCallCount = 0;
  m_lastPresentedImageSize = QSize();
  m_lastPresentWallMs = 0;
#if defined(ENABLE_WEBRTC_LIBDATACHANNEL) && defined(ENABLE_FFMPEG)
  m_coalescedPresentImage = QImage();
  m_coalescedPresentFrameId = 0;
  m_coalesceFlushQueued.store(false, std::memory_order_release);
  m_coalescedPresentEpoch.store(0, std::memory_order_release);
  if (m_presentRateTimer)
    m_presentRateTimer->stop();
#endif
  m_lastRtpPacketTime = 0;  // 重置 RTP 包到达时间，避免旧时间干扰诊断
#if defined(ENABLE_WEBRTC_LIBDATACHANNEL) && defined(ENABLE_FFMPEG)
  m_proactiveEncoderHintSent = false;
  m_proactiveEncoderHintDcOpenRetries = 0;
#endif
#ifdef ENABLE_WEBRTC_LIBDATACHANNEL
  m_dataChannelBlockedSendWarnCount = 0;
#endif
  if (m_reconnectTimer && m_reconnectTimer->isActive()) {
    qDebug() << "[Client][WebRTC] prepareForNewConnection: 停止挂起的重连定时器 stream="
             << m_stream;
    m_reconnectTimer->stop();
  }
  m_reconnectScheduled = false;

  if (m_currentReply) {
    qInfo() << "[Client][WebRTC] prepareForNewConnection: abort 进行中的信令 HTTP stream="
            << m_stream;
    QNetworkReply *reply = m_currentReply;
    m_currentReply = nullptr;
    reply->disconnect(this);
    reply->abort();
    reply->deleteLater();
  }

  teardownMediaPipeline();
}

void WebRtcClient::scheduleAutoReconnectIfNeeded(const char *reason, int disconnectWaveId) {
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
    qWarning() << "[Client][WebRTC] scheduleAutoReconnect: 跳过（无 stream/server） reason="
               << reason;
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
    if (!m_reconnectTimer)
      return;
    qWarning() << "[Client][WebRTC] 自动重连次数已达上限（" << kMaxReconnectAttempts
               << "）stream=" << m_stream << " lastReason=" << reason
               << " — 60s 后自动重置计数器并重试";
    updateStatus(QStringLiteral("连接持续断开，60s 后自动恢复…"), false);
    m_reconnectTimer->setInterval(60000);
    m_reconnectTimer->start();
    // 将计数器延迟重置（在下一次定时器触发时）
    connect(
        m_reconnectTimer, &QTimer::timeout, this,
        [this]() {
          m_reconnectCount = 0;
          qInfo() << "[Client][WebRTC] 重连计数器已重置，等待下次触发";
        },
        Qt::SingleShotConnection);
    return;
  }

  // ── 诊断：重连触发时打印完整状态上下文 ─────────────────────────────────────
  const int64_t now = QDateTime::currentMSecsSinceEpoch();
  const int64_t connDuration = (m_connectStartTime > 0) ? (now - m_connectStartTime) : -1;
  const int64_t lastFrameAge = (m_lastFrameTime > 0) ? (now - m_lastFrameTime) : -1;
  qWarning() << "[Client][WebRTC][State][Diag] PeerConnection Closed/Failed stream=" << m_stream
             << " reason=" << reason << " disconnectWaveId=" << disconnectWaveId
             << " 已连接时长=" << connDuration << "ms"
             << " 上帧距今=" << lastFrameAge << "ms"
             << " 本周期帧数=" << m_videoFrameLogCount << " 【诊断结论】："
             << (m_videoFrameLogCount == 0 ? "ZLM 从未发帧(检查 ZLM 推流)"
                 : lastFrameAge > 5000     ? "ZLM 已停发帧(检查 carla-bridge→ZLM 链路)"
                                           : "ICE/UDP 连接断开(ZLM 或网络抖动)");

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
void WebRtcClient::scheduleIngressDrainFromAnyThread() {
  if (!m_decodePipelineActive.load(std::memory_order_acquire))
    return;
  H264Decoder *dec = m_h264Decoder;
  if (!dec)
    return;
  bool expected = false;
  if (!m_ingressDrainPosted.compare_exchange_strong(expected, true, std::memory_order_acq_rel,
                                                    std::memory_order_relaxed))
    return;
  const bool ok = QMetaObject::invokeMethod(dec, "drainRtpIngressQueue", Qt::QueuedConnection);
  if (!ok)
    m_ingressDrainPosted.store(false, std::memory_order_release);
}

void WebRtcClient::onIngressDrainFinished(bool morePending) {
  m_ingressDrainPosted.store(false, std::memory_order_release);
  if (!morePending)
    return;
  if (!m_decodePipelineActive.load(std::memory_order_acquire))
    return;
  H264Decoder *dec = m_h264Decoder;
  if (!dec)
    return;
  bool expected = false;
  if (!m_ingressDrainPosted.compare_exchange_strong(expected, true, std::memory_order_acq_rel,
                                                    std::memory_order_relaxed)) {
    return;
  }
  if (!QMetaObject::invokeMethod(dec, "drainRtpIngressQueue", Qt::QueuedConnection))
    m_ingressDrainPosted.store(false, std::memory_order_release);
}

void WebRtcClient::onDecoderKeyframeSuggested() { requestKeyframeFromSender("h264_decoder"); }

void WebRtcClient::onDecoderDecodeIntegrityAlert(const QString &code, const QString &detail,
                                                 bool mitigationApplied,
                                                 const QString &healthContractLine) {
  VideoDecodeIntegrityEvent evt;
  evt.stream = m_stream;
  evt.code = code;
  evt.detail = detail;
  evt.mitigationApplied = mitigationApplied;
  evt.healthContractLine = healthContractLine;
  EventBus::instance().publish(evt);
  MetricsCollector::instance().increment(QStringLiteral("client.video.decode_integrity_alert_total"));
  MetricsCollector::instance().increment(
      QStringLiteral("client.video.decode_integrity_alert_by_code_%1")
          .arg(metricCodeSuffixForVideo(code)));
  qWarning().noquote() << "[Client][WebRTC][DecodeIntegrity] stream=" << m_stream << " code=" << code
                       << " mitigationApplied=" << mitigationApplied << " detail=" << detail
                       << " healthContract=" << healthContractLine;

  // 编码端「协商」：经 DataChannel 发送可机读 hint（车辆/桥接需自行订阅并实现；无应答时不影响客户端自愈）
  if (mitigationApplied && m_isConnected &&
      code == QLatin1String("MULTI_SLICE_MULTITHREAD_STRIPE_RISK")) {
    QJsonObject o;
    o.insert(QStringLiteral("type"), QStringLiteral("client_video_encoder_hint"));
    o.insert(QStringLiteral("schemaVersion"), 1);
    o.insert(QStringLiteral("stream"), m_stream);
    o.insert(QStringLiteral("preferH264SingleSlice"), true);
    o.insert(QStringLiteral("reasonCode"), code);
    o.insert(QStringLiteral("note"),
             QStringLiteral(
                 "H.264 multi-slice + multi-thread decode caused stripe risk; client forced "
                 "thread_count=1. Prefer encoder slices=1 or single-slice GOP; H.264 has no "
                 "loop_filter_across_slices (HEVC concept)."));
    QJsonObject policy;
    ClientVideoStreamHealth::fillJsonClientVideoPolicy(policy);
    o.insert(QStringLiteral("clientVideoPolicy"), policy);
    if (!healthContractLine.isEmpty())
      o.insert(QStringLiteral("decoderContractAtHint"), healthContractLine);
    emit clientEncoderHintSent(o);
    const QByteArray hintPay = QJsonDocument(o).toJson(QJsonDocument::Compact);
    const bool dcOk = trySendDataChannelMessage(hintPay);
    if (!dcOk) {
      qInfo().noquote()
          << "[Client][VideoHealth][EncoderHint] DataChannel 未发送；MQTT relay 已 emit stream="
          << m_stream;
    }
    qInfo().noquote() << "[Client][VideoHealth][EncoderHint] relay stream=" << m_stream
                      << " dcSent=" << dcOk
                      << " bracket=" << policy.value(QStringLiteral("clientVideoHealthBracket")).toString()
                      << " decoderContractAtHint_len=" << healthContractLine.size()
                      << " ★MQTT 见 [Client][MQTT][EncoderHint]";
  }
}

void WebRtcClient::requestKeyframeFromSender(const char *reason) {
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  if (now - m_lastTrackKeyframeRequestMs < 500)
    return;
  m_lastTrackKeyframeRequestMs = now;
  if (!m_videoTrack)
    return;
  try {
    m_videoTrack->requestKeyframe();
    qInfo() << "[Client][WebRTC][RTCP] requestKeyframe ok stream=" << m_stream
            << " reason=" << reason;
  } catch (const std::exception &e) {
    qWarning() << "[Client][WebRTC][RTCP] requestKeyframe 异常 stream=" << m_stream
               << " reason=" << reason << " err=" << e.what();
  } catch (...) {
    qWarning() << "[Client][WebRTC][RTCP] requestKeyframe 未知异常 stream=" << m_stream
               << " reason=" << reason;
  }
}

namespace {
/**
 * 未设置：默认开。1/true：解码 frameReady → 独立呈现线程合帧/限频 → 主线程输出。
 * 0/false：合帧与限频仍在 GUI 线程。
 */
bool webrtcDecoupledPresentEnabled() {
  const QByteArray v = qgetenv("CLIENT_VIDEO_DECOUPLED_PRESENT");
  if (v.isEmpty())
    return true;
  const QByteArray vl = v.trimmed().toLower();
  return vl != "0" && vl != "false" && vl != "off" && vl != "no";
}
}  // namespace

void WebRtcClient::setupVideoDecoder() {
  if (!m_videoTrack || m_h264Decoder)
    return;
  // ── 诊断：记录从 onTrack 到 decoder setup 的延迟 ───────────────────────────
  const int64_t now = QDateTime::currentMSecsSinceEpoch();
  const int64_t trackDelay = (m_trackReceivedTime > 0) ? (now - m_trackReceivedTime) : -1;
  const int64_t connDelay = (m_connectStartTime > 0) ? (now - m_connectStartTime) : -1;
  qInfo() << "[Client][WebRTC][SDP] setupVideoDecoder stream=" << m_stream
          << " onTrack→setupDelay=" << trackDelay << "ms"
          << " connectStart→setupDelay=" << connDelay << "ms"
          << " framesSinceLast=" << m_framesSinceLastStats.load()
          << " lastFrameAge=" << (m_lastFrameTime > 0 ? (now - m_lastFrameTime) : -1) << "ms";

  if (m_decodeThread || m_h264Decoder) {
    qWarning() << "[Client][WebRTC][WARN] setupVideoDecoder: 残留解码管线 stream=" << m_stream;
    return;
  }
  m_rtpClock = std::make_unique<RtpStreamClockContext>();
  m_rtpVideoSsrc.store(0, std::memory_order_relaxed);
  m_rtpIngressRing = std::make_unique<RtpPacketSpscQueue>(m_stream, m_mediaBudgetSlot);
  ClientMediaBudget::instance().setSlotEnabled(
      m_mediaBudgetSlot,
      m_mediaBudgetSlot >= 0 && m_mediaBudgetSlot < ClientMediaBudget::kMaxSlots);

  m_decodeThread = new QThread();
  m_h264Decoder = new H264Decoder(m_stream, nullptr);
  m_h264Decoder->setIngressQueue(m_rtpIngressRing.get());
  m_h264Decoder->setRtpClockContext(m_rtpClock.get());
  m_ingressDrainPosted.store(false, std::memory_order_release);

  qInfo() << "[Client][WebRTC][setupVideoDecoder] H264Decoder+专用解码线程 stream=" << m_stream
          << " budgetSlot=" << m_mediaBudgetSlot
          << " ★ 媒体线程→SPSC→RTCP SR 时钟→[可选 CLIENT_RTP_PLAYOUT_MODE "
             "jitter]→drain→feedRtp→FFmpeg；"
             "跨路预算 ClientMediaBudget；语义见 RtpTrueJitterBuffer.h";
  if (webrtcDecoupledPresentEnabled()) {
    startPresentPipeline();
    connect(m_h264Decoder, &H264Decoder::frameReady, m_presentWorker,
            &VideoFramePresentWorker::ingestDecoderFrame, Qt::QueuedConnection);
    qInfo() << "[Client][WebRTC][setupVideoDecoder] CLIENT_VIDEO_DECOUPLED_PRESENT=on → "
               "frameReady→PresentWorker thread stream="
            << m_stream;
  } else {
    connect(m_h264Decoder, &H264Decoder::frameReady, this, &WebRtcClient::onVideoFrameFromDecoder,
            Qt::QueuedConnection);
    qInfo() << "[Client][WebRTC][setupVideoDecoder] CLIENT_VIDEO_DECOUPLED_PRESENT=off → "
               "frameReady→GUI coalesce stream="
            << m_stream;
  }
  connect(m_h264Decoder, &H264Decoder::ingressDrainFinished, this,
          &WebRtcClient::onIngressDrainFinished, Qt::QueuedConnection);
  connect(m_h264Decoder, &H264Decoder::senderKeyframeSuggested, this,
          &WebRtcClient::onDecoderKeyframeSuggested, Qt::QueuedConnection);
  connect(m_h264Decoder, &H264Decoder::decodeIntegrityAlert, this,
          &WebRtcClient::onDecoderDecodeIntegrityAlert, Qt::QueuedConnection);
  connect(m_h264Decoder, &H264Decoder::frameReadyDmaBuf, this,
          &WebRtcClient::onDecoderDmaBufFrameReady, Qt::QueuedConnection);
  const int frameReadyReceivers = m_h264Decoder->receiverCountFrameReady();
  const int frameReadyDmaReceivers = m_h264Decoder->receiverCountFrameReadyDmaBuf();
  m_h264Decoder->moveToThread(m_decodeThread);
  m_decodeThread->start();
  m_decodePipelineActive.store(true, std::memory_order_release);
  qInfo() << "[Client][WebRTC][setupVideoDecoder] stream=" << m_stream
          << " m_h264Decoder=" << (void *)m_h264Decoder
          << " decodeThread=" << (void *)m_decodeThread
          << " frameReadyReceivers=" << frameReadyReceivers
          << " frameReadyDmaBufReceivers=" << frameReadyDmaReceivers
          << " ★ frameReadyReceivers=0 → 无人连接 frameReady→主线程呈现";
  qInfo() << "[Client][CodecHealth][Init] stream=" << m_stream
          << " path=H264Decoder+FFmpeg(swscale→RGBA8888) decoupledPresent="
          << (webrtcDecoupledPresentEnabled() ? "on" : "off")
          << " ★每秒grep [Client][CodecHealth][1Hz] stream=" << m_stream
          << " verdict=OK|STALL|WAIT_IDR|…；异常查 [H264][Stats] 与同 stream 的 [H264][decode]";
  ClientVideoStreamHealth::logGlobalEnvOnce();
  qInfo().noquote() << "[Client][VideoHealth][Pipeline] phase=pipeline_bootstrap stream=" << m_stream
                    << " budgetSlot=" << m_mediaBudgetSlot
                    << " decoupledPresent=" << (webrtcDecoupledPresentEnabled() ? "on" : "off")
                    << " " << ClientVideoStreamHealth::globalPolicy().formatGlobalBracket()
                    << " ★本路解码契约在解码线程就绪后打印 [Client][VideoHealth][Stream] phase=path_hw|path_sw";

  // libdatachannel onMessage：仅拷贝入有界 SPSC + 跨路字节预算；解码线程批量 drain（避免每包
  // QueuedConnection）。
  static QAtomicInt s_rtpArrivalLogCount{0};
  static QAtomicInt s_camFrontLogCount{0};
  m_videoTrack->onMessage([this](rtc::message_variant msg) {
    try {
      if (!m_decodePipelineActive.load(std::memory_order_acquire))
        return;
      const quint64 lifecycleId = VideoFrame::nextLifecycleId();

      const int64_t onMsgTime = QDateTime::currentMSecsSinceEpoch();
      const int camFrontSeq = ++s_camFrontLogCount;
      if (camFrontSeq <= 50 || (camFrontSeq % 300) == 0) {
        qInfo() << "[Client][WebRTC][onMessage] ★★★ onMessage 工作线程回调 ★★★"
                << " camFrontSeq=" << camFrontSeq << " stream=" << m_stream
                << " hasVariant=" << msg.index() << " lifecycleId=" << lifecycleId
                << " time=" << onMsgTime
                << " (index: 0=binary/RTP, 1=string/SDP...)";
      }
      if (std::holds_alternative<rtc::binary>(msg)) {
        const auto &bin = std::get<rtc::binary>(msg);
        if (camFrontSeq <= 5 || (camFrontSeq % 300) == 0) {
            qInfo() << "[Client][WebRTC][onMessage] Binary packet received: stream=" << m_stream << " size=" << bin.size();
        }
        if (!m_rtpIngressRing || bin.empty())
          return;
        const uint8_t *const p = reinterpret_cast<const uint8_t *>(bin.data());
        const size_t len = bin.size();
        if (m_rtpClock && len >= 4u) {
          const unsigned v = static_cast<unsigned>(p[0]) >> 6;
          const unsigned pt = static_cast<unsigned>(p[1]);
          if (v == 2u && pt >= 200u && pt <= 204u) {
            QString rlog;
            rtcpCompoundTryConsumeAndUpdateClock(
                p, len, m_rtpClock.get(), QDateTime::currentMSecsSinceEpoch(),
                m_rtpVideoSsrc.load(std::memory_order_relaxed), &rlog);
            return;
          }
        }
        logWebRtcIngressPacketClassify(m_stream, p, len, lifecycleId);
        const int64_t rtpArrivalTime = QDateTime::currentMSecsSinceEpoch();
        const int64_t frameGap =
            (m_lastRtpPacketTime > 0) ? (rtpArrivalTime - m_lastRtpPacketTime) : -1;
        m_lastRtpPacketTime = rtpArrivalTime;
        const int seq = ++s_rtpArrivalLogCount;
        const bool logEveryFrontRtp =
            qEnvironmentVariableIntValue("CLIENT_WEBRTC_LOG_EVERY_RTP_CAM_FRONT") != 0;
        const bool pipeTrace = VideoFrameEvidence::pipelineTraceEnabled();
        const int rtpWarmupLogs = pipeTrace ? 96 : 20;
        const bool logThisPacket =
            seq <= rtpWarmupLogs || frameGap > 100 || frameGap < 0 ||
            (logEveryFrontRtp && m_stream.contains(QStringLiteral("cam_front")));
        if (logThisPacket) {
          qInfo() << "[Client][WebRTC][RTP-Arrival] ★★★ RTP包到达(libdatachannel工作线程) ★★★"
                  << " seq=" << seq << " stream=" << m_stream << " lifecycleId=" << lifecycleId
                  << " pktSize=" << static_cast<qsizetype>(bin.size())
                  << " rtpArrival=" << rtpArrivalTime << " frameGapFromLast=" << frameGap << "ms"
                  << " ringPackets=" << m_rtpIngressRing->packetCount()
                  << " mediaBudgetB=" << ClientMediaBudget::instance().totalBytes()
                  << "（>100ms=发帧慢或网络抖动，<0=首次包）";
        }

        if (len >= 12u) {
          const quint32 ss = (static_cast<quint32>(p[8]) << 24) |
                             (static_cast<quint32>(p[9]) << 16) |
                             (static_cast<quint32>(p[10]) << 8) | static_cast<quint32>(p[11]);
          quint32 cur = m_rtpVideoSsrc.load(std::memory_order_relaxed);
          if (cur == 0u)
            m_rtpVideoSsrc.store(ss, std::memory_order_relaxed);
        }

        RtpIngressPacket ing;
        ing.bytes = QByteArray(reinterpret_cast<const char *>(bin.data()),
                               static_cast<qsizetype>(bin.size()));
        ing.lifecycleId = lifecycleId;
        if (!m_rtpIngressRing->tryPush(std::move(ing))) {
          requestKeyframeFromSender("ingress_ring_or_budget_full");
          return;
        }
        m_framesSinceLastStats.fetch_add(1, std::memory_order_relaxed);
        scheduleIngressDrainFromAnyThread();
      }
    } catch (const std::exception &e) {
      qCritical() << "[Client][WebRTC][onMessage][ERROR] 异常 stream=" << m_stream
                  << " error=" << e.what();
    } catch (...) {
      qCritical() << "[Client][WebRTC][onMessage][ERROR] 未知异常 stream=" << m_stream;
    }
  });
  qDebug() << "[Client][WebRTC] video track RTP -> SPSC ring -> RTCP SR 更新时钟 ->(drain)→ "
              "jitter? -> feedRtp -> frameReady ->(Q)→ 主线程 stream="
           << m_stream;
}

void WebRtcClient::startPresentPipeline() {
  if (m_presentThread)
    return;
  m_presentThread = new QThread();
  m_presentWorker = new VideoFramePresentWorker();
  m_presentWorker->setStreamTag(m_stream);
  m_presentWorker->moveToThread(m_presentThread);
  m_presentThread->start();

  connect(
      m_presentWorker, &VideoFramePresentWorker::frameIngressed, this,
      [this]() { ++m_presentSecVideoSlotEntries; }, Qt::QueuedConnection);
  connect(
      m_presentWorker, &VideoFramePresentWorker::coalescedDropOccurred, this,
      [this]() { ++m_presentSecCoalescedDrops; }, Qt::QueuedConnection);
  connect(
      m_presentWorker, &VideoFramePresentWorker::rateLimitSkipped, this,
      [this]() { ++m_presentSecSkippedRateLimit; }, Qt::QueuedConnection);
  connect(
      m_presentWorker, &VideoFramePresentWorker::flushCoalescedTick, this,
      [this]() { ++m_presentSecFlushCoalescedCount; }, Qt::QueuedConnection);
  connect(m_presentWorker, &VideoFramePresentWorker::presentFrameReady, this,
          &WebRtcClient::onPresentWorkerDeliveredFrame, Qt::QueuedConnection);

  qInfo() << "[Client][WebRTC][Present] startPresentPipeline stream=" << m_stream
          << " thread=" << (void *)m_presentThread << " worker=" << (void *)m_presentWorker;
}

void WebRtcClient::stopPresentPipeline() {
  if (!m_presentThread && !m_presentWorker)
    return;

  if (m_h264Decoder && m_presentWorker) {
    QObject::disconnect(m_h264Decoder, &H264Decoder::frameReady, m_presentWorker,
                        &VideoFramePresentWorker::ingestDecoderFrame);
  }
  if (m_h264Decoder) {
    QObject::disconnect(m_h264Decoder, &H264Decoder::frameReady, this,
                        &WebRtcClient::onVideoFrameFromDecoder);
    QObject::disconnect(m_h264Decoder, &H264Decoder::frameReadyDmaBuf, this,
                        &WebRtcClient::onDecoderDmaBufFrameReady);
  }

  if (m_presentWorker) {
    QObject::disconnect(m_presentWorker, nullptr, this, nullptr);
    const bool ok =
        QMetaObject::invokeMethod(m_presentWorker, "resetState", Qt::BlockingQueuedConnection);
    if (!ok)
      qWarning() << "[Client][WebRTC][Present] resetState BlockingQueued failed stream="
                 << m_stream;
    // Qt 要求：moveToThread 须由对象当前所在线程调用（见 Qt 文档 QObject::moveToThread）。
    const bool moved = QMetaObject::invokeMethod(m_presentWorker, "moveToApplicationThread",
                                                 Qt::BlockingQueuedConnection);
    if (!moved)
      qWarning() << "[Client][WebRTC][Present] moveToApplicationThread invoke failed stream="
                 << m_stream;
    delete m_presentWorker;
    m_presentWorker = nullptr;
  }
  if (m_presentThread) {
    m_presentThread->quit();
    if (!m_presentThread->wait(8000)) {
      qWarning() << "[Client][WebRTC][Present] present thread wait timeout stream=" << m_stream;
      m_presentThread->terminate();
      m_presentThread->wait(2000);
    }
    delete m_presentThread;
    m_presentThread = nullptr;
  }
  qInfo() << "[Client][WebRTC][Present] stopPresentPipeline done stream=" << m_stream;
}

void WebRtcClient::onDecoderDmaBufFrameReady(SharedDmaBufFrame handle, quint64 frameId) {
  try {
    syncPresentBackendStateMachine("dmaBufPresent");
    ++m_framesPendingInQueue;
    m_presentSecMaxPending = std::max(m_presentSecMaxPending, m_framesPendingInQueue);

    if (m_h264Decoder) {
      const int64_t emitMs = m_h264Decoder->lastFrameReadyEmitWallMs();
      const int64_t nowMs = static_cast<int64_t>(QDateTime::currentMSecsSinceEpoch());
      const int64_t lagMs = (emitMs > 0) ? (nowMs - emitMs) : 0;
      if (lagMs >= 0 && lagMs < 30000) {
        m_presentSecMaxQueuedLagMs = std::max(m_presentSecMaxQueuedLagMs, lagMs);
        m_presentSecTotalQueuedLagMs += lagMs;
        ++m_presentSecQueuedLagSamples;
        if (lagMs > 100) {
          qWarning() << "[Client][VideoPresent][QueuedLag][DmaBuf] stream=" << m_stream
                     << " lag=" << lagMs << "ms"
                     << " frameId=" << frameId << " pending=" << m_framesPendingInQueue;
        }
      }
    }

    if (!handle) {
      --m_framesPendingInQueue;
      return;
    }

    const int w = static_cast<int>(handle->frame.width);
    const int h = static_cast<int>(handle->frame.height);
    if (w <= 0 || h <= 0) {
      --m_framesPendingInQueue;
      return;
    }

    if (RemoteVideoSurface *rs = m_boundRemoteSurface.data()) {
      rs->applyDmaBufFrame(handle, frameId, m_stream);
      ++m_presentSecFrames;
      m_lastPresentedImageSize = QSize(w, h);
      m_lastFrameTime = QDateTime::currentMSecsSinceEpoch();
      m_framesSinceLastStats.store(0, std::memory_order_relaxed);
      emit videoFrameReady(w, h, frameId);
    } else {
      ++m_presentSecNullSink;
      static std::atomic<int> s_noSurfLog{0};
      if (s_noSurfLog.fetch_add(1) < 6) {
        qWarning() << "[Client][WebRTC][DmaBuf] 无 RemoteVideoSurface 绑定，无法呈现 DMA-BUF stream="
                   << m_stream << " ★ 使用 bindVideoSurface 或关闭 DMA-BUF 导出";
      }
    }
    --m_framesPendingInQueue;
  } catch (const std::exception &e) {
    qCritical() << "[Client][WebRTC][ERROR] onDecoderDmaBufFrameReady stream=" << m_stream
                << " err=" << e.what();
    --m_framesPendingInQueue;
  } catch (...) {
    qCritical() << "[Client][WebRTC][ERROR] onDecoderDmaBufFrameReady unknown stream=" << m_stream;
    --m_framesPendingInQueue;
  }
}

void WebRtcClient::onPresentWorkerDeliveredFrame(QImage image, quint64 frameId) {
  try {
    ++m_framesPendingInQueue;
    m_presentSecMaxPending = std::max(m_presentSecMaxPending, m_framesPendingInQueue);

    if (m_h264Decoder) {
      const int64_t emitMs = m_h264Decoder->lastFrameReadyEmitWallMs();
      const int64_t nowMs = static_cast<int64_t>(QDateTime::currentMSecsSinceEpoch());
      const int64_t lagMs = (emitMs > 0) ? (nowMs - emitMs) : 0;
      if (lagMs >= 0 && lagMs < 30000) {
        m_presentSecMaxQueuedLagMs = std::max(m_presentSecMaxQueuedLagMs, lagMs);
        m_presentSecTotalQueuedLagMs += lagMs;
        ++m_presentSecQueuedLagSamples;
        if (lagMs > 100) {
          qWarning() << "[Client][VideoPresent][QueuedLag][Decoupled] stream=" << m_stream
                     << " lag=" << lagMs << "ms"
                     << " frameId=" << frameId << " pending=" << m_framesPendingInQueue
                     << " ★ worker→主线程排队或主线程仍阻塞";
        }
      }
    }

    if (image.isNull()) {
      --m_framesPendingInQueue;
      return;
    }

    presentDecodedFrameToOutputs(image, frameId);
    --m_framesPendingInQueue;
  } catch (const std::exception &e) {
    qCritical() << "[Client][WebRTC][ERROR] onPresentWorkerDeliveredFrame: stream=" << m_stream
                << " err=" << e.what();
    --m_framesPendingInQueue;
  } catch (...) {
    qCritical() << "[Client][WebRTC][ERROR] onPresentWorkerDeliveredFrame unknown stream="
                << m_stream;
    --m_framesPendingInQueue;
  }
}

namespace {
/** 未设置或 1：合并多帧解码回调，仅向 QVideoSink 呈现最新帧；0：每帧立即呈现（旧行为） */
bool videoPresentCoalesceEnabled() {
  const QByteArray v = qgetenv("CLIENT_VIDEO_PRESENT_COALESCE");
  if (v.isEmpty())
    return true;
  const QByteArray vl = v.trimmed().toLower();
  return vl != "0" && vl != "false" && vl != "off";
}
/**
 * 呈现侧最大帧率（全路各自独立）。0=不限频（仍合并队列内重复 flush）。
 * 默认 30：软 GL/四路时与合成能力对齐，减少 setVideoFrame 风暴导致的闪烁。
 */
int videoMaxPresentFps() {
  bool ok = false;
  const int n = qEnvironmentVariableIntValue("CLIENT_VIDEO_MAX_PRESENT_FPS", &ok);
  if (!ok)
    return 30;
  return n;
}
}  // namespace

void WebRtcClient::flushCoalescedVideoPresent() {
  m_coalesceFlushQueued.store(false, std::memory_order_release);

  if (m_coalescedPresentImage.isNull())
    return;

  const int maxFps = videoMaxPresentFps();
  const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
  if (maxFps > 0 && m_lastPresentWallMs > 0) {
    const qint64 minIntervalMs = qMax(qint64(1), (1000 + maxFps / 2) / maxFps);
    const qint64 elapsed = nowMs - m_lastPresentWallMs;
    if (elapsed < minIntervalMs) {
      ++m_presentSecSkippedRateLimit;
      if (!m_presentRateTimer) {
        m_presentRateTimer = new QTimer(this);
        m_presentRateTimer->setSingleShot(true);
        connect(m_presentRateTimer, &QTimer::timeout, this,
                &WebRtcClient::flushCoalescedVideoPresent);
      }
      if (!m_presentRateTimer->isActive())
        m_presentRateTimer->start(int(minIntervalMs - elapsed));
      return;
    }
  }

  const quint64 epochBefore = m_coalescedPresentEpoch.load(std::memory_order_acquire);
  const QImage img = m_coalescedPresentImage;
  const quint64 fid = m_coalescedPresentFrameId;
  presentDecodedFrameToOutputs(img, fid);
  ++m_presentSecFlushCoalescedCount;
  m_lastPresentWallMs = QDateTime::currentMSecsSinceEpoch();

  const quint64 epochAfter = m_coalescedPresentEpoch.load(std::memory_order_acquire);
  if (epochAfter != epochBefore) {
    bool expected = false;
    if (m_coalesceFlushQueued.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
      QMetaObject::invokeMethod(
          this, [this]() { flushCoalescedVideoPresent(); }, Qt::QueuedConnection);
  }
}

void WebRtcClient::presentDecodedFrameToOutputs(const QImage &image, quint64 frameId) {
  static int s_presentCount = 0;
  if (s_presentCount <= 5 || (s_presentCount % 300) == 0) {
      qInfo() << "[Client][WebRTC][Present] presentDecodedFrameToOutputs stream=" << m_stream
               << " frameId=" << frameId << " size=" << image.size()
               << " totalPresent=" << s_presentCount;
  }
  s_presentCount++;
  if (image.isNull() || image.width() <= 0 || image.height() <= 0) {
    qWarning() << "[Client][WebRTC][Present] 跳过无效/不完整尺寸 stream=" << m_stream
               << " frameId=" << frameId << " size=" << image.size();
    return;
  }

  static QSet<QString> s_loggedStreams;
  if (!s_loggedStreams.contains(m_stream)) {
    s_loggedStreams.insert(m_stream);
    qInfo() << "[Client][WebRTC][Present] first frame stream=" << m_stream
            << " frameId=" << frameId;
  }

  m_lastPresentedImageSize = image.size();

  const quint64 lifecycleId = m_h264Decoder ? m_h264Decoder->currentLifecycleId() : 0;
  (void)lifecycleId;

  // ── CPU模式性能优化：消除 presentDecodedFrameToOutputs 内的冗余 copy() ─────────
  // 原因同 onVideoFrameFromDecoder 注释：H264Decoder 3槽帧池 + Qt 隐式共享已保证线程安全。
  //   - image 来自 flushCoalescedVideoPresent 的局部变量 img（= m_coalescedPresentImage），
  //     其生命周期跨越本函数调用，refcount≥1；
  //   - QVideoFrame(const QImage &) 使用隐式共享（再次 refcount++），渲染器持有 vf 时
  //     像素数据不会被释放；
  //   - 无显式 copy，主线程仅做 QVideoFrame 封装 + setVideoFrame，避免 8MB 堆分配。
  const QImage &displayFrame = image;  // 隐式共享引用，无像素拷贝

  const int64_t handlerEnterTime = QDateTime::currentMSecsSinceEpoch();
  QElapsedTimer handlerTimer;
  const bool presentTrace = qEnvironmentVariableIntValue("CLIENT_VIDEO_PRESENT_TRACE") != 0;
  if (presentTrace)
    handlerTimer.start();

  m_videoFrameLogCount++;
  syncPresentBackendStateMachine("presentDecodedFrameToOutputs");

  if (VideoFrameEvidence::shouldLogVideoStage(frameId)) {
    qInfo().noquote() << VideoFrameEvidence::diagLine("MAIN_PRESENT", m_stream, frameId,
                                                      displayFrame);
  }

  if (m_lastFrameTime > 0) {
    const int64_t frameInterval = handlerEnterTime - m_lastFrameTime;
    if (frameInterval > 2000) {
      qWarning() << "[Client][WebRTC][FrameGap] stream=" << m_stream << " gap=" << frameInterval
                 << "ms";
    }
    if (qEnvironmentVariableIntValue("CLIENT_VIDEO_FRAME_INTERVAL_TRACE") != 0) {
      if (frameInterval < 5 || frameInterval > 150) {
        static QHash<QString, int> s_fiCount;
        const int fc = ++s_fiCount[m_stream];
        if (fc <= 100 || (fc % 50) == 0) {
          qInfo() << "[Client][Present][FrameInterval] stream=" << m_stream
                  << " dtMs=" << frameInterval << " pendingInQueue=" << m_framesPendingInQueue
                  << " frame#" << m_videoFrameLogCount
                  << " ★间隔过小/过大：与软渲染、主线程积压、或解码异常相关，设本 env=1 启用";
        }
      }
    }
  }
  m_lastFrameTime = handlerEnterTime;
  m_framesSinceLastStats.store(0, std::memory_order_relaxed);

  if (m_videoFrameLogCount <= 10) {
    qInfo() << "[Client][VideoFrame] stream=" << m_stream << " frame#" << m_videoFrameLogCount
            << " size=" << displayFrame.size()
            << " sink=" << static_cast<const void *>(activeSink());
  }

  if (m_framesPendingInQueue > 2 && m_videoFrameLogCount <= 20) {
    qWarning() << "[Client][WebRTC][Queue] stream=" << m_stream
               << " pending=" << m_framesPendingInQueue;
  }
    if (m_videoFrameLogCount <= 5 || (m_videoFrameLogCount % 60) == 0) {
      RemoteVideoSurface *const rsPath = m_boundRemoteSurface.data();
      QVideoSink *const skPath = activeSink();
      const char *branch = "none";
      if (rsPath)
        branch = "remote_surface";
      else if (skPath)
        branch = (skPath == m_ownedSink) ? "qvideo_sink_owned" : "qvideo_sink_bound";
      qInfo() << "[Client][VideoPresent][Path] stream=" << m_stream << " frame#"
              << m_videoFrameLogCount << " frameId=" << frameId
              << " presentBackend=" << webRtcPresentBackendTag(m_presentBackendSnapshot)
              << " branch=" << branch
              << " remoteSurfacePtr=" << static_cast<const void *>(rsPath)
              << " activeVideoSinkPtr=" << static_cast<const void *>(skPath)
              << " bindOutCnt=" << m_bindVideoOutputCallCount
              << " bindSurfCnt=" << m_bindVideoSurfaceCallCount
              << " pendingInQueue=" << m_framesPendingInQueue
              << " ★ branch=真实呈现路径；bind*Cnt=本连接周期计数(prepare 清零后与指针可能不一致)";
      qInfo() << "[Client][VideoPresent][Diag] stream=" << m_stream << " frame#"
              << m_videoFrameLogCount << " frameId=" << frameId
              << " pendingInQueue=" << m_framesPendingInQueue
              << " ★ 详见同帧 [VideoPresent][Path] 的 branch=/remoteSurfacePtr=/activeVideoSinkPtr=";
    }

  QElapsedTimer mainThreadFrameBudget;
  mainThreadFrameBudget.start();
  try {
    if (m_boundRemoteSurface) {
      if (RemoteVideoSurface *rs = m_boundRemoteSurface.data()) {
        bool didPresent = false;
        QImage frameForAdopt = image;
        if (auto cpu = CpuVideoRgba8888Frame::tryAdopt(std::move(frameForAdopt))) {
          rs->applyCpuRgba8888Frame(std::move(*cpu), frameId, m_stream);
          MetricsCollector::instance().increment(
              QStringLiteral("client.video.cpu_rgba8888_present_apply_total"));
          didPresent = true;
        } else {
          MetricsCollector::instance().increment(
              QStringLiteral("client.video.cpu_rgba8888_producer_violation_total"));
          static std::atomic<int> s_rgbaViol{0};
          const int vn = s_rgbaViol.fetch_add(1, std::memory_order_relaxed);
          if (vn < 32 || (vn % 200) == 0) {
            qCritical().noquote()
                << "[Client][WebRTC][RgbaContract] producer violation path=RemoteVideoSurface stream="
                << m_stream << " frameId=" << frameId
                << " qimgFmt=" << static_cast<int>(image.format()) << " bpl=" << image.bytesPerLine()
                << " size=" << image.size() << " expect=Format_RGBA8888+bpl>=4*w strictCpuFmt="
                << (ClientVideoStreamHealth::cpuPresentFormatStrict() ? 1 : 0) << " "
                << ClientVideoDiagCache::videoPipelineEnvFingerprint();
          }
          if (!ClientVideoStreamHealth::cpuPresentFormatStrict()) {
            rs->applyFrame(image, frameId, m_stream);
            didPresent = true;
          }
        }
        if (didPresent) {
          ++m_presentSecFrames;
          const int64_t afterPresent = QDateTime::currentMSecsSinceEpoch();
          if (presentTrace) {
            const int64_t sinceLast =
                (m_lastPresentWallMs > 0) ? (afterPresent - m_lastPresentWallMs) : -1;
            const qint64 handlerUs = handlerTimer.nsecsElapsed() / 1000;
            const bool slow = handlerUs > 8000;
            if ((m_videoFrameLogCount % 30) == 0 || slow) {
              qInfo() << "[Client][VideoPresent] stream=" << m_stream << " frameId=" << frameId
                      << " lifecycleId=" << lifecycleId
                      << " pendingInQueue=" << m_framesPendingInQueue
                      << " sinceLastPresentMs=" << sinceLast << " handlerUs=" << handlerUs
                      << " image=" << displayFrame.size()
                      << " path=RemoteVideoSurface rs=" << (void *)rs;
            }
            m_lastPresentWallMs = afterPresent;
          }
        }
      } else {
        ++m_presentSecNullSink;
        qCritical() << "[Client][WebRTC][RemoteSurface] QPointer expired stream=" << m_stream;
      }
    } else if (QVideoSink *sink = activeSink()) {
      // Qt 6：QVideoSink::setVideoFrame（官方文档）https://doc.qt.io/qt-6/qvideosink.html#setVideoFrame
      // 与 RemoteVideoSurface 一致：默认 strict 下仅接受 RGBA8888 且 bpl>=4*w，否则拒绝 setVideoFrame。
      const bool strictCpuPresentFmt = ClientVideoStreamHealth::cpuPresentFormatStrict();
      QImage sinkFrame = image;
      const auto sinkCpuContractOk = [](const QImage &img) -> bool {
        if (img.isNull() || img.width() <= 0 || img.height() <= 0)
          return false;
        if (img.format() != QImage::Format_RGBA8888)
          return false;
        return img.bytesPerLine() >= img.width() * 4;
      };

      if (!sinkCpuContractOk(sinkFrame)) {
        if (strictCpuPresentFmt) {
          MetricsCollector::instance().increment(
              QStringLiteral("client.video.qvideosink_present_contract_reject_total"));
          static std::atomic<int> s_sinkRej{0};
          const int rn = s_sinkRej.fetch_add(1, std::memory_order_relaxed);
          if (rn < 48 || (rn % 200) == 0) {
            qCritical().noquote()
                << "[Client][WebRTC][VideoSink][PresentContract][REJECT] stream=" << m_stream
                << " frameId=" << frameId << " qimgFmt=" << static_cast<int>(sinkFrame.format())
                << " bpl=" << sinkFrame.bytesPerLine() << " size=" << sinkFrame.size()
                << " strictCpuFmt=1 expect=Format_RGBA8888+bpl>=4*w "
                << ClientVideoDiagCache::videoPipelineEnvFingerprint()
                << " ★临时放行：CLIENT_VIDEO_CPU_PRESENT_FORMAT_STRICT=0（convertToFormat，可能掩盖上游 bug）";
          }
        } else {
          QImage c = sinkFrame.convertToFormat(QImage::Format_RGBA8888);
          if (!sinkCpuContractOk(c)) {
            static std::atomic<int> s_convFail{0};
            if (s_convFail.fetch_add(1, std::memory_order_relaxed) < 16) {
              qCritical() << "[Client][WebRTC][VideoSink][PresentContract][CONVERT_FAIL] stream="
                          << m_stream << " frameId=" << frameId
                          << " fromFmt=" << static_cast<int>(sinkFrame.format());
            }
            MetricsCollector::instance().increment(
                QStringLiteral("client.video.qvideosink_present_convert_fail_total"));
          } else {
            static std::atomic<int> s_convLog{0};
            if (s_convLog.fetch_add(1, std::memory_order_relaxed) < 8) {
              qWarning().noquote()
                  << "[Client][WebRTC][VideoSink][PresentContract][CONVERT] stream=" << m_stream
                  << " fromFmt=" << static_cast<int>(sinkFrame.format())
                  << " → Format_RGBA8888 ★ 上游应仅输出 RGBA8888";
            }
            sinkFrame = std::move(c);
          }
        }
      }

      if (sinkCpuContractOk(sinkFrame)) {
        const QVideoFrame vf(sinkFrame);
        if (vf.isValid()) {
          sink->setVideoFrame(vf);
          ++m_presentSecFrames;
          const QSize sinkVs = sink->videoSize();
          const QSize imgSz = sinkFrame.size();
          const bool traceVs =
              qEnvironmentVariableIntValue("CLIENT_VIDEO_SINK_SIZE_MISMATCH_TRACE") != 0;
          if (sinkVs.isValid() && sinkVs != imgSz) {
            if (traceVs || m_videoFrameLogCount <= 30 || (m_videoFrameLogCount % 60) == 0) {
              qWarning() << "[Client][WebRTC][VideoSink] post-setVideoFrame videoSize!=image stream="
                         << m_stream << " sinkVideoSize=" << sinkVs << " image=" << imgSz << " frame#"
                         << m_videoFrameLogCount
                         << " ★ 见 Qt6 QVideoSink::videoSize / setVideoFrame "
                            "文档；持续不一致再查像素格式与缩放";
            }
          } else if (!sinkVs.isValid() && imgSz.width() > 0 && imgSz.height() > 0) {
            if (traceVs || m_videoFrameLogCount <= 20 || (m_videoFrameLogCount % 120) == 0) {
              qWarning() << "[Client][WebRTC][VideoSink] post-setVideoFrame videoSize invalid stream="
                         << m_stream << " image=" << imgSz << " frame#" << m_videoFrameLogCount
                         << " ★ sink 未报告尺寸：VideoOutput 未就绪或平台延迟；对照 QML "
                            "[VideoOutputGeom] 日志";
            }
          }
          const int64_t afterPresent = QDateTime::currentMSecsSinceEpoch();
          if (presentTrace) {
            const int64_t sinceLast =
                (m_lastPresentWallMs > 0) ? (afterPresent - m_lastPresentWallMs) : -1;
            const qint64 handlerUs = handlerTimer.nsecsElapsed() / 1000;
            const bool slow = handlerUs > 8000;
            if ((m_videoFrameLogCount % 30) == 0 || slow) {
              qInfo() << "[Client][VideoPresent] stream=" << m_stream << " frameId=" << frameId
                      << " lifecycleId=" << lifecycleId
                      << " pendingInQueue=" << m_framesPendingInQueue
                      << " sinceLastPresentMs=" << sinceLast << " handlerUs=" << handlerUs
                      << " image=" << sinkFrame.size() << " vfValid=1 sink=" << (void *)sink;
            }
            m_lastPresentWallMs = afterPresent;
          }
        } else {
          ++m_presentSecInvalidVf;
          qWarning() << "[Client][WebRTC][VideoSink] QVideoFrame(QImage) 无效 stream=" << m_stream
                     << " fmt=" << sinkFrame.format();
          if (presentTrace) {
            qWarning() << "[Client][VideoPresent] vfInvalid stream=" << m_stream
                       << " frameId=" << frameId << " pendingInQueue=" << m_framesPendingInQueue;
          }
        }
      }
    } else {
      ++m_presentSecNullSink;
      qCritical() << "[Client][WebRTC][VideoSink] activeSink=null stream=" << m_stream;
    }

    emit videoFrameReady(displayFrame.width(), displayFrame.height(), frameId);
  } catch (const std::exception &e) {
    qCritical() << "[Client][WebRTC][ERROR] videoSink/videoFrameReady 异常 stream=" << m_stream
                << " error=" << e.what();
  } catch (...) {
    qCritical() << "[Client][WebRTC][ERROR] videoSink/videoFrameReady 未知异常 stream=" << m_stream;
  }

  {
    const qint64 handlerUs = mainThreadFrameBudget.nsecsElapsed() / 1000;
    m_presentSecMaxHandlerUs = std::max(m_presentSecMaxHandlerUs, handlerUs);
    const int envTh = qEnvironmentVariableIntValue("CLIENT_VIDEO_SLOW_PRESENT_US");
    const int thresholdUs = envTh > 0 ? envTh : 8000;
    if (handlerUs >= thresholdUs) {
      ++m_presentSecSlow;
      if (m_videoFrameLogCount <= 40 || (m_videoFrameLogCount % 30) == 0) {
        qWarning() << "[Client][WebRTC][MainThreadBudget] stream=" << m_stream
                   << " presentPathUs=" << handlerUs << " pendingAfter=" << m_framesPendingInQueue
                   << " frame#" << m_videoFrameLogCount
                   << " ★ 本槽内：QVideoFrame+setVideoFrame 或 RemoteSurface::applyFrame+emit";
      }
    }
  }

  m_lastHandlerDoneTime = QDateTime::currentMSecsSinceEpoch();
}

void WebRtcClient::onVideoFrameFromDecoder(const QImage &image, quint64 frameId) {
  try {
    static int s_decoderSlotInCount = 0;
    if (s_decoderSlotInCount <= 5 || (s_decoderSlotInCount % 300) == 0) {
        qInfo() << "[Client][WebRTC][Slot] onVideoFrameFromDecoder stream=" << m_stream
                 << " frameId=" << frameId << " size=" << image.size()
                 << " totalSlotIn=" << s_decoderSlotInCount;
    }
    s_decoderSlotInCount++;
    ++m_presentSecVideoSlotEntries;
    ++m_framesPendingInQueue;
    m_presentSecMaxPending = std::max(m_presentSecMaxPending, m_framesPendingInQueue);

    // ★ 测量 QueuedConnection 事件队列延迟
    // m_h264Decoder->lastFrameReadyEmitWallMs() 是解码线程最后一次 emit frameReady 的墙钟 ms
    // 当前墙钟与其差值 = 此帧在 Qt 事件队列中等待执行的时间
    if (m_h264Decoder) {
      const int64_t emitMs = m_h264Decoder->lastFrameReadyEmitWallMs();
      const int64_t nowMs = static_cast<int64_t>(QDateTime::currentMSecsSinceEpoch());
      const int64_t lagMs = (emitMs > 0) ? (nowMs - emitMs) : 0;
      if (lagMs >= 0 && lagMs < 30000) {  // 合理范围内才统计，排除时钟异常
        m_presentSecMaxQueuedLagMs = std::max(m_presentSecMaxQueuedLagMs, lagMs);
        m_presentSecTotalQueuedLagMs += lagMs;
        ++m_presentSecQueuedLagSamples;
        // 超过 100ms 立即警告：表明主线程事件循环严重阻塞
        if (lagMs > 100) {
          qWarning() << "[Client][VideoPresent][QueuedLag] ★ 事件队列严重阻塞 stream=" << m_stream
                     << " lag=" << lagMs << "ms"
                     << " frameId=" << frameId << " pending=" << m_framesPendingInQueue
                     << " → 主线程卡顿，解码帧在队列中等待超 100ms";
        }
      }
    }

    if (image.isNull()) {
      qCritical() << "[WebRTC][FrameDiag] 空视频帧 frameId=" << frameId << " stream=" << m_stream;
      --m_framesPendingInQueue;
      return;
    }

    if (videoPresentCoalesceEnabled()) {
      // ── CPU模式性能优化：消除冗余 image.copy() ──────────────────────────────────
      // 原有 image.copy() 在主线程执行，4路×30fps×8MB = 960 MB/s 额外拷贝开销。
      //
      // 安全性分析（Qt 隐式共享）：
      //   1. H264Decoder 使用 3槽帧池（kFramePoolSize=3），emit frameReady(dstFrame) 后
      //      主线程通过 QueuedConnection 持有同一 QImage（refcount≥2）；
      //   2. 解码线程下一帧 dstFrame.detach() 检测 refcount>1 → 新分配，不覆写旧数据；
      //   3. 本槽 m_coalescedPresentImage = image 仅递增 refcount（隐式共享），无像素复制；
      //   4. flushCoalescedVideoPresent 在主线程事件循环顺序执行，不存在并发写。
      // 结论：两者均在主线程，无竞态；Qt 隐式共享已保证数据安全，无需显式 copy()。
      const bool wasCoalesced = !m_coalescedPresentImage.isNull();
      m_coalescedPresentImage = image;  // 隐式共享（refcount++），无像素拷贝
      m_coalescedPresentFrameId = frameId;
      m_coalescedPresentEpoch.fetch_add(1, std::memory_order_relaxed);
      // ★ 统计 coalescing 丢帧：若已有待呈现帧（flush 未触发），本次覆盖即为丢帧
      if (wasCoalesced) {
        ++m_presentSecCoalescedDrops;
      }
      bool expected = false;
      if (m_coalesceFlushQueued.compare_exchange_strong(expected, true,
                                                        std::memory_order_acq_rel)) {
        QMetaObject::invokeMethod(
            this, [this]() { flushCoalescedVideoPresent(); }, Qt::QueuedConnection);
      }
      --m_framesPendingInQueue;
      return;
    }

    presentDecodedFrameToOutputs(image, frameId);
    --m_framesPendingInQueue;
  } catch (const std::exception &e) {
    qCritical() << "[Client][WebRTC][ERROR] onVideoFrameFromDecoder 总异常: stream=" << m_stream
                << " error=" << e.what();
    --m_framesPendingInQueue;
  } catch (...) {
    qCritical() << "[Client][WebRTC][ERROR] onVideoFrameFromDecoder 未知异常: stream=" << m_stream;
    --m_framesPendingInQueue;
  }
}
#endif

void WebRtcClient::onRemoteSurfaceDmaBufSceneGraphFailed(const QString& reason) {
#if defined(ENABLE_WEBRTC_LIBDATACHANNEL)
  qWarning() << "[Client][WebRTC][DmaBufSG] 请求 CPU RGBA 回退 stream=" << m_stream << " detail=" << reason;
  emit videoPresentationDegraded(m_stream, reason);
  // 切 CPU 纹理后尽快要 IDR，减少参考帧/纹理路径切换带来的花屏或长时间不清晰
  requestKeyframeFromSender("dma_buf_sg_cpu_fallback");
#  if defined(ENABLE_FFMPEG)
  if (m_h264Decoder) {
    const bool ok = QMetaObject::invokeMethod(
        m_h264Decoder, &H264Decoder::onDmaBufSceneGraphPresentFailed, Qt::QueuedConnection);
    if (!ok) {
      qCritical() << "[Client][WebRTC][DmaBufSG] invoke onDmaBufSceneGraphPresentFailed failed "
                     "stream="
                  << m_stream;
    }
  }
#  endif
#else
  Q_UNUSED(reason);
#endif
}

void WebRtcClient::trySendProactiveEncoderDisplayContractHint() {
#if defined(ENABLE_WEBRTC_LIBDATACHANNEL) && defined(ENABLE_FFMPEG)
  constexpr int kMaxDcOpenRetries = 40;
  if (m_proactiveEncoderHintSent || !m_isConnected)
    return;
  if (!m_dataChannel || !m_dataChannel->isOpen()) {
    if (m_proactiveEncoderHintDcOpenRetries >= kMaxDcOpenRetries) {
      qWarning() << "[Client][VideoHealth][EncoderHint][Proactive] give_up stream=" << m_stream
                 << " retries=" << m_proactiveEncoderHintDcOpenRetries
                 << " ★DataChannel 未打开，无法发送单 slice 契约 hint";
      m_proactiveEncoderHintSent = true;
      return;
    }
    ++m_proactiveEncoderHintDcOpenRetries;
    QTimer::singleShot(150, this, &WebRtcClient::trySendProactiveEncoderDisplayContractHint);
    return;
  }
  QJsonObject o;
  o.insert(QStringLiteral("type"), QStringLiteral("client_video_encoder_hint"));
  o.insert(QStringLiteral("schemaVersion"), 2);
  o.insert(QStringLiteral("stream"), m_stream);
  o.insert(QStringLiteral("preferH264SingleSlice"), true);
  o.insert(QStringLiteral("reasonCode"), QStringLiteral("PROACTIVE_DISPLAY_CONTRACT"));
  o.insert(QStringLiteral("note"),
           QStringLiteral(
               "Proactive: prefer H.264 single-slice per frame for decode/display stability on "
               "multi-camera clients (reduces multi-slice + multi-thread stripe risk). "
               "Reactive hint may still follow if integrity alerts fire."));
  QJsonObject policy;
  ClientVideoStreamHealth::fillJsonClientVideoPolicy(policy);
  o.insert(QStringLiteral("clientVideoPolicy"), policy);
  m_proactiveEncoderHintSent = true;
  emit clientEncoderHintSent(o);
  const QByteArray proactivePay = QJsonDocument(o).toJson(QJsonDocument::Compact);
  if (!trySendDataChannelMessage(proactivePay)) {
    qWarning().noquote()
        << "[Client][VideoHealth][EncoderHint][Proactive] DC trySend failed after open; MQTT relay "
           "已 emit stream="
        << m_stream;
  }
  qInfo().noquote()
      << "[Client][VideoHealth][EncoderHint][Proactive] sent stream=" << m_stream
      << " bracket=" << policy.value(QStringLiteral("clientVideoHealthBracket")).toString()
      << " effectiveDmaExport="
      << policy.value(QStringLiteral("clientWebRtcHwExportDmaBufEffective")).toBool()
      << " ★车端/编码器宜优先单 slice；MQTT 转发见 connectEncoderHintMqttRelay";
#else
  Q_UNUSED(this);
#endif
}

bool WebRtcClient::isDataChannelOpen() const {
#ifdef ENABLE_WEBRTC_LIBDATACHANNEL
  return m_dataChannel && m_dataChannel->isOpen();
#else
  return false;
#endif
}

bool WebRtcClient::trySendDataChannelMessage(const QByteArray &data) {
  if (!m_isConnected || data.isEmpty())
    return false;
#ifdef ENABLE_WEBRTC_LIBDATACHANNEL
  if (!m_dataChannel || !m_dataChannel->isOpen())
    return false;
  try {
    m_dataChannel->send(std::string(data.data(), data.length()));
    m_dataChannelBlockedSendWarnCount = 0;
    qDebug() << "[Client][WebRTC] Sent data channel message:" << data;
    return true;
  } catch (const std::exception &e) {
    qCritical() << "[Client][WebRTC][ERROR] Failed to send data channel message:" << e.what();
    return false;
  } catch (...) {
    qCritical()
        << "[Client][WebRTC][ERROR] Failed to send data channel message: unknown exception";
    return false;
  }
#else
  qDebug() << "trySendDataChannelMessage (simulated, no libdatachannel):" << data.size() << "B";
  return true;
#endif
}

void WebRtcClient::sendDataChannelMessage(const QByteArray &data) {
  if (!m_isConnected) {
    qWarning() << "[Client][WebRTC][WARN] Cannot send message: not connected";
    return;
  }
  if (trySendDataChannelMessage(data))
    return;

#ifdef ENABLE_WEBRTC_LIBDATACHANNEL
  if (m_dataChannelBlockedSendWarnCount < 5) {
    ++m_dataChannelBlockedSendWarnCount;
    qWarning() << "[Client][WebRTC][WARN] DataChannel not open, cannot send message stream=" << m_stream;
  } else if (m_dataChannelBlockedSendWarnCount == 5) {
    ++m_dataChannelBlockedSendWarnCount;
    qWarning() << "[Client][WebRTC][WARN] DataChannel not open: suppressing further send warnings until "
                  "DC opens or reconnect stream="
               << m_stream;
  }
#else
  qDebug() << "Sending data channel message (simulated):" << data;
#endif
}

QString WebRtcClient::videoFrameReadySignalMeta() const {
  const QMetaObject *mo = metaObject();
  // 查找 videoFrameReady 信号（3 参数：宽、高、frameId，无 QImage）
  QStringList results;
  for (int i = mo->methodOffset(); i < mo->methodCount(); ++i) {
    QMetaMethod method = mo->method(i);
    if (method.methodType() == QMetaMethod::Signal &&
        QString::fromLatin1(method.name()) == QLatin1String("videoFrameReady")) {
      QString sig = QString::fromLatin1(method.methodSignature());
      results.append(
          QStringLiteral("index=%1 params=%2 sig=%3").arg(i).arg(method.parameterCount()).arg(sig));
    }
  }
  return results.isEmpty() ? QStringLiteral("NOT FOUND") : results.join(QLatin1String(" | "));
}

int WebRtcClient::receiverCountVideoFrameReady() const {
  return receivers(SIGNAL(videoFrameReady(int, int, quint64)));
}

int WebRtcClient::rtpDecodeQueueDepth() const {
#if defined(ENABLE_WEBRTC_LIBDATACHANNEL) && defined(ENABLE_FFMPEG)
  if (m_rtpIngressRing)
    return static_cast<int>(m_rtpIngressRing->packetCount());
  return 0;
#else
  return 0;
#endif
}

void WebRtcClient::setMediaBudgetSlot(int slot) {
#if defined(ENABLE_WEBRTC_LIBDATACHANNEL) && defined(ENABLE_FFMPEG)
  if (slot >= 0 && slot < ClientMediaBudget::kMaxSlots)
    m_mediaBudgetSlot = slot;
  else
    m_mediaBudgetSlot = -1;
#else
  Q_UNUSED(slot);
#endif
}

void WebRtcClient::forceRefresh() {
  qDebug() << "[Client][WebRTC] forceRefresh: QVideoSink/VideoOutput 路径无需 SceneGraph 强制刷新 "
              "stream="
           << m_stream;
}

void WebRtcClient::updateStatus(const QString &status, bool connected) {
  m_statusText = status;
  m_isConnected = connected;
  emit statusTextChanged(m_statusText);
  emit connectionStatusChanged(m_isConnected);
}
