#include "webrtcstreammanager.h"

#include "app/client_app_bootstrap.h"
#include "app/client_system_stall_diag.h"
#include "core/metricscollector.h"
#include "core/tracing.h"
#include "utils/WebRtcUrlResolve.h"
#include "mqttcontroller.h"
#include "vehiclemanager.h"

#ifdef ENABLE_FFMPEG
#include "infrastructure/media/DecoderFactory.h"
#include "media/ClientVideoStreamHealth.h"
#endif

#include <QDateTime>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMetaMethod>
#include <QMutex>
#include <QMutexLocker>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcessEnvironment>
#include <QSet>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#include <atomic>
#include <cstdlib>
#ifdef ENABLE_QT6_OPENGL
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#endif
#if defined(Q_OS_UNIX)
#include <sys/resource.h>
#endif

namespace {

/** 端到端视频拉流诊断：每次 connectFourStreams 自增，便于 grep 单会话全链路 */
std::atomic_int g_streamE2eConnectSeq{0};

QString streamE2eFmtUrlProbe(const QString &url) {
  if (url.isEmpty())
    return QStringLiteral("empty");
  const QUrl u(url);
  if (!u.isValid() || u.host().isEmpty())
    return QStringLiteral("len=%1 invalid_or_no_host head=%2").arg(url.size()).arg(url.left(96));
  return QStringLiteral("len=%1 scheme=%2 host=%3 port=%4 pathHead=%5")
      .arg(url.size())
      .arg(u.scheme())
      .arg(u.host())
      .arg(u.port())
      .arg(u.path().left(64));
}

bool envTruthyBytes(const QByteArray &raw) {
  const QString t = QString::fromLatin1(raw).trimmed().toLower();
  return t == QLatin1String("1") || t == QLatin1String("true") || t == QLatin1String("yes") ||
         t == QLatin1String("on");
}

/** @return 本车四路 {vin}_cam_* 在 ZLM getMediaList data 数组中的在册路数 0..4 */
int countVinCamStreamsInZlmMediaList(const QJsonArray &mediaList, const QString &vin) {
  const QString vinPfx = vin.isEmpty() ? QString() : (vin + QStringLiteral("_"));
  const QString expect[] = {vinPfx + QStringLiteral("cam_front"), vinPfx + QStringLiteral("cam_rear"),
                            vinPfx + QStringLiteral("cam_left"), vinPfx + QStringLiteral("cam_right")};
  int presentMask = 0;
  for (const QJsonValue &v : mediaList) {
    const QString st = v.toObject().value(QStringLiteral("stream")).toString();
    for (int i = 0; i < 4; ++i) {
      if (st == expect[i])
        presentMask |= (1 << i);
    }
  }
  return (presentMask & 1) + ((presentMask >> 1) & 1) + ((presentMask >> 2) & 1) +
         ((presentMask >> 3) & 1);
}
bool videoPresent1HzSummaryEnabled() {
  const QByteArray raw = qgetenv("CLIENT_VIDEO_PRESENT_1HZ_SUMMARY");
  if (raw.isEmpty())
    return true;
  const QByteArray t = raw.trimmed().toLower();
  return t != "0" && t != "false" && t != "off" && t != "no";
}

/** 运行期 QueuedLag 持续超阈 SLO（关：CLIENT_VIDEO_RUNTIME_SLO=0） */
bool videoRuntimeQueuedLagSloEnabled() {
  const QByteArray raw = qgetenv("CLIENT_VIDEO_RUNTIME_SLO");
  if (raw.isEmpty())
    return true;
  const QByteArray t = raw.trimmed().toLower();
  return t != "0" && t != "false" && t != "off" && t != "no";
}

QString fmtPresentArm(const QString &abbr, WebRtcClient *c, const WebRtcPresentSecondStats &s,
                      int decEmit, int ingressTryPushFailDelta) {
  if (!c)
    return QString();
  const QSize vs = c->diagnosticPresentSize();
  const QString vsz =
      vs.isValid() ? QStringLiteral("%1x%2").arg(vs.width()).arg(vs.height()) : QStringLiteral("-");
  const QString dEstr = (decEmit < 0) ? QStringLiteral("-") : QString::number(decEmit);
  const QString iDropStr =
      (ingressTryPushFailDelta < 0) ? QStringLiteral("-") : QString::number(ingressTryPushFailDelta);
  // dE = 解码线程 emit frameReady 次数（与 H264Decoder 原子计数同源）；n = 主线程
  // QVideoSink::setVideoFrame 成功次数 h = 当前 pendingVideoHandlerDepth；rg = RTP 入环包数（与
  // totPend 中 rtpRing 同源） iDrop=本秒入环失败(环/预算满)；vse=主线程视频槽每秒进入次数；fc=合帧
  return QStringLiteral(
             " %1{n%2,sl%3,mxP%4,umax%5,vsz%6,bO%7,bS%8,ns%9,iv%10,c%11,h%12,rg%13,iDrop%14,dE%15,"
             "skR%16,vse%17,fc%18}")
      .arg(abbr)
      .arg(s.framesToSink)
      .arg(s.slowPresent)
      .arg(s.maxPending)
      .arg(s.maxHandlerUs)
      .arg(vsz)
      .arg(c->bindVideoOutputInvocationCount())
      .arg(c->bindVideoSurfaceInvocationCount())
      .arg(s.nullSink)
      .arg(s.invalidVf)
      .arg(c->isConnected() ? 1 : 0)
      .arg(c->pendingVideoHandlerDepth())
      .arg(c->rtpDecodeQueueDepth())
      .arg(iDropStr)
      .arg(dEstr)
      .arg(s.skippedPresentRateLimit)
      .arg(s.videoSlotEntries)
      .arg(s.flushCoalescedCount);
}

void warnDecodePresentGap(const char *armTag, WebRtcClient *c, int dE,
                          const WebRtcPresentSecondStats &s) {
  if (!c || !c->isConnected() || dE < 0)
    return;
  const int n = s.framesToSink;
  const int gap = dE - n;
  if (gap > 5) {
    qWarning().noquote()
        << QStringLiteral(
               "[Client][VideoPresent][1Hz][E2E-QueuedLag] arm=%1 dE=%2 n=%3 gap=%4 ★ decode emit "
               "frameReady 快于主线程 setVideoFrame；查 GUI 阻塞 / LIBGL_SW / 日志风暴")
               .arg(QString::fromUtf8(armTag))
               .arg(dE)
               .arg(n)
               .arg(gap);
  }
  if (n > dE + 1) {
    qWarning().noquote() << QStringLiteral(
                                "[Client][VideoPresent][1Hz][E2E-CountMismatch] arm=%1 dE=%2 n=%3 "
                                "★ 计数异常；查是否多次 drain 或解码器重建")
                                .arg(QString::fromUtf8(armTag))
                                .arg(dE)
                                .arg(n);
  }
  if (dE > 0 && n == 0) {
    qWarning().noquote()
        << QStringLiteral(
               "[Client][VideoPresent][1Hz][E2E-DecodeNoSink] arm=%1 dE=%2 n=0 ★ 有 emit、无 "
               "setVideoFrame；查 activeSink=null / vf 无效 / 槽未跑完")
               .arg(QString::fromUtf8(armTag))
               .arg(dE);
  }
}

void warnDecoderVsSlotGap(const char *armTag, WebRtcClient *c, int dE,
                          const WebRtcPresentSecondStats &s) {
  if (!c || !c->isConnected() || dE < 0)
    return;
  const int vse = s.videoSlotEntries;
  if (dE > vse + 8) {
    qWarning().noquote() << QStringLiteral(
                                "[Client][VideoPresent][1Hz][DecoderVsSlot] arm=%1 dE=%2 vse=%3 "
                                "gap=%4 ★ decode emit 远多于主线程 "
                                "onVideoFrameFromDecoder 执行次数 → Queued "
                                "事件堆积或主线程长期阻塞（系统级卡滞信号）")
                                .arg(QString::fromUtf8(armTag))
                                .arg(dE)
                                .arg(vse)
                                .arg(dE - vse);
  }
}
}  // namespace

static const char kStreamFront[] = "cam_front";
static const char kStreamRear[] = "cam_rear";
static const char kStreamLeft[] = "cam_left";
static const char kStreamRight[] = "cam_right";

void WebRtcStreamManager::connectEncoderHintMqttRelay(MqttController *mqtt) {
  if (!mqtt) {
    qWarning() << "[StreamManager][EncoderHint] MqttController=null, skip MQTT relay hookup";
    return;
  }
  const auto hook = [mqtt](const QJsonObject &o) { mqtt->publishClientEncoderHint(o); };
  for (WebRtcClient *c : {m_front, m_rear, m_left, m_right}) {
    if (!c)
      continue;
    QObject::connect(c, &WebRtcClient::clientEncoderHintSent, mqtt, hook);
  }
  qInfo() << "[StreamManager][EncoderHint] 4×WebRtcClient::clientEncoderHintSent → "
             "MqttController::publishClientEncoderHint (topic teleop/client_encoder_hint)";
}

