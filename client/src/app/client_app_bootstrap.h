#pragma once

#include "app/client_window_frame_policy.h"

#include <QList>
#include <QString>
#include <QUrl>

class QGuiApplication;
class QObject;
class QQmlApplicationEngine;
class QQmlContext;
class QQmlEngine;
class AuthManager;
class VehicleManager;
class WebRtcClient;
class WebRtcStreamManager;
class MqttController;
class VehicleStatus;
class NodeHealthChecker;
class EventBus;
class SystemStateMachine;
class SessionManager;
class VehicleControlService;
class SafetyMonitorService;
class NetworkQualityAggregator;
class DegradationManager;
class Tracing;

namespace ClientApp {

/** OpenGL 默认帧缓冲探测结果（用于日志、Prometheus 门禁与硬件呈现策略）。 */
struct OpenGlFramebufferProbeResult {
  bool skipped = false;               /**< CLIENT_SKIP_OPENGL_PROBE=1 */
  bool success = false;               /**< 上下文创建且 makeCurrent 成功 */
  bool rendererLooksSoftware = false; /**< llvmpipe / lavapipe / swrast 等 */
  QString vendor;
  QString renderer;
  QString version;
  QString glslVersion;
};

/**
 * 须在创建 QGuiApplication 之前调用：设置 QSurfaceFormat::defaultFormat()（默认可交换间隔 1）。
 * 与 Qt 文档中 swap interval / 呈现节流一致；驱动仍可覆盖。
 */
void applyPresentationSurfaceFormatDefaults();

/** 创建离屏 GL 上下文并探测 GL 字符串（不打印日志）。 */
OpenGlFramebufferProbeResult probeOpenGlDefaultFramebuffer();

void logOpenGlProbeResult(const OpenGlFramebufferProbeResult &r);

/** 写入 client_display_* Gauge（须已构造 MetricsCollector::instance()）。 */
void recordDisplayProbeMetrics(const OpenGlFramebufferProbeResult &r);

/**
 * 硬件呈现门禁：Linux 下交互式 xcb 会话默认启用；或 CLIENT_REQUIRE_HARDWARE_PRESENTATION=1、
 * CLIENT_TELOP_STATION=1。若 LIBGL_ALWAYS_SOFTWARE、或 GL_RENDERER 判定为软件光栅、或探测失败，
 * 则返回非 0。CLIENT_ALLOW_SOFTWARE_PRESENTATION=1 或 CLIENT_GPU_PRESENTATION_OPTIONAL=1 时恒为 0。
 * 无交互显示会话时恒为 0。
 */
int enforceHardwarePresentationGate(const OpenGlFramebufferProbeResult &r);

/** 探测 + 日志 + recordDisplayProbeMetrics + enforceHardwarePresentationGate（main 推荐入口）。 */
int runDisplayEnvironmentCheck();

/**
 * 最近一次 runDisplayEnvironmentCheck 中，是否判定为「非 LIBGL_ALWAYS_SOFTWARE 且 GL_RENDERER
 * 非软件光栅」。 探测跳过或失败时为 false。供 QML rd_hardwarePresentationOk 与运维对照。
 */
bool lastHardwarePresentationEnvironmentOk();

QUrl resolveQmlMainUrl(QQmlApplicationEngine *engine);

void registerQmlTypes();

/** 中文字体与应用元数据（组织名、版本等） */
void setupApplicationChrome(QGuiApplication &app, QString &outChineseFont);

void registerContextProperties(QQmlContext *ctx, AuthManager *authManager,
                               VehicleManager *vehicleManager, WebRtcClient *webrtcClient,
                               WebRtcStreamManager *webrtcStreamManager,
                               MqttController *mqttController, VehicleStatus *vehicleStatus,
                               NodeHealthChecker *nodeHealthChecker, EventBus *eventBus,
                               SystemStateMachine *systemStateMachine,
                               SessionManager *teleopSession, VehicleControlService *vehicleControl,
                               SafetyMonitorService *safetyMonitor,
                               NetworkQualityAggregator *networkQuality,
                               DegradationManager *degradationManager, Tracing *tracing,
                               const QString &applicationChineseFont,
                               QObject *videoIntegrityBannerBridge = nullptr);

/** 启动后记录 GUI 平台与显示环境（用于定位 X11/xcb/Wayland 问题） */
void logGuiPlatformDiagnostics();

/** 记录 QML import 路径（main.qml 解析后 engine 上应有 qml 根目录）。 */
void logQmlEngineImportPaths(const QQmlEngine *engine);

/** 记录根上下文 rd_* 注入快照（AppContext.qml 数据源；与 ReferenceError 对照）。 */
void logQmlRootContextRdSnapshot(const QQmlContext *root);

/**
 * 挂接 QQmlEngine::warnings：分字段日志、ReferenceError/AppContext 专项提示、累计计数。
 * lifecycleOwner 建议为 QGuiApplication，保证进程存活期内连接有效。
 */
void installQmlEngineDiagnosticHooks(QQmlEngine *engine, QObject *lifecycleOwner);

/** main.qml load 成功后：根对象数量与类型名（窗口未显示前的结构诊断）。 */
void logQmlPostLoadSummary(const QQmlApplicationEngine *engine, const QUrl &mainUrl);

/** registerContextProperties 之后有效；供平台门禁读取窗口策略 */
bool lastWindowFramePolicyUseWindowFrame();
QString lastWindowFramePolicyReasonString();

/**
 * xcb 且已加载 QML 窗口后：输出「客户区透出宿桌面」类现象的 5Why 对照与处置提示（非判定失败）。
 * 供运维 grep [Client][CompositingRoot]；与 PresentHealth delta=0 解耦（静态 UI 无动画时 delta 可为 0）。
 */
void logX11ClientAreaTransparencyFiveWhyHint();

/**
 * 等价于 runDisplayEnvironmentCheck()（保留旧符号供调用方兼容）。
 * 失败时仍打 [Client][GLProbe]；可用 CLIENT_SKIP_OPENGL_PROBE=1 跳过探测。
 */
void logOpenGlDefaultFramebufferProbe();

}  // namespace ClientApp
