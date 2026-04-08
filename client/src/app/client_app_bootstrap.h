#pragma once

#include <QString>
#include <QUrl>

class QGuiApplication;
class QQmlApplicationEngine;
class QQmlContext;
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

QUrl resolveQmlMainUrl(QQmlApplicationEngine *engine);

void registerQmlTypes();

/** 中文字体与应用元数据（组织名、版本等） */
void setupApplicationChrome(QGuiApplication &app, QString &outChineseFont);

void registerContextProperties(
    QQmlContext *ctx, AuthManager *authManager, VehicleManager *vehicleManager,
    WebRtcClient *webrtcClient, WebRtcStreamManager *webrtcStreamManager,
    MqttController *mqttController, VehicleStatus *vehicleStatus,
    NodeHealthChecker *nodeHealthChecker, EventBus *eventBus,
    SystemStateMachine *systemStateMachine, SessionManager *teleopSession,
    VehicleControlService *vehicleControl, SafetyMonitorService *safetyMonitor,
    NetworkQualityAggregator *networkQuality, DegradationManager *degradationManager,
    Tracing *tracing, const QString &applicationChineseFont);

/** 启动后记录 GUI 平台与显示环境（用于定位 X11/xcb/Wayland 问题） */
void logGuiPlatformDiagnostics();

} // namespace ClientApp