WebRtcStreamManager::WebRtcStreamManager(QObject *parent)
    : QObject(parent),
      m_front(nullptr),
      m_rear(nullptr),
      m_left(nullptr),
      m_right(nullptr),
      m_app(QStringLiteral("teleop")),
      m_vin(),
      m_vehicleManager(nullptr),
      m_currentBase(),
      m_zlmPollTimer(nullptr),
      m_presentDiag1HzTimer(nullptr),
      m_lastZlmStreamsSeen(),
      m_lastEmittedAnyConnected(false),
      m_qmlLayerEventsThisSecond(0),
      m_glInfoLoggedOnce(false),
      m_runtimeHighQueuedLagStreakSec(0),
      m_runtimeLowQueuedLagStreakSec(0),
      m_runtimeQueuedLagSloBreached(false),
      m_streamsConnectedVin(),
      m_zlmReadyTimer(nullptr),
      m_zlmReadyNam(nullptr),
      m_zlmReadyReply(nullptr),
      m_zlmReadyWhep(),
      m_zlmReadyDeadlineMs(0),
      m_zlmReadyPollInFlight(false) {
  m_front = new WebRtcClient(this);
  m_rear = new WebRtcClient(this);
  m_left = new WebRtcClient(this);
  m_right = new WebRtcClient(this);

  m_front->setMediaBudgetSlot(0);
  m_rear->setMediaBudgetSlot(1);
  m_left->setMediaBudgetSlot(2);
  m_right->setMediaBudgetSlot(3);

#ifdef ENABLE_FFMPEG
  ClientVideoStreamHealth::logGlobalEnvOnce();
#endif

  // ── ★★★ 端到端追踪：四路 WebRtcClient 创建完成，QML 即将可见 ★★★ ─────────────
  qInfo() << "[StreamManager][init] ★ WebRtcClient×4 创建完成 ★"
          << " front=" << (void *)m_front << " rear=" << (void *)m_rear
          << " left=" << (void *)m_left << " right=" << (void *)m_right
          << " ★ 对比 DrivingInterface.qml streamClient 绑定日志"
          << " ★ 对比 QML console.log webrtcStreamManager.frontClient 检查绑定是否正常";

  {
    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString sw = env.value(QStringLiteral("LIBGL_ALWAYS_SOFTWARE")).trimmed();
    bool fpsOk = false;
    const int maxPresFps = qEnvironmentVariableIntValue("CLIENT_VIDEO_MAX_PRESENT_FPS", &fpsOk);
    qInfo() << "[StreamManager][init][DisplayEnv] LIBGL_ALWAYS_SOFTWARE=" << sw
            << " QT_XCB_GL_INTEGRATION=" << env.value(QStringLiteral("QT_XCB_GL_INTEGRATION"))
            << " QSG_RHI_BACKEND=" << env.value(QStringLiteral("QSG_RHI_BACKEND"))
            << " CLIENT_VIDEO_PRESENT_COALESCE=" << qgetenv("CLIENT_VIDEO_PRESENT_COALESCE")
            << "(空=开)"
            << " CLIENT_VIDEO_DECOUPLED_PRESENT=" << qgetenv("CLIENT_VIDEO_DECOUPLED_PRESENT")
            << "(空=开→合帧在呈现线程)"
            << " CLIENT_VIDEO_MAX_PRESENT_FPS=" << (fpsOk ? maxPresFps : -1)
            << "(未设默认30;0=不限)"
            << " CLIENT_VIDEO_PRESENT_SHARE_IMAGE=" << qgetenv("CLIENT_VIDEO_PRESENT_SHARE_IMAGE")
            << "(1=跳过copy省CPU;默认copy保完整帧)"
            << " CLIENT_MAIN_THREAD_STALL_DIAG="
            << env.value(QStringLiteral("CLIENT_MAIN_THREAD_STALL_DIAG"))
            << " CLIENT_VIDEO_SCENE_GL_LOG="
            << env.value(QStringLiteral("CLIENT_VIDEO_SCENE_GL_LOG"))
            << " CLIENT_PERF_DIAG=" << env.value(QStringLiteral("CLIENT_PERF_DIAG"))
            << " CLIENT_VIDEO_PERF_JSON_1HZ="
            << env.value(QStringLiteral("CLIENT_VIDEO_PERF_JSON_1HZ"))
            << " CLIENT_VIDEO_PERF_METRICS_1HZ="
            << env.value(QStringLiteral("CLIENT_VIDEO_PERF_METRICS_1HZ"))
            << " CLIENT_VIDEO_PERF_TRACE_SPAN="
            << env.value(QStringLiteral("CLIENT_VIDEO_PERF_TRACE_SPAN"))
            << " CLIENT_H264_SWS_DIAG=" << qgetenv("CLIENT_H264_SWS_DIAG")
            << "(0=关 非0=sws/像素格式诊断)"
            << " CLIENT_VIDEO_FORENSICS=" << qgetenv("CLIENT_VIDEO_FORENSICS")
            << "(1=CRC+加量TexSize/SG 取证)"
            << " CLIENT_VIDEO_SAVE_FRAME=" << qgetenv("CLIENT_VIDEO_SAVE_FRAME")
            << "(非空=解码落盘 PNG/RAW 路径前缀)"
            << " CLIENT_ABORT_IF_SOFTWARE_VIDEO_STACK="
            << qgetenv("CLIENT_ABORT_IF_SOFTWARE_VIDEO_STACK")
            << "(1=软件GL栈时直接exit79 钉环境问题)"
            << " ★ 四路实时视频目标形态：硬件 GL；软渲染仅排障/CI"
            << " ★ 系统级卡滞：开 CLIENT_MAIN_THREAD_STALL_DIAG=1 看 [SysStall]；开 "
               "CLIENT_VIDEO_SCENE_GL_LOG=1 钉 GL_RENDERER"
            << " ★ SLO/多服务：CLIENT_PERF_DIAG=1 一键默认开 1Hz "
               "JSON+metrics+trace（仅对未设置的子变量生效）";
#ifdef ENABLE_FFMPEG
    qInfo() << "[StreamManager][init][DecoderFactory] availableDecoders="
            << DecoderFactory::availableDecoders()
            << " ★ WebRTC 四路当前为 H264Decoder(FFmpeg avcodec CPU + sws→RGBA8888)，与 "
               "MediaPipeline+DecoderFactory 硬解路径独立；硬解走 ZLM/管道时需单独部署";
#endif
    if (!sw.isEmpty() && sw != QLatin1String("0") && sw.toLower() != QLatin1String("false")) {
      qWarning()
          << "[StreamManager][init][DisplayEnv] ★★★ LIBGL_ALWAYS_SOFTWARE 已启用：四路 "
             "RemoteVideoSurface/VideoOutput "
             "负载高；请优先硬件 GL，或车端降载：carla-bridge 环境变量 CAMERA_FPS=10（默认）/5 ★★★";
    }
    if (envTruthyBytes(qgetenv("CLIENT_ABORT_IF_SOFTWARE_VIDEO_STACK"))) {
      if (!ClientApp::lastHardwarePresentationEnvironmentOk()) {
        qCritical().noquote()
            << "[StreamManager][DisplayGate] CLIENT_ABORT_IF_SOFTWARE_VIDEO_STACK=1 且 "
               "lastHardwarePresentationEnvironmentOk=false（LIBGL_ALWAYS_SOFTWARE 或 GL "
               "探测判定软件光栅）。"
               "四路视频呈现不可靠；请 unset LIBGL_ALWAYS_SOFTWARE 并启用 GPU "
               "驱动，或取消本变量以继续调试。"
               " 证伪像素路径可设 CLIENT_H264_SWS_DIAG=1。 exit=79";
        std::exit(79);
      }
    }
  }

  m_lastEmittedAnyConnected = anyConnected();

  // 任一路 connectionStatusChanged 都会进来；仅当四路 OR 聚合结果变化时才 emit
  // anyConnectedChanged， 否则 QML 侧 Connections/onAnyConnectedChanged 会在「同一次 ICE
  // 跌落」内被触发数十次，造成状态栏/占位层抖动。
  auto onPerStreamConnectionChanged = [this]() {
    static int64_t s_lastTimestamp = 0;
    const int64_t now = QDateTime::currentMSecsSinceEpoch();
    const int64_t delta = s_lastTimestamp > 0 ? (now - s_lastTimestamp) : -1;
    s_lastTimestamp = now;
    const bool f = m_front && m_front->isConnected();
    const bool rear = m_rear && m_rear->isConnected();
    const bool l = m_left && m_left->isConnected();
    const bool r = m_right && m_right->isConnected();
    const bool aggNow = anyConnected();
    const bool aggChanged = (aggNow != m_lastEmittedAnyConnected);
    qInfo().noquote() << "[StreamManager][Diag] perStreamConn"
                      << " aggregateNow=" << aggNow << " aggregateChanged=" << aggChanged
                      << " f=" << f << " rear=" << rear << " l=" << l << " r=" << r
                      << " deltaMs=" << delta << " now=" << now
                      << "【delta 极小且 aggregateChanged=false → 单路抖动未改聚合；仍刷屏请查 "
                         "WebRtcClient 状态】";
    if (aggChanged) {
      m_lastEmittedAnyConnected = aggNow;
      emit anyConnectedChanged();
    }
  };
  connect(m_front, &WebRtcClient::connectionStatusChanged, this, onPerStreamConnectionChanged);
  connect(m_rear, &WebRtcClient::connectionStatusChanged, this, onPerStreamConnectionChanged);
  connect(m_left, &WebRtcClient::connectionStatusChanged, this, onPerStreamConnectionChanged);
  connect(m_right, &WebRtcClient::connectionStatusChanged, this, onPerStreamConnectionChanged);

  connect(m_front, &WebRtcClient::zlmSnapshotRequested, this,
          &WebRtcStreamManager::onZlmSnapshotRequested);
  connect(m_rear, &WebRtcClient::zlmSnapshotRequested, this,
          &WebRtcStreamManager::onZlmSnapshotRequested);
  connect(m_left, &WebRtcClient::zlmSnapshotRequested, this,
          &WebRtcStreamManager::onZlmSnapshotRequested);
  connect(m_right, &WebRtcClient::zlmSnapshotRequested, this,
          &WebRtcStreamManager::onZlmSnapshotRequested);

  // ── 诊断：周期性查询 ZLM API，验证推流是否在册 ───────────────────────────────
  // 若 ZLM getMediaList 中无 {vin}_cam_* 流，说明推流断连，客户端断流是正常行为
  // 若 ZLM getMediaList 中有流但客户端断流，说明 RTCP/ICE 问题
  m_zlmPollTimer = new QTimer(this);
  m_zlmPollTimer->setInterval(15000);  // 每 15s 查一次，避免频繁
  connect(m_zlmPollTimer, &QTimer::timeout, this, [this]() {
    {
      checkZlmStreamRegistration();
    }  
  });
  m_zlmPollTimer->start();
  qInfo() << "[StreamManager][Diag] ZLM 流状态轮询已启动，周期=15s";

  m_zlmReadyNam = new QNetworkAccessManager(this);
  m_zlmReadyTimer = new QTimer(this);
  m_zlmReadyTimer->setSingleShot(false);
  connect(m_zlmReadyTimer, &QTimer::timeout, this, &WebRtcStreamManager::onZlmReadyTimerTick);

  m_presentDiag1HzTimer = new QTimer(this);
  m_presentDiag1HzTimer->setInterval(1000);
  connect(m_presentDiag1HzTimer, &QTimer::timeout, this,
          &WebRtcStreamManager::emitVideoPresent1HzSummary);
  m_presentDiag1HzTimer->start();
  if (videoPresent1HzSummaryEnabled()) {
    qInfo().noquote() << "[StreamManager][VideoPresent][1Hz] 已启动 interval=1000ms 关闭: "
                         "CLIENT_VIDEO_PRESENT_1HZ_SUMMARY=0"
                      << " Qt6 doc: https://doc.qt.io/qt-6/qvideosink.html#setVideoFrame"
                      << " arm字段: dE=解码emit frameReady n=setVideoFrame h=handler深度 rg=RTP环"
                      << " 闪烁分类 grep: [Client][VideoFlickerClass][1Hz]"
                      << " falsify: unset LIBGL_ALWAYS_SOFTWARE | single-stream | lower resolution";
  }
}

WebRtcStreamManager::~WebRtcStreamManager() {
  disconnectAll();
  if (m_zlmReadyTimer) {
    m_zlmReadyTimer->stop();
    m_zlmReadyTimer->deleteLater();
    m_zlmReadyTimer = nullptr;
  }
  if (m_presentDiag1HzTimer) {
    m_presentDiag1HzTimer->stop();
    m_presentDiag1HzTimer->deleteLater();
    m_presentDiag1HzTimer = nullptr;
  }
  if (m_zlmPollTimer) {
    m_zlmPollTimer->stop();
    m_zlmPollTimer->deleteLater();
    m_zlmPollTimer = nullptr;
  }
}

void WebRtcStreamManager::setStreamingReady(bool ready) {
  if (m_streamingReady != ready) {
    m_streamingReady = ready;
    qInfo().noquote() << "[StreamManager][Status] streamingReady 变更 →" << ready
                     << "★ 来自 VehicleStatus 反馈；就绪后 pollTimer 仍将运行以验证 ZLM 实例状态";
    emit streamingReadyChanged(m_streamingReady);
    
    // 如果就绪，且正在等待 ZLM 就绪，则可以尝试提前拉流
    if (ready && m_zlmReadyTimer && m_zlmReadyTimer->isActive()) {
        qInfo().noquote() << "[StreamManager][ZlmReady] 车端已上报 streamingReady=true，提前触发 connectFourStreams";
        m_zlmReadyTimer->stop();
        connectFourStreams(m_zlmReadyWhep);
    }
  }
}

void WebRtcStreamManager::setCurrentVin(const QString &vin) {
  const QString was = m_vin;
  if (was == vin) {
    qDebug().noquote() << QStringLiteral("[Client][StreamE2E][SET_VIN] no-op same vin=")
                       << (vin.isEmpty() ? QStringLiteral("(empty)") : vin) << "m_currentBase="
                       << (m_currentBase.isEmpty() ? QStringLiteral("(empty)") : m_currentBase)
                       << "anyConnected=" << anyConnected();
    return;
  }
  m_vin = vin;
  qInfo().noquote() << QStringLiteral("[Client][StreamE2E][SET_VIN] vin_changed was=")
                    << (was.isEmpty() ? QStringLiteral("(empty)") : was)
                    << "now=" << (vin.isEmpty() ? QStringLiteral("(empty)") : vin)
                    << "m_currentBase="
                    << (m_currentBase.isEmpty() ? QStringLiteral("(empty)") : m_currentBase)
                    << "anyConnected=" << anyConnected() << "vehicleMgrVin="
                    << (m_vehicleManager ? (m_vehicleManager->currentVin().isEmpty()
                                                ? QStringLiteral("(empty)")
                                                : m_vehicleManager->currentVin())
                                         : QStringLiteral("(null_vm)"))
                    << "★ 若 now 空：流名前缀将退化为 cam_*；若 was 非空→空：常与登出/清车一致";
}

