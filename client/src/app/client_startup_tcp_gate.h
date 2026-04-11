#pragma once

namespace ClientApp {

/**
 * 启动必过：对配置中的服务端点做 TCP 连通性探测（仅建立 TCP，不做 TLS 应用层握手）。
 *
 * 默认启用。关闭方式（择一）：
 * - CLIENT_STARTUP_TCP_GATE=0 / false / off
 * - CLIENT_SKIP_TCP_STARTUP_GATE=1
 * - CLIENT_SKIP_PLATFORM_GATE=1（与整段启动门禁一并跳过，应急）
 *
 * 目标列表（CLIENT_STARTUP_TCP_TARGETS 未设置时，由 CLIENT_STARTUP_READINESS_PROFILE 决定，见
 * client_startup_readiness_gate.h；容器 /.dockerenv 存在且未设 PROFILE 时默认 full→四端点）：
 * - CLIENT_STARTUP_TCP_TARGETS=逗号分隔：backend, mqtt, keycloak, zlm
 * - 全量四端点：CLIENT_STARTUP_TCP_TARGETS=backend,mqtt,keycloak,zlm
 * - CLIENT_STARTUP_TCP_TARGETS=none 表示不检查预置目标（仍可用 EXTRA_URLS）
 *
 * 额外 URL（完整 URL，逗号分隔）：
 * - CLIENT_STARTUP_TCP_EXTRA_URLS=http://10.0.0.1:9000,tcp://host:1883
 *
 * 弱网 / 启动耗时（单次连接超时 + 重试，各端点并行探测）：
 * - CLIENT_STARTUP_TCP_TIMEOUT_MS  单次 try 超时（默认 1200，最小 200）
 * - CLIENT_STARTUP_TCP_ATTEMPTS    每端点尝试次数（默认 3，最小 1）
 * - CLIENT_STARTUP_TCP_RETRY_GAP_MS 失败后间隔再试（默认 200ms）
 *
 * 失败：exit=96 (TCP_STARTUP_GATE_FAILED)，stderr + qCritical 列出每个失败端点及原因。
 */

int runMandatoryTcpConnectivityGate();

}  // namespace ClientApp
