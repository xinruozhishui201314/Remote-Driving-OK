#pragma once

#include <QString>

/**
 * WHEP / ZLM base URL 解析（纯函数，无环境变量与日志副作用）。
 * 生产路径由 WebRtcStreamManager 调用；单测仅链接本单元，避免整仓 WebRTC 链接。
 */
namespace WebRtcUrlResolve {

QString baseUrlFromWhep(const QString &whepUrl);

/// whep 非空则解析 WHEP 得 base；否则返回 envZlmVideoUrl（由调用方从环境读取）。
QString resolveBaseUrl(const QString &whepUrl, const QString &envZlmVideoUrl);

/// 从 WHEP query 取 app=，缺省用 defaultApp。
QString appFromWhepQuery(const QString &whepUrl, const QString &defaultApp);

}  // namespace WebRtcUrlResolve
