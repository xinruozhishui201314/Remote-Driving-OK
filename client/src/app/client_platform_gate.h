#pragma once

#include <QList>
#include <QString>

class QObject;
class QGuiApplication;

namespace ClientApp {

/**
 * 启动必过门禁：任一项失败即 process 退出，stderr + qCritical 给出「原因 + 建议 + exit 码」。
 *
 * 环境变量（应急绕过，不推荐生产）：
 * - CLIENT_SKIP_PLATFORM_GATE=1：跳过本文件内 PRE/POST/QML 窗口相关检查
 * - CLIENT_ALLOW_MISSING_DISPLAY=1：Linux xcb 路径下允许 DISPLAY 为空
 * - CLIENT_ALLOW_MISSING_WAYLAND_DISPLAY=1：纯 Wayland 路径下允许 WAYLAND_DISPLAY 为空
 * - CLIENT_STARTUP_TCP_GATE=0 / CLIENT_SKIP_TCP_STARTUP_GATE=1：跳过 TCP 端点必过（见
 * client_startup_tcp_gate.h）
 * - CLIENT_SKIP_CONFIG_READINESS_GATE=1：跳过配置 URL 必过（见 client_startup_readiness_gate.h）
 *
 * 退出码一览（与 main / DisplayGate 对齐，便于脚本判断）：
 * - 75–78  [Client][DisplayGate] 硬件呈现 / 探测配置冲突（见 client_app_bootstrap）
 * - 81     PRE：将使用 xcb 但 DISPLAY 未设置
 * - 82     PRE：将使用纯 Wayland 但 WAYLAND_DISPLAY 未设置
 * - 83     POST：platformName 为空
 * - 84     POST：交互式 GUI 无可用屏幕或 primaryScreen 为空
 * - 86–87  POST-QML：窗框策略与 Frameless 不一致 / 无 QQuickWindow
 * - 88     GL：交互模式下 OpenGL 基本探测失败（见 runDisplayEnvironmentCheck）
 * - 95     配置就绪：服务 URL 无效（见 client_startup_readiness_gate）
 * - 96     TCP：必测端点不可达（见 client_startup_tcp_gate；默认目标随 readiness 档位）
 * - 91     QML：找不到 main.qml
 * - 93     QML：engine.load 后根对象为空
 * - 94     QML：根对象创建失败（objectCreated 回调）
 */

/** 启动时打印一次「必过项清单」，便于对照日志 */
void logStartupMandatoryGatesBrief();

/** 全部必过项通过后调用，便于确认进入事件循环前状态 */
void logStartupAllMandatoryGatesOk();

int runPreQGuiApplicationPlatformGate();

int runPostQGuiApplicationPlatformGate(QGuiApplication &app);

/** 须在 registerContextProperties 与 engine.load 之后调用 */
int enforceQmlLoadedPlatformGate(const QList<QObject *> &rootObjects);

}  // namespace ClientApp
