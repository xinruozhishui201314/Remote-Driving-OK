#include "client_startup_readiness_gate.h"

#include "core/configuration.h"

#include <QFile>
#include <QProcessEnvironment>
#include <QUrl>

#include <cstdio>

namespace ClientApp {
namespace {

bool readinessGateSkipped(const QProcessEnvironment &env) {
  if (qEnvironmentVariableIntValue("CLIENT_SKIP_PLATFORM_GATE") == 1) {
    return true;
  }
  if (qEnvironmentVariableIntValue("CLIENT_SKIP_CONFIG_READINESS_GATE") == 1) {
    return true;
  }
  return false;
}

QString resolveBackendUrl(const Configuration &cfg) {
  const QProcessEnvironment &e = QProcessEnvironment::systemEnvironment();
  QString u = e.value(QStringLiteral("DEFAULT_SERVER_URL")).trimmed();
  if (!u.isEmpty()) {
    return u;
  }
  u = e.value(QStringLiteral("REMOTE_DRIVING_SERVER")).trimmed();
  if (!u.isEmpty()) {
    return u;
  }
  u = e.value(QStringLiteral("BACKEND_URL")).trimmed();
  if (!u.isEmpty()) {
    return u;
  }
  return cfg.serverUrl();
}

QString resolveMqttUrl(const Configuration &cfg) {
  const QString u =
      QProcessEnvironment::systemEnvironment().value(QStringLiteral("MQTT_BROKER_URL")).trimmed();
  if (!u.isEmpty()) {
    return u;
  }
  return cfg.mqttBrokerUrl();
}

QString resolveKeycloakUrl(const Configuration &cfg) {
  const QString u =
      QProcessEnvironment::systemEnvironment().value(QStringLiteral("KEYCLOAK_URL")).trimmed();
  if (!u.isEmpty()) {
    return u;
  }
  return cfg.keycloakUrl();
}

QString resolveZlmUrl(const Configuration &cfg) {
  const QString u =
      QProcessEnvironment::systemEnvironment().value(QStringLiteral("ZLM_VIDEO_URL")).trimmed();
  if (!u.isEmpty()) {
    return u;
  }
  return cfg.zlmUrl();
}

bool validateNetworkServiceUrl(const QString &urlStr, QString *errOut) {
  if (urlStr.trimmed().isEmpty()) {
    *errOut = QStringLiteral("URL 为空");
    return false;
  }
  const QUrl u = QUrl::fromUserInput(urlStr.trimmed());
  if (!u.isValid() || u.host().isEmpty()) {
    *errOut = QStringLiteral("无法解析主机名或 URL 无效");
    return false;
  }
  const QString scheme = u.scheme().toLower();
  if (scheme == QLatin1String("file") || scheme.isEmpty()) {
    *errOut = QStringLiteral("scheme 无效");
    return false;
  }
  return true;
}

void printReadinessFailureHelp() {
  std::fprintf(
      stderr,
      "\n"
      "══════════════════════════════════════════════════════════════════════\n"
      "[Client][StartupGate] FATAL exit=95 (CONFIG_READINESS_GATE_FAILED)\n"
      "原因: 启动前配置 URL 校验未通过（Backend/MQTT/Keycloak/ZLM 之一无效或未设置）。\n"
      "建议:\n"
      "  1) 容器全栈: 设置 BACKEND_URL / MQTT_BROKER_URL / KEYCLOAK_URL / ZLM_VIDEO_URL\n"
      "  2) 仅调试控制面: CLIENT_STARTUP_READINESS_PROFILE=minimal\n"
      "  3) CI/无依赖: CLIENT_SKIP_CONFIG_READINESS_GATE=1（与 CLIENT_SKIP_PLATFORM_GATE 二选一应急）\n"
      "  4) 默认 TCP 探测目标随档位变化；显式覆盖仍可用 CLIENT_STARTUP_TCP_TARGETS=...\n"
      "══════════════════════════════════════════════════════════════════════\n\n");
}

}  // namespace

StartupReadinessProfile parseStartupReadinessProfile(const QProcessEnvironment &env) {
  const QString raw = env.value(QStringLiteral("CLIENT_STARTUP_READINESS_PROFILE")).trimmed().toLower();
  if (raw == QLatin1String("minimal") || raw == QLatin1String("dev")) {
    return StartupReadinessProfile::Minimal;
  }
  if (raw == QLatin1String("full") || raw == QLatin1String("production")) {
    return StartupReadinessProfile::Full;
  }
  if (raw == QLatin1String("standard")) {
    return StartupReadinessProfile::Standard;
  }
  if (!raw.isEmpty()) {
    return StartupReadinessProfile::Standard;
  }
  if (QFile::exists(QStringLiteral("/.dockerenv"))) {
    return StartupReadinessProfile::Full;
  }
  return StartupReadinessProfile::Standard;
}

QStringList defaultTcpTargetNamesForReadinessProfile(StartupReadinessProfile p) {
  if (p == StartupReadinessProfile::Full) {
    return QStringList{QStringLiteral("backend"), QStringLiteral("mqtt"), QStringLiteral("keycloak"),
                       QStringLiteral("zlm")};
  }
  return QStringList{QStringLiteral("backend"), QStringLiteral("mqtt")};
}

int runMandatoryConfigurationReadinessGate() {
  const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  if (readinessGateSkipped(env)) {
    qInfo().noquote() << "[Client][StartupGate][Readiness] 已跳过（CLIENT_SKIP_CONFIG_READINESS_GATE=1 或 "
                         "CLIENT_SKIP_PLATFORM_GATE=1）";
    return 0;
  }

  const StartupReadinessProfile profile = parseStartupReadinessProfile(env);
  Configuration &cfg = Configuration::instance();

  struct NamedUrl {
    QString name;
    QString url;
  };
  QList<NamedUrl> checks;
  checks.append({QStringLiteral("backend"), resolveBackendUrl(cfg)});
  checks.append({QStringLiteral("mqtt"), resolveMqttUrl(cfg)});
  if (profile == StartupReadinessProfile::Full) {
    checks.append({QStringLiteral("keycloak"), resolveKeycloakUrl(cfg)});
    checks.append({QStringLiteral("zlm"), resolveZlmUrl(cfg)});
  }

  for (const NamedUrl &nu : checks) {
    QString err;
    if (!validateNetworkServiceUrl(nu.url, &err)) {
      const char *profileTag =
          profile == StartupReadinessProfile::Full
              ? "full"
              : (profile == StartupReadinessProfile::Minimal ? "minimal" : "standard");
      qCritical().noquote() << "[Client][StartupGate][Readiness] FAIL name=" << nu.name
                            << " url=" << nu.url << " detail=" << err << " profile=" << profileTag;
      printReadinessFailureHelp();
      return 95;
    }
  }

  {
    const char *profileTag =
        profile == StartupReadinessProfile::Full
            ? "full"
            : (profile == StartupReadinessProfile::Minimal ? "minimal" : "standard");
    qInfo().noquote() << "[Client][StartupGate][Readiness] OK profile=" << profileTag
                      << " checkedUrls=" << checks.size();
  }
  return 0;
}

}  // namespace ClientApp