void WebRtcStreamManager::emitVideoPresent1HzSummary() {
  if (!videoPresent1HzSummaryEnabled())
    return;

  // ── 首次调用时打印实际 OpenGL 驱动信息，确认硬件/软件 GL ──────────────────────────────
  // 解决方案验证用：若 GL_RENDERER 含 "llvmpipe" 或 "softpipe" 则说明仍在软件渲染，需排查 GPU
  // 挂载。 若含 "NVIDIA" / "Intel" / "Radeon" 等则硬件加速已生效，本闪烁根因已消除。
  if (!m_glInfoLoggedOnce) {
    m_glInfoLoggedOnce = true;
#ifdef ENABLE_QT6_OPENGL
    QOpenGLContext *glCtx = QOpenGLContext::currentContext();
    if (glCtx) {
      QOpenGLFunctions *glf = glCtx->functions();
      if (glf) {
        const char *vendor = reinterpret_cast<const char *>(glf->glGetString(GL_VENDOR));
        const char *renderer = reinterpret_cast<const char *>(glf->glGetString(GL_RENDERER));
        const char *version = reinterpret_cast<const char *>(glf->glGetString(GL_VERSION));
        const QString rendStr = QString::fromLatin1(renderer ? renderer : "null");
        const bool isSoftware = rendStr.contains(QLatin1String("llvmpipe"), Qt::CaseInsensitive) ||
                                rendStr.contains(QLatin1String("softpipe"), Qt::CaseInsensitive) ||
                                rendStr.contains(QLatin1String("software"), Qt::CaseInsensitive);
        const QString glLine =
            QStringLiteral(
                "[Client][VideoPresent][GL_INFO] vendor=%1 renderer=%2 version=%3"
                " | software_rasterizer=%4"
                " ★ 若 software_rasterizer=1 则 GPU 未生效，请检查 docker-compose.yml "
                "devices挂载及宿主机 /dev/dri")
                .arg(QString::fromLatin1(vendor ? vendor : "null"))
                .arg(rendStr)
                .arg(QString::fromLatin1(version ? version : "null"))
                .arg(isSoftware ? 1 : 0);
        if (isSoftware) {
          qWarning().noquote() << glLine;
        } else {
          qInfo().noquote() << glLine;
        }
      }
    } else {
      qInfo().noquote() << "[Client][VideoPresent][GL_INFO] "
                           "QOpenGLContext::currentContext()=nullptr（主线程无激活 GL 上下文）"
                           " — GL Renderer 字符串无法在此时机获取；可在 "
                           "QQuickWindow::sceneGraphInitialized 信号中查询";
    }
#else
    qInfo().noquote()
        << "[Client][VideoPresent][GL_INFO] Qt6::OpenGL 模块未链接（ENABLE_QT6_OPENGL 未定义）"
           " — 跳过 GL_RENDERER 查询；libgl_sw 仍来自 LIBGL_ALWAYS_SOFTWARE 环境变量";
#endif
  }
  // ─────────────────────────────────────────────────────────────────────────────────────────

  // 先取解码侧 emit 计数再取呈现侧，二者均为「自上一 1Hz tick 以来」，用于钉死
  // decode→Queued→setVideoFrame
  const int decF = m_front ? m_front->drainDecoderFrameReadyEmitDiagCount() : -1;
  const int decR = m_rear ? m_rear->drainDecoderFrameReadyEmitDiagCount() : -1;
  const int decL = m_left ? m_left->drainDecoderFrameReadyEmitDiagCount() : -1;
  const int decRi = m_right ? m_right->drainDecoderFrameReadyEmitDiagCount() : -1;

  const int idF = m_front ? m_front->drainIngressTryPushFailDiagCount() : -1;
  const int idR = m_rear ? m_rear->drainIngressTryPushFailDiagCount() : -1;
  const int idL = m_left ? m_left->drainIngressTryPushFailDiagCount() : -1;
  const int idRi = m_right ? m_right->drainIngressTryPushFailDiagCount() : -1;

  const WebRtcPresentSecondStats sf =
      m_front ? m_front->drainPresentSecondStats() : WebRtcPresentSecondStats{};
  const WebRtcPresentSecondStats sr =
      m_rear ? m_rear->drainPresentSecondStats() : WebRtcPresentSecondStats{};
  const WebRtcPresentSecondStats sl =
      m_left ? m_left->drainPresentSecondStats() : WebRtcPresentSecondStats{};
  const WebRtcPresentSecondStats sri =
      m_right ? m_right->drainPresentSecondStats() : WebRtcPresentSecondStats{};

  // ★ 性能优化：仅在必要时访问 QProcessEnvironment（非常昂贵，涉及全局锁/系统调用）
  static bool swOn = false;
  static std::once_flag s_swOnce;
  std::call_once(s_swOnce, []() {
      const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
      const QString sw = env.value(QStringLiteral("LIBGL_ALWAYS_SOFTWARE")).trimmed();
      swOn = !sw.isEmpty() && sw != QLatin1String("0") && sw.toLower() != QLatin1String("false");
  });

  const int totPend = getTotalPendingFrames();
  // totPend = 四路 pendingVideoHandlerDepth + rtpIngressRing；拆开后避免把「RTP
  // 环堆积」误判为「主线程槽排队」
  int sumHandler = 0;
  int sumRtpRing = 0;
  auto acc = [&](const WebRtcClient *c) {
    if (!c)
      return;
    sumHandler += c->pendingVideoHandlerDepth();
    sumRtpRing += c->rtpDecodeQueueDepth();
  };
  acc(m_front);
  acc(m_rear);
  acc(m_left);
  acc(m_right);

  const int maxMxP = qMax(qMax(sf.maxPending, sr.maxPending), qMax(sl.maxPending, sri.maxPending));
  const int slowSum = sf.slowPresent + sr.slowPresent + sl.slowPresent + sri.slowPresent;
  const qint64 maxUsec =
      qMax(qMax(sf.maxHandlerUs, sr.maxHandlerUs), qMax(sl.maxHandlerUs, sri.maxHandlerUs));

  QString line;
  line.reserve(720);
  line =
      QStringLiteral("[Client][VideoPresent][1Hz] libgl_sw=%1 totPend=%2 handler=%3 rtpRing=%4 |")
          .arg(swOn ? 1 : 0)
          .arg(totPend)
          .arg(sumHandler)
          .arg(sumRtpRing);
  line += fmtPresentArm(QStringLiteral("Fr"), m_front, sf, decF, idF);
  line += fmtPresentArm(QStringLiteral("Re"), m_rear, sr, decR, idR);
  line += fmtPresentArm(QStringLiteral("Le"), m_left, sl, decL, idL);
  line += fmtPresentArm(QStringLiteral("Ri"), m_right, sri, decRi, idRi);
  line += QStringLiteral(
      " | present=QVideoSink|RemoteSurface+SceneGraph "
      "doc=https://doc.qt.io/qt-6/qvideosink.html#setVideoFrame");
  qInfo().noquote() << line;

  warnDecodePresentGap("Fr", m_front, decF, sf);
  warnDecodePresentGap("Re", m_rear, decR, sr);
  warnDecodePresentGap("Le", m_left, decL, sl);
  warnDecodePresentGap("Ri", m_right, decRi, sri);
  warnDecoderVsSlotGap("Fr", m_front, decF, sf);
  warnDecoderVsSlotGap("Re", m_rear, decR, sr);
  warnDecoderVsSlotGap("Le", m_left, decL, sl);
  warnDecoderVsSlotGap("Ri", m_right, decRi, sri);

  // 软件 GL 时 cat 恒为 SOFTWARE_GL，容易掩盖「主路本秒未 setVideoFrame」与 QML
  // 盖层问题；单独钉一条
  if (swOn && m_front && m_front->isConnected() && sf.framesToSink == 0) {
    qWarning().noquote() << QStringLiteral(
                                "[Client][VideoPresent][1Hz][SWGL+FrontPresentZero] libgl_sw=1 "
                                "且前路 isConnected=1 但本秒 Fr.n=0（无 QVideoSink::setVideoFrame）"
                                " ★ 对照 Fr.dE 与 [H264][Stats] emitted；dE>0 见 "
                                "[E2E-DecodeNoSink]；dE=0 查 WebRTC/解码/ZLM")
                         << " Fr.dE=" << decF
                         << " pendingFront=" << m_front->pendingVideoHandlerDepth()
                         << " rtpRingFront=" << m_front->rtpDecodeQueueDepth();
  }

  // 硬件 GL 仍闪烁时的收敛规则（见日志一行即可判因）
  if (!swOn) {
    if (sumHandler > 1) {
      qWarning().noquote() << QStringLiteral(
                                  "[Client][VideoPresent][1Hz][HWGL][疑点A] libgl_sw=0 但 "
                                  "sumHandler=%1 >1 ★ 主线程呈现计数异常或重入；"
                                  " 开 CLIENT_VIDEO_PRESENT_TRACE=1 对照单帧 handlerUs")
                                  .arg(sumHandler);
    }
    if (slowSum > 0) {
      qWarning().noquote()
          << QStringLiteral(
                 "[Client][VideoPresent][1Hz][HWGL][疑点B] slowPresent 四路合计=%1 "
                 "maxHandlerUs(max)=%2 µs ★ 单帧 setVideoFrame+emit 仍超"
                 " CLIENT_VIDEO_SLOW_PRESENT_US；主线程算力/同步阻塞，非纯 QML 遮罩")
                 .arg(slowSum)
                 .arg(maxUsec);
    }
    if (maxMxP > 2) {
      qWarning().noquote() << QStringLiteral(
                                  "[Client][VideoPresent][1Hz][HWGL][疑点C] 秒内队列峰值 "
                                  "maxPending=%1 ★ 解码→QueuedConnection 在单路或多路上堆积；"
                                  " 查是否嵌套事件循环或 handler 过慢")
                                  .arg(maxMxP);
    }
    if (sumRtpRing >= 160 && sumHandler <= 1 && slowSum == 0) {
      qInfo().noquote()
          << QStringLiteral(
                 "[Client][VideoPresent][1Hz][HWGL][提示] sumRtpRing=%1 偏高、handler/slow 正常 ★ "
                 "闪烁更可能来自网络抖动/ingress"
                 " 或 SceneGraph 与 VSync，而非 QML 占位层；仍闪请对照 [LayerFlickerRisk] 日志")
                 .arg(sumRtpRing);
    }
  }

  // ── 单一分类行：钉死「主线程 / RTP 环 / QML 盖层」── 优先 grep: [Client][VideoFlickerClass][1Hz]
  const int qml1s = m_qmlLayerEventsThisSecond.exchange(0, std::memory_order_acq_rel);

  // ★ 聚合四路 QueuedConnection 延迟与 coalescing 丢帧（在 swOn 分支内也输出，不再屏蔽）
  const int64_t maxQueuedLag =
      qMax(qMax(sf.maxQueuedLagMs, sr.maxQueuedLagMs), qMax(sl.maxQueuedLagMs, sri.maxQueuedLagMs));
  const int64_t avgQueuedLag =
      (sf.avgQueuedLagMs + sr.avgQueuedLagMs + sl.avgQueuedLagMs + sri.avgQueuedLagMs) / 4;
  const int totalCoalescedDrops =
      sf.coalescedDrops + sr.coalescedDrops + sl.coalescedDrops + sri.coalescedDrops;

  // 输出 QueuedConnection 延迟汇总行（无论 swOn 与否，只要有数据就输出）
  if (maxQueuedLag > 0 || totalCoalescedDrops > 0) {
    QString lagLine =
        QStringLiteral(
            "[Client][VideoPresent][1Hz][QueuedLag] maxLag=%1ms avgLag=%2ms coalescedDrops=%3"
            " | Fr(lag=%4ms,drop=%5) Re(lag=%6ms,drop=%7) Le(lag=%8ms,drop=%9) "
            "Ri(lag=%10ms,drop=%11)"
            " ★ maxLag>50ms→主线程事件循环阻塞；coalescedDrops高→帧被跳过未呈现")
            .arg(maxQueuedLag)
            .arg(avgQueuedLag)
            .arg(totalCoalescedDrops)
            .arg(sf.maxQueuedLagMs)
            .arg(sf.coalescedDrops)
            .arg(sr.maxQueuedLagMs)
            .arg(sr.coalescedDrops)
            .arg(sl.maxQueuedLagMs)
            .arg(sl.coalescedDrops)
            .arg(sri.maxQueuedLagMs)
            .arg(sri.coalescedDrops);
    if (maxQueuedLag > 50 || totalCoalescedDrops > 10) {
      qWarning().noquote() << lagLine;
    } else {
      qInfo().noquote() << lagLine;
    }
  }

  // ── 运行期呈现 SLO：与是否打印 [QueuedLag] 行无关，每 1s 更新 streak ──
  if (videoRuntimeQueuedLagSloEnabled()) {
    static constexpr int64_t kHighLagMs = 80;
    static constexpr int kBreachConsecutiveSec = 5;
    static constexpr int kRecoverConsecutiveSec = 5;
    static constexpr int64_t kRecoverBelowMs = 35;

    if (maxQueuedLag >= kHighLagMs) {
      ++m_runtimeHighQueuedLagStreakSec;
      m_runtimeLowQueuedLagStreakSec = 0;
    } else {
      m_runtimeHighQueuedLagStreakSec = 0;
      if (maxQueuedLag <= kRecoverBelowMs)
        ++m_runtimeLowQueuedLagStreakSec;
      else
        m_runtimeLowQueuedLagStreakSec = 0;
    }

    if (m_runtimeHighQueuedLagStreakSec >= kBreachConsecutiveSec &&
        !m_runtimeQueuedLagSloBreached) {
      m_runtimeQueuedLagSloBreached = true;
      qCritical().noquote()
          << QStringLiteral(
                 "[Client][VideoPresent][RuntimeSLO] ★ 主线程呈现排队持续异常：maxQueuedLag≥%1ms "
                 "已连续 %2s "
                 "（四路聚合）★ 画面可能卡顿/条带；建议：关 H264/Video DEBUG 日志、降 "
                 "CLIENT_VIDEO_MAX_PRESENT_FPS、"
                 "确认硬件 GL（client_display_hw_presentation_ok=1）、对照 [QueuedLag]")
                 .arg(kHighLagMs)
                 .arg(kBreachConsecutiveSec);
      MetricsCollector::instance().increment(
          QStringLiteral("client_video_present_runtime_slo_breach_total"));
      MetricsCollector::instance().set(
          QStringLiteral("client_video_present_runtime_queued_lag_sustained"), 1.0);
    } else if (m_runtimeQueuedLagSloBreached &&
               m_runtimeLowQueuedLagStreakSec >= kRecoverConsecutiveSec) {
      m_runtimeQueuedLagSloBreached = false;
      qInfo().noquote()
          << QStringLiteral(
                 "[Client][VideoPresent][RuntimeSLO] maxQueuedLag 已回落至≤%1ms 连续 %2s，"
                 "清除 sustained 标记（可再次触发 breach 计数）")
                 .arg(kRecoverBelowMs)
                 .arg(kRecoverConsecutiveSec);
      MetricsCollector::instance().set(
          QStringLiteral("client_video_present_runtime_queued_lag_sustained"), 0.0);
    }
  }

  QString cat;
  QString detail;
  if (swOn) {
    // ★ SOFTWARE_GL 环境下不再仅输出 "先取消LIBGL_ALWAYS_SOFTWARE"，
    // 而是同时输出细分诊断，帮助在无法换硬件时精确定位瓶颈。
    const bool queuedSuspect = (maxQueuedLag > 50);
    const bool coalesceSuspect = (totalCoalescedDrops > 5);
    const bool rtpSuspect = (sumRtpRing >= 120);
    cat = QStringLiteral("SOFTWARE_GL");
    // 字段与上面布尔一致，避免固定脚注「queuedBlocked=1」与实际 0/1 矛盾（历史误导见终端对照）
    const int qb = queuedSuspect ? 1 : 0;
    const int cd = coalesceSuspect ? 1 : 0;
    const int rt = rtpSuspect ? 1 : 0;
    QString tail;
    if (qb != 0) {
      tail = QStringLiteral(
          " ★ queuedSuspect=1：maxQueuedLag>50ms，主线程事件队列/软 GL 呈现可能阻塞（对照 "
          "[QueuedLag]）");
    } else if (cd != 0) {
      tail = QStringLiteral(
          " ★ coalesceSuspect=1：合帧路径跳过中间帧（coalescedDrops 高）；可降 "
          "CLIENT_VIDEO_MAX_PRESENT_FPS 或关 H264 DEBUG 日志风暴");
    } else if (rt != 0) {
      tail = QStringLiteral(" ★ rtpSuspect=1：RTP 环计数高，查网络/ingress/jitter");
    } else {
      tail = QStringLiteral(" ★ 本秒无 queued/coalesce/rtp 强信号；仍卡请对照 totPend 与 libgl_sw");
    }
    detail = QStringLiteral(
                 "maxQueuedLag=%1ms coalescedDrops=%2 rtpRing=%3"
                 " | queuedSuspect=%4 coalesceSuspect=%5 rtpSuspect=%6%7")
                 .arg(maxQueuedLag)
                 .arg(totalCoalescedDrops)
                 .arg(sumRtpRing)
                 .arg(qb)
                 .arg(cd)
                 .arg(rt)
                 .arg(tail);
  } else {
    const bool mainSuspect = (sumHandler > 1 || maxMxP > 2 || slowSum > 0);
    const bool rtpSuspect = (sumRtpRing >= 120);
    const bool qmlSuspect = (qml1s > 0);
    if (qmlSuspect && mainSuspect && rtpSuspect) {
      cat = QStringLiteral("MIXED_ALL");
      detail = QStringLiteral("qml1s=%1 + main(sl=%2,mxP=%3,h=%4) + rtpRing=%5")
                   .arg(qml1s)
                   .arg(slowSum)
                   .arg(maxMxP)
                   .arg(sumHandler)
                   .arg(sumRtpRing);
    } else if (qmlSuspect && mainSuspect) {
      cat = QStringLiteral("MIXED_QML_MAIN");
      detail = QStringLiteral("qml1s=%1 main(sl=%2,mxP=%3,h=%4)")
                   .arg(qml1s)
                   .arg(slowSum)
                   .arg(maxMxP)
                   .arg(sumHandler);
    } else if (qmlSuspect && rtpSuspect) {
      cat = QStringLiteral("MIXED_QML_RTP");
      detail =
          QStringLiteral("qml1s=%1 rtpRing=%2 h=%3").arg(qml1s).arg(sumRtpRing).arg(sumHandler);
    } else if (mainSuspect && rtpSuspect) {
      cat = QStringLiteral("MIXED_MAIN_RTP");
      detail = QStringLiteral("sl=%1 mxP=%2 h=%3 rtpRing=%4")
                   .arg(slowSum)
                   .arg(maxMxP)
                   .arg(sumHandler)
                   .arg(sumRtpRing);
    } else if (qmlSuspect) {
      cat = QStringLiteral("QML_LAYER");
      detail = QStringLiteral("qml1s=%1 everHad被清空→占位/Canvas盖VideoOutput").arg(qml1s);
    } else if (mainSuspect) {
      cat = QStringLiteral("MAIN_THREAD");
      detail = QStringLiteral("sl=%1 mxP=%2 maxUsec=%3 h=%4 ★setVideoFrame路径/Queued堆积")
                   .arg(slowSum)
                   .arg(maxMxP)
                   .arg(maxUsec)
                   .arg(sumHandler);
    } else if (rtpSuspect) {
      cat = QStringLiteral("RTP_RING");
      detail = QStringLiteral("rtpRing=%1 ★入环/网络/调度；对照各路rg").arg(sumRtpRing);
    } else {
      cat = QStringLiteral("OK");
      detail = QStringLiteral(
          "本秒无三类强信号；闪屏若仍存查VSync/全屏合成或提高CLIENT_VIDEO_SLOW_PRESENT_US灵敏度");
    }
  }
  qInfo().noquote() << QStringLiteral(
                           "[Client][VideoFlickerClass][1Hz] cat=%1 qml1s=%2 rtpRing=%3 handler=%4 "
                           "slowSum=%5 maxMxP=%6 maxUsec=%7 | %8")
                           .arg(cat)
                           .arg(qml1s)
                           .arg(sumRtpRing)
                           .arg(sumHandler)
                           .arg(slowSum)
                           .arg(maxMxP)
                           .arg(maxUsec)
                           .arg(detail);

  ClientSystemStallDiag::MainThreadWatchdogSecondStats stallSec{};
  const bool stallW = ClientSystemStallDiag::isMainThreadWatchdogEnabled();
  if (stallW)
    stallSec = ClientSystemStallDiag::drainMainThreadWatchdogSecondStats();
  if (stallW) {
    const QString stallLine =
        QStringLiteral(
            "[Client][SysStall][1Hz] mainThreadMaxTickGapMs=%1 stallEvents=%2 "
            "★ maxTickGap≫CLIENT_MAIN_THREAD_STALL_INTERVAL_MS → "
            "主线程未及时跑事件循环（系统级阻塞/长槽）；"
            "stallEvents=秒内 tickGap>interval+CLIENT_MAIN_THREAD_STALL_WARN_EXTRA_MS 次数")
            .arg(stallSec.maxTickGapMs)
            .arg(stallSec.stallEvents);
    if (stallSec.stallEvents > 0 || stallSec.maxTickGapMs > 80)
      qWarning().noquote() << stallLine;
    else
      qInfo().noquote() << stallLine;
  }

  const bool perfJson = envTruthyBytes(qgetenv("CLIENT_VIDEO_PERF_JSON_1HZ"));
  const bool perfMetrics = envTruthyBytes(qgetenv("CLIENT_VIDEO_PERF_METRICS_1HZ"));
  const bool perfTrace = envTruthyBytes(qgetenv("CLIENT_VIDEO_PERF_TRACE_SPAN"));
  [[maybe_unused]] const bool perfRusage = envTruthyBytes(qgetenv("CLIENT_VIDEO_PERF_RUSAGE_1HZ"));

  if (perfMetrics) {
    MetricsCollector &mc = MetricsCollector::instance();
    mc.set(QStringLiteral("client_video_present_libgl_software"), swOn ? 1.0 : 0.0);
    mc.set(QStringLiteral("client_video_present_total_pending"), static_cast<double>(totPend));
    mc.set(QStringLiteral("client_video_present_handler_depth_sum"),
           static_cast<double>(sumHandler));
    mc.set(QStringLiteral("client_video_present_rtp_ring_sum"), static_cast<double>(sumRtpRing));
    mc.set(QStringLiteral("client_video_present_max_queued_lag_ms"),
           static_cast<double>(maxQueuedLag));
    mc.set(QStringLiteral("client_video_present_avg_queued_lag_ms"),
           static_cast<double>(avgQueuedLag));
    mc.increment(QStringLiteral("client_video_present_coalesced_drops_total"),
                 static_cast<double>(totalCoalescedDrops));
    mc.set(QStringLiteral("client_video_present_slow_present_sum"), static_cast<double>(slowSum));
    mc.set(QStringLiteral("client_video_present_max_pending_peak"), static_cast<double>(maxMxP));
    mc.set(QStringLiteral("client_video_present_max_handler_us"), static_cast<double>(maxUsec));
    mc.set(QStringLiteral("client_video_present_qml_layer_events_1s"), static_cast<double>(qml1s));
    mc.set(QStringLiteral("client_video_present_frames_front"),
           static_cast<double>(sf.framesToSink));
    mc.set(QStringLiteral("client_video_present_frames_rear"),
           static_cast<double>(sr.framesToSink));
    mc.set(QStringLiteral("client_video_present_frames_left"),
           static_cast<double>(sl.framesToSink));
    mc.set(QStringLiteral("client_video_present_frames_right"),
           static_cast<double>(sri.framesToSink));
    mc.observe(QStringLiteral("client_video_present_queued_lag_ms_sample"),
               static_cast<double>(maxQueuedLag));
    if (stallW) {
      mc.set(QStringLiteral("client_main_thread_watchdog_max_tick_gap_ms"),
             static_cast<double>(stallSec.maxTickGapMs));
      mc.increment(QStringLiteral("client_main_thread_stall_events_total"),
                   static_cast<double>(stallSec.stallEvents));
    }
  }

  Tracing::Span perfSpan;
  if (perfTrace) {
    perfSpan = Tracing::instance().beginSpan(QStringLiteral("client_video"),
                                             QStringLiteral("present_1hz_summary"));
    if (!perfSpan.traceId.isEmpty()) {
      Tracing::instance().addSpanTag(perfSpan, QStringLiteral("flicker_cat"), cat);
      Tracing::instance().addSpanTag(perfSpan, QStringLiteral("libgl_sw"), swOn ? 1 : 0);
      Tracing::instance().addSpanTag(perfSpan, QStringLiteral("max_queued_lag_ms"),
                                     static_cast<qint64>(maxQueuedLag));
      Tracing::instance().addSpanTag(perfSpan, QStringLiteral("coalesced_drops"),
                                     totalCoalescedDrops);
      Tracing::instance().addSpanTag(perfSpan, QStringLiteral("rtp_ring_sum"), sumRtpRing);
      Tracing::instance().addSpanTag(perfSpan, QStringLiteral("handler_sum"), sumHandler);
      Tracing::instance().addSpanTag(perfSpan, QStringLiteral("qml_layer_1s"), qml1s);
      if (stallW) {
        Tracing::instance().addSpanTag(perfSpan, QStringLiteral("main_tick_gap_ms"),
                                       stallSec.maxTickGapMs);
        Tracing::instance().addSpanTag(perfSpan, QStringLiteral("main_stall_events"),
                                       stallSec.stallEvents);
      }
    }
  }

  if (perfJson) {
    QJsonObject o;
    o.insert(QStringLiteral("schema"), QStringLiteral("client.video_present_1hz.v1"));
    o.insert(QStringLiteral("ts_ms"), QDateTime::currentMSecsSinceEpoch());
    o.insert(QStringLiteral("trace_id"), Tracing::instance().currentTraceId());
    o.insert(QStringLiteral("libgl_sw"), swOn ? 1 : 0);
    o.insert(QStringLiteral("tot_pend"), totPend);
    o.insert(QStringLiteral("handler_sum"), sumHandler);
    o.insert(QStringLiteral("rtp_ring_sum"), sumRtpRing);
    o.insert(QStringLiteral("max_queued_lag_ms"), static_cast<qint64>(maxQueuedLag));
    o.insert(QStringLiteral("avg_queued_lag_ms"), static_cast<qint64>(avgQueuedLag));
    o.insert(QStringLiteral("coalesced_drops"), totalCoalescedDrops);
    o.insert(QStringLiteral("flicker_cat"), cat);
    o.insert(QStringLiteral("qml_layer_events_1s"), qml1s);
    o.insert(QStringLiteral("slow_present_sum"), slowSum);
    o.insert(QStringLiteral("max_pending_peak"), maxMxP);
    o.insert(QStringLiteral("max_handler_us"), maxUsec);
    o.insert(QStringLiteral("frames_front"), sf.framesToSink);
    o.insert(QStringLiteral("frames_rear"), sr.framesToSink);
    o.insert(QStringLiteral("frames_left"), sl.framesToSink);
    o.insert(QStringLiteral("frames_right"), sri.framesToSink);
    if (stallW) {
      QJsonObject st;
      st.insert(QStringLiteral("max_tick_gap_ms"), stallSec.maxTickGapMs);
      st.insert(QStringLiteral("stall_events"), stallSec.stallEvents);
      o.insert(QStringLiteral("main_thread_watchdog"), st);
    }
#if defined(Q_OS_UNIX)
    if (perfRusage) {
      struct rusage ru;
      if (::getrusage(RUSAGE_SELF, &ru) == 0) {
        static qint64 s_prevCpuUs = -1;
        const qint64 cpuUs =
            static_cast<qint64>(ru.ru_utime.tv_sec) * 1000000LL + ru.ru_utime.tv_usec +
            static_cast<qint64>(ru.ru_stime.tv_sec) * 1000000LL + ru.ru_stime.tv_usec;
        if (s_prevCpuUs >= 0)
          o.insert(QStringLiteral("process_cpu_delta_us"), cpuUs - s_prevCpuUs);
        s_prevCpuUs = cpuUs;
      }
    }
#endif
    qInfo().noquote() << QStringLiteral("[Client][VideoPerf][JSON] ")
                      << QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
  }

  if (perfTrace)
    Tracing::instance().endSpan(perfSpan);
}

