#pragma once

#include <QStringList>

class QProcessEnvironment;

namespace ClientApp {

/**
 * 启动就绪档位（影响：配置 URL 校验范围 + 默认 TCP 探测目标，当 CLIENT_STARTUP_TCP_TARGETS 未设置时）。
 *
 * - Minimal / Dev：仅 Backend + MQTT（与历史默认一致）。
 * - Standard：同 Minimal（保留别名，便于文档与脚本）。
 * - Full / Production：Backend + MQTT + Keycloak + ZLM 配置必须可解析，且 TCP 默认探测四端点。
 *
 * CLIENT_STARTUP_READINESS_PROFILE 取值：minimal|dev|standard|full|production（大小写不敏感）。
 * 未设置时：检测到容器（/.dockerenv）→ Full；否则 → Standard（仅 backend+mqtt）。
 *
 * 跳过：CLIENT_SKIP_CONFIG_READINESS_GATE=1 或 CLIENT_SKIP_PLATFORM_GATE=1（与整段应急绕过对齐）。
 *
 * 失败：exit=95 (CONFIG_READINESS_GATE_FAILED)。
 */

enum class StartupReadinessProfile { Minimal, Standard, Full };

StartupReadinessProfile parseStartupReadinessProfile(const QProcessEnvironment &env);

QStringList defaultTcpTargetNamesForReadinessProfile(StartupReadinessProfile p);

/** 须在 runDisplayEnvironmentCheck 之后、runMandatoryTcpConnectivityGate 之前调用 */
int runMandatoryConfigurationReadinessGate();

}  // namespace ClientApp