void WebRtcStreamManager::reportVideoFlickerQmlLayerEvent(const QString &where,
                                                          const QString &detail) {
  m_qmlLayerEventsThisSecond.fetch_add(1, std::memory_order_relaxed);
  qWarning().noquote() << QStringLiteral(
                              "[Client][VideoFlickerClass][QML_LAYER][instant] where=%1 detail=%2")
                              .arg(where, detail);
}

QString WebRtcStreamManager::getStreamDebugInfo() const {
  // ★★★ 诊断：供 QML console 调用，验证 webrtcStreamManager.frontClient 等属性是否可访问 ★★★
  // 若 QML 中出现 "webrtcStreamManager.getStreamDebugInfo is not a function" → QML 未识别
  // Q_INVOKABLE 若返回空字符串 → this 指针或成员访问异常
  QString info;
  QTextStream ts(&info);
  ts << "StreamManager Debug:\n";
  ts << "  front=" << (void *)m_front << " connected=" << (m_front ? m_front->isConnected() : -1)
     << "\n";
  ts << "  rear=" << (void *)m_rear << " connected=" << (m_rear ? m_rear->isConnected() : -1)
     << "\n";
  ts << "  left=" << (void *)m_left << " connected=" << (m_left ? m_left->isConnected() : -1)
     << "\n";
  ts << "  right=" << (void *)m_right << " connected=" << (m_right ? m_right->isConnected() : -1)
     << "\n";
  ts << "  vin=" << m_vin << " base=" << m_currentBase << "\n";
  {
    const QString zlmEnv =
        QProcessEnvironment::systemEnvironment().value(QStringLiteral("ZLM_VIDEO_URL"));
    ts << "  ZLM_VIDEO_URL set=" << (!zlmEnv.isEmpty() ? 1 : 0) << " len=" << zlmEnv.size() << "\n";
    ts << "  ZLM_VIDEO_URL probe=" << streamE2eFmtUrlProbe(zlmEnv) << "\n";
  }

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

int WebRtcStreamManager::getQmlSignalReceiverCount() const {
  // ★★★ 关键诊断：直接检查 QML 是否连接到 videoFrameReady 信号 ★★★
  // 返回值：
  //   -1 = m_front 为 null（异常）
  //   0 = QML 完全没有连接到信号（根因！）
  //   >0 = QML 有连接（理论上视频应该显示）
  // 注意：只在 front 上检查，因为四路都走同样逻辑

  if (!m_front) {
    qWarning() << "[StreamManager][Diag] getQmlSignalReceiverCount: m_front is NULL! streamManager="
               << (void *)this << " ★★★ FATAL: WebRtcClient 未初始化！★★★"
               << " 可能原因：对象被意外删除或构造失败";
    return -1;
  }

  int rc = m_front->receiverCountVideoFrameReady();
  qInfo() << "[StreamManager][Diag] getQmlSignalReceiverCount"
          << " m_front=" << (void *)m_front << " rc=" << rc
          << " ★ -1=前端m_front为null | 0=信号无接收者 | >0=有QML连接 ★";
  return rc;
}

int WebRtcStreamManager::getFrontSignalReceiverCount() const {
  // ★★★ 诊断：各路独立接收者计数 ★★★
  // 用于判断"哪一路"没有 QML 连接，精准定位问题
  if (!m_front)
    return -1;
  int rc = m_front->receiverCountVideoFrameReady();
  qInfo() << "[StreamManager][Diag] getFrontSignalReceiverCount()=" << rc
          << " ★ 0=该路 QML Connections 未连接该信号，>0=有连接"
          << " front=" << (void *)m_front
          << " stream=" << (m_front ? m_front->property("m_stream").toString() : "N/A");
  return rc;
}

int WebRtcStreamManager::getRearSignalReceiverCount() const {
  if (!m_rear)
    return -1;
  int rc = m_rear->receiverCountVideoFrameReady();
  qInfo() << "[StreamManager][Diag] getRearSignalReceiverCount()=" << rc
          << " ★ 0=该路 QML Connections 未连接，>0=有连接"
          << " rear=" << (void *)m_rear;
  return rc;
}

int WebRtcStreamManager::getLeftSignalReceiverCount() const {
  if (!m_left)
    return -1;
  int rc = m_left->receiverCountVideoFrameReady();
  qInfo() << "[StreamManager][Diag] getLeftSignalReceiverCount()=" << rc
          << " ★ 0=该路 QML Connections 未连接，>0=有连接"
          << " left=" << (void *)m_left;
  return rc;
}

int WebRtcStreamManager::getRightSignalReceiverCount() const {
  if (!m_right)
    return -1;
  int rc = m_right->receiverCountVideoFrameReady();
  qInfo() << "[StreamManager][Diag] getRightSignalReceiverCount()=" << rc
          << " ★ 0=该路 QML Connections 未连接，>0=有连接"
          << " right=" << (void *)m_right;
  return rc;
}

QString WebRtcStreamManager::getStreamSignalMetaInfo() const {
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
  dumpClient("rear", m_rear);
  dumpClient("left", m_left);
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

void WebRtcStreamManager::dumpStreamInfo() const {
  // 直接打印到日志，用于运行时诊断
  qInfo() << "[StreamManager][Diag] dumpStreamInfo:";
  qInfo() << "  front=" << (void *)m_front
          << " connected=" << (m_front ? m_front->isConnected() : -1);
  qInfo() << "  rear=" << (void *)m_rear << " connected=" << (m_rear ? m_rear->isConnected() : -1);
  qInfo() << "  left=" << (void *)m_left << " connected=" << (m_left ? m_left->isConnected() : -1);
  qInfo() << "  right=" << (void *)m_right
          << " connected=" << (m_right ? m_right->isConnected() : -1);

  if (m_front) {
    int rc = m_front->receiverCountVideoFrameReady();
    qInfo() << "  front videoFrameReady receivers=" << rc << " ★ 0=未连接QML，>0=有QML连接 ★";
    qInfo() << "  front streamUrl=" << m_front->streamUrl();
  }
}

int WebRtcStreamManager::getTotalPendingFrames() const {
  int t = 0;
  if (m_front) {
    t += m_front->pendingVideoHandlerDepth();
    t += m_front->rtpDecodeQueueDepth();
  }
  if (m_rear) {
    t += m_rear->pendingVideoHandlerDepth();
    t += m_rear->rtpDecodeQueueDepth();
  }
  if (m_left) {
    t += m_left->pendingVideoHandlerDepth();
    t += m_left->rtpDecodeQueueDepth();
  }
  if (m_right) {
    t += m_right->pendingVideoHandlerDepth();
    t += m_right->rtpDecodeQueueDepth();
  }
  return t;
}

void WebRtcStreamManager::forceRefreshAllRenderers(const QString &reason) {
  // ── Qt 6 兼容：渲染线程刷新 ──────────────────────────────────────
  // 根因：Qt Scene Graph 在 VehicleSelectionDialog modal=true 显示期间
  // 完全阻塞主事件循环，导致 window()->update() 投递的 QEvent::UpdateRequest
  // 堆积在主线程队列中无法处理，Scene Graph 停止调用 updatePaintNode。
  //
  // 显示已改为 QVideoSink + VideoOutput；forceRefresh 为兼容保留（空操作）
  // 向渲染线程事件队列投递刷新任务，绕过主线程阻塞。
  // 对话框打开期间每 16ms 持续调用（dialogOpenPollingTimer），持续驱动渲染线程。
  const int64_t now = QDateTime::currentMSecsSinceEpoch();
  static QAtomicInt s_callCount{0};
  const int callSeq = ++s_callCount;
  static int64_t s_rateWinStart = 0;
  static int s_callsIn5s = 0;
  if (s_rateWinStart == 0)
    s_rateWinStart = now;
  if (now - s_rateWinStart >= 5000) {
    if (s_callsIn5s > 15) {
      qWarning() << "[StreamManager][forceRefresh][Rate] 5s 内调用次数=" << s_callsIn5s
                 << " ★ 过高会加剧主线程负载；检查 dialogOpenPollingTimer / RenderMonitor";
    }
    s_callsIn5s = 0;
    s_rateWinStart = now;
  }
  ++s_callsIn5s;
  qInfo() << "[StreamManager] ★★★ forceRefreshAllRenderers 被调用 ★★★"
          << " callSeq=" << callSeq
          << " reason=" << (reason.isEmpty() ? QStringLiteral("(empty)") : reason) << " now=" << now
          << " totalPendingVideoHandlers=" << getTotalPendingFrames()
          << " front=" << (void *)m_front << " rear=" << (void *)m_rear
          << " left=" << (void *)m_left << " right=" << (void *)m_right
          << " callingThread=" << (void *)QThread::currentThreadId()
          << " ★ Qt 6 scheduleRenderJob 方案：向渲染线程投递刷新任务 ★";

  // 遍历四路 WebRtcClient，调用其 forceRefresh 方法
  auto refreshClient = [&](const char *name, WebRtcClient *client) {
    if (client) {
      {
        client->forceRefresh();
        qInfo() << "[StreamManager] forceRefresh:" << name << " 已调用";
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
  qInfo() << "[StreamManager] forceRefreshAllRenderers 完成，耗时=" << (doneTime - now) << "ms"
          << " callSeq=" << callSeq
          << " ★ 对比 updatePaintNode 日志确认渲染线程是否被 scheduleRenderJob 唤醒 ★";
}

bool WebRtcStreamManager::anyConnected() const {
  return (m_front && m_front->isConnected()) || (m_rear && m_rear->isConnected()) ||
         (m_left && m_left->isConnected()) || (m_right && m_right->isConnected());
}

QString WebRtcStreamManager::baseUrlFromWhep(const QString &whepUrl) const {
  return WebRtcUrlResolve::baseUrlFromWhep(whepUrl);
}

QString WebRtcStreamManager::appFromWhep(const QString &whepUrl) const {
  return WebRtcUrlResolve::appFromWhepQuery(whepUrl, m_app);
}

QString WebRtcStreamManager::resolveBaseUrl(const QString &whepUrl) const {
  const QString envZlm =
      QProcessEnvironment::systemEnvironment().value(QStringLiteral("ZLM_VIDEO_URL"));
  if (!whepUrl.isEmpty()) {
    const QString b = WebRtcUrlResolve::baseUrlFromWhep(whepUrl);
    qInfo().noquote() << QStringLiteral("[Client][StreamE2E][RESOLVE_BASE] source=whepArg")
                      << "whepProbe=" << streamE2eFmtUrlProbe(whepUrl)
                      << "baseOutEmpty=" << (b.isEmpty() ? 1 : 0)
                      << "envZLM_set=" << (!envZlm.isEmpty() ? 1 : 0)
                      << "envZLM_len=" << envZlm.size()
                      << "★ base 来自 WHEP 解析；若 baseOutEmpty=1 则 WHEP 格式/主机无效";
    if (!b.isEmpty()) {
      const QUrl bu(b);
      qInfo().noquote() << QStringLiteral("[Client][StreamE2E][RESOLVE_BASE] parsedBase host=")
                        << bu.host() << "port=" << bu.port() << "fullLen=" << b.size();
    }
    return b;
  }
  qInfo().noquote()
      << QStringLiteral("[Client][StreamE2E][RESOLVE_BASE] source=env_ZLM_VIDEO_URL")
      << "envEmpty=" << (envZlm.isEmpty() ? 1 : 0) << "envLen=" << envZlm.size()
      << "envProbe=" << streamE2eFmtUrlProbe(envZlm)
      << "★ whep 实参为空，仅用环境变量；若 envEmpty=1 则 connectFourStreams 将 abort";
  return WebRtcUrlResolve::resolveBaseUrl(whepUrl, envZlm);
}

void WebRtcStreamManager::syncStreamVinFromVehicleManager() {
  if (!m_vehicleManager) {
    qWarning().noquote() << QStringLiteral(
                                "[Client][StreamE2E][SYNC_VIN] vehicleManager=nullptr ★ 无法从 UI "
                                "同步 VIN，仅用 StreamManager.m_vin=")
                         << (m_vin.isEmpty() ? QStringLiteral("(empty)") : m_vin);
    return;
  }
  const QString vmVin = m_vehicleManager->currentVin();
  if (vmVin.isEmpty()) {
    qInfo().noquote() << QStringLiteral(
                             "[Client][StreamE2E][SYNC_VIN] VehicleManager.currentVin 为空 → "
                             "不覆盖 m_vin；当前 m_vin=")
                      << (m_vin.isEmpty() ? QStringLiteral("(empty)") : m_vin)
                      << "★ 若四路流名需 VIN 前缀，请先在选车页选中车辆";
    qInfo() << "[StreamManager][VIN] VehicleManager.currentVin 为空，拉流沿用 WebRtcStreamManager "
               "m_vin="
            << (m_vin.isEmpty() ? QStringLiteral("(empty)") : m_vin);
    return;
  }
  if (vmVin != m_vin) {
    qWarning() << "[StreamManager][VIN] ★ 拉流前与选车不一致 → 以 VehicleManager 为准 ★  was="
               << m_vin << " now=" << vmVin
               << " （例：仅 SessionManager 曾 "
                  "setCurrentVin，而用户在延迟窗口内换了车；或列表刷新后未重选）";
    qInfo().noquote() << QStringLiteral("[Client][StreamE2E][SYNC_VIN] override m_vin was=")
                      << m_vin << "now=" << vmVin;
    m_vin = vmVin;
  } else {
    qDebug().noquote() << QStringLiteral("[Client][StreamE2E][SYNC_VIN] aligned vmVin=mgrVin=")
                       << vmVin;
  }
}

void WebRtcStreamManager::connectFourStreams(const QString &whepUrl) {
  const int e2eSeq = ++g_streamE2eConnectSeq;
  const qint64 wallMs = QDateTime::currentMSecsSinceEpoch();
  {
    qInfo().noquote() << QStringLiteral("[Client][StreamE2E][CFS_ENTER] seq=") << e2eSeq
                      << "wallMs=" << wallMs << "whepEmpty=" << (whepUrl.isEmpty() ? 1 : 0)
                      << "whepLen=" << whepUrl.size()
                      << "whepProbe=" << streamE2eFmtUrlProbe(whepUrl) << "preVmVin="
                      << (m_vehicleManager ? m_vehicleManager->currentVin()
                                           : QStringLiteral("(no_vm)"))
                      << "preMgrVin=" << (m_vin.isEmpty() ? QStringLiteral("(empty)") : m_vin)
                      << "preBase="
                      << (m_currentBase.isEmpty() ? QStringLiteral("(empty)") : m_currentBase)
                      << "preAnyConn=" << anyConnected() << "★ grep 全链路: seq=" << e2eSeq;

    syncStreamVinFromVehicleManager();

    qInfo().noquote() << QStringLiteral("[Client][StreamE2E][CFS_POST_SYNC_VIN] seq=") << e2eSeq
                      << "mgrVin=" << (m_vin.isEmpty() ? QStringLiteral("(empty)") : m_vin)
                      << "vmVin="
                      << (m_vehicleManager ? m_vehicleManager->currentVin()
                                           : QStringLiteral("(no_vm)"));

    const QString resolvedBase = resolveBaseUrl(whepUrl);

    qInfo().noquote() << QStringLiteral("[Client][StreamE2E][CFS_RESOLVED] seq=") << e2eSeq
                      << "resolvedBaseEmpty=" << (resolvedBase.isEmpty() ? 1 : 0)
                      << "resolvedLen=" << resolvedBase.size()
                      << "resolvedProbe=" << streamE2eFmtUrlProbe(resolvedBase);

    qInfo() << "[Client][WebRTC][StreamManager] connectFourStreams 开始"
            << " seq=" << e2eSeq << " whepUrlEmpty=" << whepUrl.isEmpty()
            << " resolvedBase=" << resolvedBase << " currentBase=" << m_currentBase
            << " anyConnected=" << anyConnected()
            << " vin=" << (m_vin.isEmpty() ? "(empty)" : m_vin)
            << " ★ 对比 QML connectFourStreams 调用日志"
            << " ★ 对比 PeerConnection state logs"
            << " ★ 对比 DrivingInterface.qml streamClient 绑定日志";

    // ── 诊断：Q_PROPERTY getter vs 成员指针一致性验证 ─────────────────────────
    // 若两者不一致 → Q_PROPERTY 绑定异常或对象被替换（极端情况下 QML 可能读到空指针）
    qInfo() << "[StreamManager][Diag] Q_PROPERTY vs 成员指针一致性验证:"
            << " frontClient()=" << (void *)frontClient() << " m_front=" << (void *)m_front
            << " rearClient()=" << (void *)rearClient() << " m_rear=" << (void *)m_rear
            << " leftClient()=" << (void *)leftClient() << " m_left=" << (void *)m_left
            << " rightClient()=" << (void *)rightClient() << " m_right=" << (void *)m_right
            << " ★ 若指针不一致 → Q_PROPERTY 绑定异常，极端情况下 QML 可能读到空指针";

    // ── 诊断：下发前各路 videoFrameReady 接收者计数 ─────────────────────────
    // 对比下发后的 rc，可判断 QML 是否在 connectFourStreams 调用前就已经连接成功
    qInfo() << "[StreamManager][Diag] 下发前各路 videoFrameReady 接收者:"
            << " front-rc=" << (m_front ? m_front->receiverCountVideoFrameReady() : -1)
            << " rear-rc=" << (m_rear ? m_rear->receiverCountVideoFrameReady() : -1)
            << " left-rc=" << (m_left ? m_left->receiverCountVideoFrameReady() : -1)
            << " right-rc=" << (m_right ? m_right->receiverCountVideoFrameReady() : -1)
            << " ★ rc=0 意味着 QML Connections 未成功绑定或语法错误 ★"
            << " ★ rc>0 说明 QML 在 connectFourStreams 调用前已连接，后续若 rc "
               "变化则说明信号有重绑定 ★";

    // 若 base URL 不变且已有连接，跳过断连重连，避免约 10s 无视频窗口（VIN 变更则必须重建）
    if (!resolvedBase.isEmpty() && resolvedBase == m_currentBase && anyConnected() &&
        m_vin == m_streamsConnectedVin) {
      qWarning().noquote()
          << QStringLiteral("[Client][StreamE2E][CFS_EXIT] seq=") << e2eSeq
          << "branch=SKIP_SAME_BASE_ALREADY_CONNECTED"
          << "base=" << resolvedBase
          << "vin=" << (m_vin.isEmpty() ? QStringLiteral("(empty)") : m_vin)
          << "★ 未调用各 connectToStream；若误以为无视频，先断连再连或查是否仅 UI 未绑定";
      qInfo() << "[Client][WebRTC][StreamManager] base URL 不变且已有连接，跳过断连重连"
              << " streams 保持原状";
      return;
    }

    // base 变更或无连接：断开旧连接
    if (anyConnected()) {
      qInfo() << "[Client][WebRTC][StreamManager] base 变更或无连接，执行断连重建"
              << " oldBase=" << m_currentBase << " newBase=" << resolvedBase;
    }
    qInfo().noquote() << QStringLiteral("[Client][StreamE2E][CFS_ASSIGN_BASE] seq=") << e2eSeq
                      << "m_currentBase:=resolvedBase emptySoon="
                      << (resolvedBase.isEmpty() ? 1 : 0);

    m_currentBase = resolvedBase;

    // 统一在 try 块内执行断连，确保异常时仍能打日志
    {
      disconnectAll();
    }  

    QString base = resolvedBase;
    if (base.isEmpty()) {
      qCritical().noquote() << QStringLiteral("[Client][StreamE2E][CFS_EXIT] seq=") << e2eSeq
                            << "branch=ABORT_NO_BASE_AFTER_DISCONNECT"
                            << "whepWasEmpty=" << (whepUrl.isEmpty() ? 1 : 0)
                            << "ZLM_VIDEO_URL_set="
                            << (!QProcessEnvironment::systemEnvironment()
                                        .value(QStringLiteral("ZLM_VIDEO_URL"))
                                        .isEmpty()
                                    ? 1
                                    : 0)
                            << "★ 已 disconnectAll；m_currentBase 现为 Empty→ZlmPoll 将跳过";
      qCritical()
          << "[CLIENT][WebRTC] ZLM_VIDEO_URL 未设置且 whepUrl 为空，无法连接视频流。"
             "请设置环境变量 ZLM_VIDEO_URL=http://<zlm-host>:80 或在创建会话时传入 whepUrl。";
      return;
    }
    QString app = appFromWhep(whepUrl);

    // 多车隔离：流名加 VIN 前缀，格式 {vin}_cam_front；VIN 为空时退化为 cam_front
    QString vinPrefix = m_vin.isEmpty() ? QString() : (m_vin + QStringLiteral("_"));
    QString sFront = vinPrefix + QString::fromUtf8(kStreamFront);
    QString sRear = vinPrefix + QString::fromUtf8(kStreamRear);
    QString sLeft = vinPrefix + QString::fromUtf8(kStreamLeft);
    QString sRight = vinPrefix + QString::fromUtf8(kStreamRight);

    // ── 诊断：打印完整四路 URL，便于在 ZLM 日志中搜索确认每个流是否被拉 ─────────
    QString fUrl =
        QString("%1/index/api/webrtc?app=%2&stream=%3&type=play").arg(base).arg(app).arg(sFront);
    QString rUrl =
        QString("%1/index/api/webrtc?app=%2&stream=%3&type=play").arg(base).arg(app).arg(sRear);
    QString lUrl =
        QString("%1/index/api/webrtc?app=%2&stream=%3&type=play").arg(base).arg(app).arg(sLeft);
    QString rtUrl =
        QString("%1/index/api/webrtc?app=%2&stream=%3&type=play").arg(base).arg(app).arg(sRight);
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
            << " front=" << (void *)m_front << " rear=" << (void *)m_rear
            << " left=" << (void *)m_left << " right=" << (void *)m_right;

    if (m_front) {
      {
        m_front->connectToStream(base, app, sFront);
        // ── 诊断：下发后检查 videoFrameReady 信号接收者数 ───────────────
        int rc = m_front->receiverCountVideoFrameReady();
        qInfo() << "[StreamManager][Diag] front.connectToStream 下发后:"
                << " stream=" << sFront << " videoFrameReady receivers=" << rc
                << " ★ rc>0=QML有连接 | rc=0=信号静默丢弃，对比 QML console 日志确认";
      }  
    } else {
      qWarning() << "[StreamManager][WARN] m_front 为 nullptr，跳过";
    }

    if (m_rear) {
      {
        m_rear->connectToStream(base, app, sRear);
        int rc = m_rear->receiverCountVideoFrameReady();
        qInfo() << "[StreamManager][Diag] rear.connectToStream 下发后:"
                << " stream=" << sRear << " videoFrameReady receivers=" << rc;
      }  
    } else {
      qWarning() << "[StreamManager][WARN] m_rear 为 nullptr，跳过";
    }

    if (m_left) {
      {
        m_left->connectToStream(base, app, sLeft);
        int rc = m_left->receiverCountVideoFrameReady();
        qInfo() << "[StreamManager][Diag] left.connectToStream 下发后:"
                << " stream=" << sLeft << " videoFrameReady receivers=" << rc;
      }  
    } else {
      qWarning() << "[StreamManager][WARN] m_left 为 nullptr，跳过";
    }

    if (m_right) {
      {
        m_right->connectToStream(base, app, sRight);
        int rc = m_right->receiverCountVideoFrameReady();
        qInfo() << "[StreamManager][Diag] right.connectToStream 下发后:"
                << " stream=" << sRight << " videoFrameReady receivers=" << rc;
      }  
    } else {
      qWarning() << "[StreamManager][WARN] m_right 为 nullptr，跳过";
    }

    const int64_t doneTime = QDateTime::currentMSecsSinceEpoch();
    m_streamsConnectedVin = m_vin;
    qInfo().noquote() << QStringLiteral("[Client][StreamE2E][CFS_EXIT] seq=") << e2eSeq
                      << "branch=DISPATCHED_CONNECT_TOSTREAM"
                      << "baseLen=" << base.size() << "app=" << app << "streams=" << sFront << ","
                      << sRear << "," << sLeft << "," << sRight
                      << "durationMs=" << (doneTime - sendTime)
                      << "frontConn=" << (m_front && m_front->isConnected() ? 1 : 0)
                      << "rearConn=" << (m_rear && m_rear->isConnected() ? 1 : 0)
                      << "leftConn=" << (m_left && m_left->isConnected() ? 1 : 0)
                      << "rightConn=" << (m_right && m_right->isConnected() ? 1 : 0)
                      << "★ ICE 未就绪时 conn=0 属正常；持续 0 查 ZLM/网络/流名";
    qInfo() << "[Client][WebRTC][StreamManager] connectFourStreams 已下发四路"
            << " base=" << base << " app=" << app << " totalDuration=" << (doneTime - sendTime)
            << "ms";
  }  
}

void WebRtcStreamManager::onZlmSnapshotRequested(int disconnectWaveId, const QString &stream,
                                                 int peerStateEnum) {
  if (qEnvironmentVariableIntValue("CLIENT_DISABLE_ZLM_WAVE_SNAPSHOT") != 0) {
    qDebug()
        << "[StreamManager][ZlmWave] CLIENT_DISABLE_ZLM_WAVE_SNAPSHOT=1，跳过 getMediaList 快照";
    return;
  }
  if (disconnectWaveId < 0)
    return;
  if (m_currentBase.isEmpty()) {
    qWarning() << "[StreamManager][ZlmWave] disconnectWaveId=" << disconnectWaveId
               << " triggerStream=" << stream << " peerStateEnum=" << peerStateEnum
               << " skip: m_currentBase 为空（未拉流或已清空）";
    return;
  }

  static QMutex s_waveSnapMx;
  static QSet<int> s_wavesFetched;
  QMutexLocker lk(&s_waveSnapMx);
  if (s_wavesFetched.contains(disconnectWaveId))
    return;
  s_wavesFetched.insert(disconnectWaveId);
  if (s_wavesFetched.size() > 200)
    s_wavesFetched.clear();
  lk.unlock();

  qInfo() << "[StreamManager][ZlmWave] ★ 同 disconnectWaveId 窗内首次请求 ZLM getMediaList 快照 ★"
          << " disconnectWaveId=" << disconnectWaveId << " firstReporterStream=" << stream
          << " peerStateEnum=" << peerStateEnum << " base=" << m_currentBase
          << " vin=" << (m_vin.isEmpty() ? QStringLiteral("(empty)") : m_vin);
  fetchZlmMediaListSnapshotForWave(disconnectWaveId, stream, peerStateEnum);
}

void WebRtcStreamManager::fetchZlmMediaListSnapshotForWave(int disconnectWaveId,
                                                           const QString &triggerStream,
                                                           int peerStateEnum) {
  const QString apiPath = m_currentBase + QStringLiteral("/index/api/getMediaList");
  const QString secret = QProcessEnvironment::systemEnvironment().value(
      QStringLiteral("ZLM_API_SECRET"), QStringLiteral("j9uH7zT0mawXzTrvqRythA48QvZ8rO2Y"));
  const QString url = apiPath + QStringLiteral("?secret=") + secret;
  const int64_t t0 = QDateTime::currentMSecsSinceEpoch();

  static QNetworkAccessManager s_nam(nullptr);
  QNetworkReply *reply = s_nam.get(QNetworkRequest(QUrl(url)));

  QObject::connect(
      reply, &QNetworkReply::finished, this,
      [this, reply, t0, disconnectWaveId, triggerStream, peerStateEnum]() {
        Q_ASSERT_X(QThread::currentThread() == this->thread(), "fetchZlmMediaListSnapshotForWave",
                   "Callback in wrong thread");

        const int64_t t1 = QDateTime::currentMSecsSinceEpoch();
        const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto netErr = reply->error();
        const QString errMsg = reply->errorString();
        const QUrl reqUrl = reply->url();
        const QByteArray data =
            (netErr == QNetworkReply::NoError) ? reply->readAll() : QByteArray();
        reply->deleteLater();

        auto peerStateName = [](int e) -> const char * {
          switch (e) {
            case 3:
              return "Disconnected";
            case 4:
              return "Failed";
            case 5:
              return "Closed";
            default:
              return "other";
          }
        };

        if (netErr != QNetworkReply::NoError) {
          qWarning() << "[StreamManager][ZlmWave][ERROR] getMediaList 失败 disconnectWaveId="
                     << disconnectWaveId << " triggerStream=" << triggerStream
                     << " peerState=" << peerStateName(peerStateEnum) << "(" << peerStateEnum << ")"
                     << " err=" << errMsg << " http=" << httpStatus << " url=" << reqUrl.toString()
                     << " latencyMs=" << (t1 - t0)
                     << "【无法区分 ZLM 掐流 vs 网络：HTTP 层即失败】";
          return;
        }
        QJsonParseError parseErr;
        const QJsonDocument doc = QJsonDocument::fromJson(data, &parseErr);
        if (parseErr.error != QJsonParseError::NoError) {
          qWarning() << "[StreamManager][ZlmWave][ERROR] JSON 解析失败 disconnectWaveId="
                     << disconnectWaveId << parseErr.errorString()
                     << " bodyHead=" << QString::fromUtf8(data.left(240));
          return;
        }

        const QJsonArray mediaList = doc.object().value(QStringLiteral("data")).toArray();
        QStringList foundStreams;
        const QString vinPfx = m_vin.isEmpty() ? QString() : (m_vin + QStringLiteral("_"));
        const QString expect[] = {
            vinPfx + QStringLiteral("cam_front"), vinPfx + QStringLiteral("cam_rear"),
            vinPfx + QStringLiteral("cam_left"), vinPfx + QStringLiteral("cam_right")};

        qInfo() << "[StreamManager][ZlmWave] ★★★ getMediaList 快照 ★★★"
                << " disconnectWaveId=" << disconnectWaveId << " triggerStream=" << triggerStream
                << " peerState=" << peerStateName(peerStateEnum) << "(" << peerStateEnum << ")"
                << " http=" << httpStatus << " bytes=" << data.size() << " latencyMs=" << (t1 - t0)
                << " mediaRows=" << mediaList.size();

        int presentMask = 0;
        for (const QJsonValue &v : mediaList) {
          const QJsonObject o = v.toObject();
          const QString st = o.value(QStringLiteral("stream")).toString();
          if (!st.isEmpty())
            foundStreams << st;
          for (int i = 0; i < 4; ++i) {
            if (st == expect[i]) {
              presentMask |= (1 << i);
              const QString schema = o.value(QStringLiteral("schema")).toString();
              const qint64 alive = o.value(QStringLiteral("aliveSecond")).toVariant().toLongLong();
              const QString app = o.value(QStringLiteral("app")).toString();
              const QString vhost = o.value(QStringLiteral("vhost")).toString();
              qInfo() << "[StreamManager][ZlmWave] row stream=" << st << " app=" << app
                      << " vhost=" << vhost << " schema=" << schema << " aliveSecond=" << alive;
            }
          }
        }

        const int presentCount = (presentMask & 1) + ((presentMask >> 1) & 1) +
                                 ((presentMask >> 2) & 1) + ((presentMask >> 3) & 1);
        QString summary;
        for (int i = 0; i < 4; ++i)
          summary += (presentMask & (1 << i)) ? QStringLiteral("✓") : QStringLiteral("✗");
        summary += QStringLiteral(" ") + expect[0] + QStringLiteral("|") + expect[1] +
                   QStringLiteral("|") + expect[2] + QStringLiteral("|") + expect[3];

        QString bias;
        if (presentCount == 4) {
          bias = QStringLiteral(
              "【推断】四路均在 ZLM getMediaList → 推流仍在册，客户端 WebRTC 断开更偏向 "
              "UDP/ICE/路径/本机网络（"
              "「纯连接面」问题）；若 aliveSecond 停滞可再查 bridge→ZLM 是否卡死");
        } else if (presentCount == 0) {
          bias = QStringLiteral(
              "【推断】本车四路均不在 getMediaList → 更偏向车端停推、bridge 断连或 ZLM 已注销发布（"
              "「ZLM/上游掐流」侧）；请对照 vehicle/carla-bridge 与 ZLM 日志同一时间线");
        } else {
          bias = QStringLiteral(
                     "【推断】仅 %1/4 路在册 → 部分相机或会话异常，需对照 ZLM 与推流端逐路排查")
                     .arg(presentCount);
        }

        qWarning().noquote() << "[StreamManager][ZlmWave] disconnectWaveId=" << disconnectWaveId
                             << " presentCount=" << presentCount << "/" << 4
                             << " summary=" << summary << "\n[StreamManager][ZlmWave] " << bias
                             << "\n[StreamManager][ZlmWave] allStreamIds(sample)=" << foundStreams;

        if (doc.object().contains(QStringLiteral("code"))) {
          qInfo() << "[StreamManager][ZlmWave] api code="
                  << doc.object().value(QStringLiteral("code")).toInt();
        }
      });
}

void WebRtcStreamManager::cancelZlmReadySchedule() {
  if (m_zlmReadyTimer && m_zlmReadyTimer->isActive())
    m_zlmReadyTimer->stop();
  if (m_zlmReadyReply) {
    m_zlmReadyReply->disconnect();
    m_zlmReadyReply->abort();
    m_zlmReadyReply->deleteLater();
    m_zlmReadyReply = nullptr;
  }
  m_zlmReadyPollInFlight = false;
}

void WebRtcStreamManager::scheduleConnectFourStreamsWhenZlmReady(const QString &whepUrl,
                                                                 int pollIntervalMs,
                                                                 int maxWaitMs) {
  cancelZlmReadySchedule();
  int interval = pollIntervalMs < 200 ? 200 : pollIntervalMs;
  int maxWait = maxWaitMs < interval ? interval : maxWaitMs;

  syncStreamVinFromVehicleManager();
  m_zlmReadyWhep = whepUrl;

  // ★ 提前解析并同步状态，解锁 checkZlmStreamRegistration 诊断
  const QString resolvedBase = resolveBaseUrl(whepUrl);
  if (!resolvedBase.isEmpty()) {
      m_currentBase = resolvedBase;
      m_app = appFromWhep(whepUrl); // 同时从 WHEP 同步 App 空间，确保轮询路径正确
      qInfo().noquote() << "[StreamManager][ZlmReady][Prep] 已提前锁定 m_currentBase=" << m_currentBase 
                       << "m_app=" << m_app << "解锁稳态监控";
  }

  m_zlmReadyDeadlineMs = QDateTime::currentMSecsSinceEpoch() + maxWait;
  m_zlmReadyTimer->setInterval(interval);

  qInfo().noquote() << QStringLiteral("[StreamManager][ZlmReady] schedule poll intervalMs=") << interval
                    << "maxWaitMs=" << maxWait << "vin="
                    << (m_vin.isEmpty() ? QStringLiteral("(empty)") : m_vin)
                    << "whepProbe=" << streamE2eFmtUrlProbe(whepUrl);

  onZlmReadyTimerTick();
  m_zlmReadyTimer->start();
}

void WebRtcStreamManager::onZlmReadyTimerTick() {
  if (m_zlmReadyPollInFlight || m_zlmReadyReply)
    return;

  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  if (now >= m_zlmReadyDeadlineMs) {
    qWarning() << "[StreamManager][ZlmReady] deadline reached → connectFourStreams (WebRTC will "
                  "retry stream-not-found)";
    m_zlmReadyTimer->stop();
    connectFourStreams(m_zlmReadyWhep);
    return;
  }

  const QString base = resolveBaseUrl(m_zlmReadyWhep);
  if (base.isEmpty()) {
    qWarning() << "[StreamManager][ZlmReady] no ZLM base (whep empty & no ZLM_VIDEO_URL) → "
                  "connectFourStreams immediate";
    m_zlmReadyTimer->stop();
    connectFourStreams(m_zlmReadyWhep);
    return;
  }

  const QString secret = QProcessEnvironment::systemEnvironment().value(
      QStringLiteral("ZLM_API_SECRET"), QStringLiteral("j9uH7zT0mawXzTrvqRythA48QvZ8rO2Y"));
  const QString urlStr = base + QStringLiteral("/index/api/getMediaList?secret=") + secret +
                         QStringLiteral("&app=") + m_app;
  const QUrl url(urlStr);
  QNetworkRequest req(url);
  req.setTransferTimeout(8000);

  m_zlmReadyPollInFlight = true;
  m_zlmReadyReply = m_zlmReadyNam->get(req);
  QObject::connect(m_zlmReadyReply, &QNetworkReply::finished, this, [this, secret, url]() {
    QNetworkReply *reply = m_zlmReadyReply;
    m_zlmReadyReply = nullptr;
    m_zlmReadyPollInFlight = false;
    if (!reply)
      return;
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
      qDebug() << "[StreamManager][ZlmReady] getMediaList HTTP err=" << reply->errorString();
      return;
    }
    const QByteArray data = reply->readAll();
    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson(data, &pe);
    if (pe.error != QJsonParseError::NoError) {
      qDebug() << "[StreamManager][ZlmReady] JSON parse fail" << pe.errorString();
      return;
    }
    const QJsonArray rows = doc.object().value(QStringLiteral("data")).toArray();
    const int n = countVinCamStreamsInZlmMediaList(rows, m_vin);
    if (n < 4) {
      qWarning().noquote() << "[StreamManager][ZlmReady] ★ ZLM 资源未就绪 ★: vinStreams=" << n << "/4"
              << "vin=" << (m_vin.isEmpty() ? QStringLiteral("(empty)") : m_vin)
              << "\n  [Check] 正在轮询 URL: " << url.toString()
              << "\n  [Check] 期望 App: " << m_app 
              << "\n  [Check] 等待 carla-bridge 完成相机创建与推流（需 ZLM_HOST 配置正确）"
              << " | bodyLen=" << data.size() << " | rows=" << rows.size()
              << " | secret=" << secret.left(4) << "...";
      if (rows.size() > 0) {
          QStringList ids;
          for (const auto& r : rows) ids << r.toObject().value("stream").toString();
          qInfo().noquote() << "[StreamManager][ZlmReady] ZLM 当前已有流 ID (前10个):" << ids.mid(0, 10);
      }
    } else {
      qInfo() << "[StreamManager][ZlmReady] ✓ ZLM 资源已就绪: vinStreams=" << n << "/4"
              << "vin=" << m_vin << " → connectFourStreams";
    }

    if (n >= 4) {
      m_zlmReadyTimer->stop();
      connectFourStreams(m_zlmReadyWhep);
    }
  });
}

void WebRtcStreamManager::disconnectAll() {
  cancelZlmReadySchedule();
  m_streamsConnectedVin.clear();
  
  static bool s_inDisconnectAll = false;
  if (s_inDisconnectAll) {
    qDebug() << "[Client][StreamE2E][DISCONNECT_ALL] 递归或并发调用，跳过";
    return;
  }
  s_inDisconnectAll = true;

  const bool hadConn = anyConnected();
  qInfo().noquote() << QStringLiteral("[Client][StreamE2E][DISCONNECT_ALL] enter vin=")
                    << (m_vin.isEmpty() ? QStringLiteral("(empty)") : m_vin) << "m_currentBase="
                    << (m_currentBase.isEmpty() ? QStringLiteral("(empty)") : m_currentBase)
                    << "anyConnected=" << hadConn
                    << "f=" << (m_front && m_front->isConnected() ? 1 : 0)
                    << "r=" << (m_rear && m_rear->isConnected() ? 1 : 0)
                    << "l=" << (m_left && m_left->isConnected() ? 1 : 0)
                    << "rt=" << (m_right && m_right->isConnected() ? 1 : 0)
                    << "★ m_currentBase 在 disconnectAll 内不清空（仅断 PeerConnection）";
  qInfo() << "[Client][WebRTC][StreamManager] disconnectAll 四路";
  {
    if (m_front) {
      {
        m_front->disconnect();
      } 
    }
    if (m_rear) {
      {
        m_rear->disconnect();
      } 
    }
    if (m_left) {
      {
        m_left->disconnect();
      } 
    }
    if (m_right) {
      {
        m_right->disconnect();
      } 
    }
  }  
  qInfo().noquote() << QStringLiteral("[Client][StreamE2E][DISCONNECT_ALL] done vin=")
                    << (m_vin.isEmpty() ? QStringLiteral("(empty)") : m_vin) << "m_currentBase="
                    << (m_currentBase.isEmpty() ? QStringLiteral("(empty)") : m_currentBase)
                    << "anyConnectedNow=" << anyConnected();
  s_inDisconnectAll = false;
}

void WebRtcStreamManager::checkZlmStreamRegistration() {
  // ── ★★★ 端到端追踪：checkZlmStreamRegistration 进入 ★★★ ─────────────────
  const int64_t funcEnterTime = QDateTime::currentMSecsSinceEpoch();
  qInfo() << "[StreamManager][ZlmPoll] ★★★ checkZlmStreamRegistration ENTER ★★★"
          << " vin=" << (m_vin.isEmpty() ? "(empty)" : m_vin)
          << " base=" << (m_currentBase.isEmpty() ? "(empty)" : m_currentBase)
          << " enterTime=" << funcEnterTime;

  // 无 base URL 时跳过（未连接）
  if (m_currentBase.isEmpty()) {
    qInfo().noquote()
        << QStringLiteral("[Client][StreamE2E][ZLM_POLL_SKIP] interval=15s m_currentBase 为空 vin=")
        << (m_vin.isEmpty() ? QStringLiteral("(empty)") : m_vin)
        << "★ 常见：CFS_EXIT ABORT_NO_BASE 或从未成功 connectFourStreams；grep [Client][StreamE2E]";
    return;
  }

  const QString apiPath = m_currentBase + "/index/api/getMediaList";
  // ZLM API secret 从环境变量读取（与 docker-compose.yml 中 ZLM_API_SECRET 一致）
  // 注意：若 docker-compose.yml 挂载了 deploy/zlm/config.ini 且 secret 不同，ZLM 会生成随机
  // secret， 导致 getMediaList 等 API 调用失败；此时请确保 ZLM_API_SECRET 环境变量与 ZLM
  // 实际使用的一致。
  const QString secret = QProcessEnvironment::systemEnvironment().value(
      QStringLiteral("ZLM_API_SECRET"),
      QStringLiteral("j9uH7zT0mawXzTrvqRythA48QvZ8rO2Y"));  // ZLM volume 默认
  const QString url = apiPath + "?secret=" + secret;

  // 使用函数内 static QNAM，避免频繁 new/delete，且在主线程调用是安全的
  static QNetworkAccessManager nam(nullptr);
  QNetworkReply *reply = nam.get(QNetworkRequest(url));
  // ── 诊断：验证 QNAM 和 reply 的线程亲缘性 ─────────────────────────────────
  qInfo() << "[StreamManager][ZlmPoll] QNAM thread=" << nam.thread()
          << " reply thread=" << reply->thread() << " this thread=" << this->thread()
          << " currentThread=" << QThread::currentThread()
          << " ★ 若线程不一致 → QNetworkAccessManager 跨线程使用，Qt 可能自动转发但有性能开销 ★";

  QObject::connect(reply, &QNetworkReply::finished, this, [this, reply, funcEnterTime]() {
    // ── 诊断：验证回调执行线程（QNetworkReply 在主线程发射信号，lambda 在主线程执行）──
    // 若此断言失败，说明回调跑到了错误的线程，可能访问已销毁的对象
    Q_ASSERT_X(QThread::currentThread() == this->thread(), "checkZlmStreamRegistration callback",
               "Callback in wrong thread — potential race condition!");
    {
      reply->deleteLater();
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
    if (data.size() > 0 && data.size() <= 768) {
      qInfo() << "[StreamManager][ZlmPoll] getMediaList 完整 body（小响应）="
              << QString::fromUtf8(data) << " ★ 可核对 code/data 是否为空、secret 是否匹配";
    }

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
      {
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
      }  
    }

    // ── 诊断：解析完成，准备输出 ZLM 流状态汇总 ─────────────────────────────
    const int64_t parseDoneTime = QDateTime::currentMSecsSinceEpoch();
    const int64_t parseDuration = parseDoneTime - responseTime;
    qInfo() << "[StreamManager][ZlmPoll] 解析耗时=" << parseDuration << "ms"
            << " foundStreams=" << foundStreams.size() << " 四路流名:";
    for (const QString &s : foundStreams) {
      qInfo() << "[StreamManager][ZlmPoll]   ZLM流=" << s;
    }

    // 检查四路相机流是否在册
    QStringList camStreams;
    for (const QString &s : {(m_vin.isEmpty() ? QString() : m_vin + "_") + "cam_front",
                             (m_vin.isEmpty() ? QString() : m_vin + "_") + "cam_rear",
                             (m_vin.isEmpty() ? QString() : m_vin + "_") + "cam_left",
                             (m_vin.isEmpty() ? QString() : m_vin + "_") + "cam_right"}) {
      camStreams << (foundStreams.contains(s) ? "✓" + s : "✗" + s);
    }
    QString summary = camStreams.join(" ");
    // 仅在状态变化时打日志，避免刷屏
    if (summary != m_lastZlmStreamsSeen) {
      m_lastZlmStreamsSeen = summary;
      // 判断四路中至少有一路在册（允许部分相机断）
      bool anyPresent = false;
      for (const QString &expected : {(m_vin.isEmpty() ? QString() : m_vin + "_") + "cam_front",
                                      (m_vin.isEmpty() ? QString() : m_vin + "_") + "cam_rear",
                                      (m_vin.isEmpty() ? QString() : m_vin + "_") + "cam_left",
                                      (m_vin.isEmpty() ? QString() : m_vin + "_") + "cam_right"}) {
        if (foundStreams.contains(expected)) {
          anyPresent = true;
          break;
        }
      }
      const int64_t totalDuration = QDateTime::currentMSecsSinceEpoch() - funcEnterTime;
      if (anyPresent) {
        qInfo() << "[StreamManager][ZlmPoll] ★★★ ZLM 推流在册（四路检查完成）★★★"
                << " vin=" << (m_vin.isEmpty() ? "(empty)" : m_vin) << " summary=" << summary
                << " totalDuration=" << totalDuration << "ms";
      } else {
        qWarning() << "[StreamManager][ZlmPoll][FATAL] ★★★ ZLM 推流失册！carla-bridge 断连或未收到 "
                      "MQTT start_stream ★★★"
                   << " vin=" << (m_vin.isEmpty() ? "(empty)" : m_vin) << " summary=" << summary
                   << " found=" << foundStreams << " totalDuration=" << totalDuration << "ms";
      }
    }
  });
}
