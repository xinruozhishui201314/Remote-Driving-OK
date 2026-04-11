#include "client_startup_tcp_gate.h"

#include "client_startup_readiness_gate.h"

#include "core/configuration.h"

#include <QHash>
#include <QProcessEnvironment>
#include <QSet>
#include <QStringList>
#include <QTcpSocket>
#include <QThread>
#include <QUrl>

#include <cstdio>
#include <functional>
#include <future>
#include <utility>

namespace ClientApp {
namespace {

bool envFalsy(const QString &v) {
  const QString s = v.trimmed().toLower();
  return s == QLatin1String("0") || s == QLatin1String("false") || s == QLatin1String("off") ||
         s == QLatin1String("no");
}

bool tcpGateSkipped(const QProcessEnvironment &env) {
  if (qEnvironmentVariableIntValue("CLIENT_SKIP_PLATFORM_GATE") == 1) {
    return true;
  }
  if (qEnvironmentVariableIntValue("CLIENT_SKIP_TCP_STARTUP_GATE") == 1) {
    return true;
  }
  if (env.contains(QStringLiteral("CLIENT_STARTUP_TCP_GATE"))) {
    const QString v = env.value(QStringLiteral("CLIENT_STARTUP_TCP_GATE")).trimmed();
    if (v.isEmpty()) {
      return false;
    }
    if (envFalsy(v)) {
      return true;
    }
  }
  return false;
}

int defaultPortForScheme(const QString &scheme) {
  const QString s = scheme.toLower();
  if (s == QLatin1String("http") || s == QLatin1String("ws")) {
    return 80;
  }
  if (s == QLatin1String("https") || s == QLatin1String("wss")) {
    return 443;
  }
  if (s == QLatin1String("mqtt") || s == QLatin1String("tcp")) {
    return 1883;
  }
  if (s == QLatin1String("mqtts") || s == QLatin1String("ssl")) {
    return 8883;
  }
  return -1;
}

struct Endpoint {
  QString label;
  QString sourceUrl;
  QString host;
  int port = -1;
};

bool buildEndpointFromUrl(const QString &label, const QString &urlStr, Endpoint *out,
                          QString *errOut) {
  if (urlStr.trimmed().isEmpty()) {
    *errOut = QStringLiteral("URL 为空");
    return false;
  }
  const QUrl u = QUrl::fromUserInput(urlStr.trimmed());
  if (!u.isValid() || u.host().isEmpty()) {
    *errOut = QStringLiteral("无法解析主机名");
    return false;
  }
  const QString scheme = u.scheme().toLower();
  if (scheme == QLatin1String("file") || scheme.isEmpty()) {
    *errOut = QStringLiteral("非网络 URL");
    return false;
  }
  int port = u.port();
  if (port <= 0) {
    port = defaultPortForScheme(scheme);
  }
  if (port <= 0) {
    *errOut = QStringLiteral("无法推断端口，请在 URL 中显式指定 :port");
    return false;
  }
  out->label = label;
  out->sourceUrl = urlStr;
  out->host = u.host();
  out->port = port;
  return true;
}

QString tcpProbe(const QString &host, int port, int timeoutMs) {
  QTcpSocket sock;
  sock.connectToHost(host, static_cast<quint16>(port));
  if (!sock.waitForConnected(timeoutMs)) {
    return sock.errorString();
  }
  sock.disconnectFromHost();
  if (sock.state() != QAbstractSocket::UnconnectedState) {
    sock.waitForDisconnected(500);
  }
  return QString();
}

void printTcpGateFailureHelp() {
  std::fprintf(
      stderr,
      "\n"
      "══════════════════════════════════════════════════════════════════════\n"
      "[Client][StartupGate] FATAL exit=96 (TCP_STARTUP_GATE_FAILED)\n"
      "原因: 至少一个配置端点在超时内无法建立 TCP 连接（仅探测端口是否可达）。\n"
      "建议:\n"
      "  1) 确认依赖服务已启动（Backend / Mosquitto / Keycloak / ZLM 等）\n"
      "  2) 容器内使用服务名与 compose 网络；宿主机使用映射端口与可达 IP\n"
      "  3) 检查防火墙、安全组、TLS 终止是否挡在 TCP 之前\n"
      "  4) 默认 TCP 目标随 CLIENT_STARTUP_READINESS_PROFILE：容器内默认 full→四端点；"
      "主机 standard→backend+mqtt。显式: CLIENT_STARTUP_TCP_TARGETS=backend,mqtt,keycloak,zlm\n"
      "  5) 弱网: CLIENT_STARTUP_TCP_ATTEMPTS=4 CLIENT_STARTUP_TCP_TIMEOUT_MS=2000 "
      "CLIENT_STARTUP_TCP_RETRY_GAP_MS=300\n"
      "  6) 无依赖环境(CI): CLIENT_STARTUP_TCP_GATE=0 或 CLIENT_SKIP_TCP_STARTUP_GATE=1\n"
      "══════════════════════════════════════════════════════════════════════\n\n");
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

QString tcpProbeWithRetries(const QString &host, int port, int perAttemptTimeoutMs, int attempts,
                            int gapMs, const QString &label, bool logRetries) {
  QString lastErr;
  for (int i = 0; i < attempts; ++i) {
    lastErr = tcpProbe(host, port, perAttemptTimeoutMs);
    if (lastErr.isEmpty()) {
      if (logRetries && i > 0) {
        qInfo().noquote() << "[Client][StartupGate][Tcp] OK after retry label=" << label
                          << "attempts=" << (i + 1);
      }
      return QString();
    }
    if (logRetries && i + 1 < attempts) {
      qInfo().noquote() << "[Client][StartupGate][Tcp] transient label=" << label
                        << " attempt=" << (i + 1) << "/" << attempts << " err=" << lastErr
                        << " gapMs=" << gapMs;
    }
    if (i + 1 < attempts && gapMs > 0) {
      QThread::msleep(gapMs);
    }
  }
  return lastErr;
}

}  // namespace

int runMandatoryTcpConnectivityGate() {
  const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  if (tcpGateSkipped(env)) {
    qInfo().noquote()
        << "[Client][StartupGate][Tcp] 已跳过（CLIENT_STARTUP_TCP_GATE=0 或 SKIP 系列）";
    return 0;
  }

  int perAttemptTimeoutMs = 1200;
  {
    bool ok = false;
    const int v = env.value(QStringLiteral("CLIENT_STARTUP_TCP_TIMEOUT_MS")).trimmed().toInt(&ok);
    if (ok && v >= 200) {
      perAttemptTimeoutMs = v;
    } else if (ok && v > 0 && v < 200) {
      perAttemptTimeoutMs = 200;
    }
  }

  int attempts = 3;
  {
    bool ok = false;
    const int v = env.value(QStringLiteral("CLIENT_STARTUP_TCP_ATTEMPTS")).trimmed().toInt(&ok);
    if (ok && v >= 1) {
      attempts = v;
    }
  }

  int gapMs = 200;
  {
    bool ok = false;
    const int v = env.value(QStringLiteral("CLIENT_STARTUP_TCP_RETRY_GAP_MS")).trimmed().toInt(&ok);
    if (ok && v >= 0) {
      gapMs = v;
    }
  }

  const bool logRetries = attempts > 1;

  Configuration &cfg = Configuration::instance();

  const QHash<QString, std::function<QString()>> namedResolvers = {
      {QStringLiteral("backend"), [&cfg]() { return resolveBackendUrl(cfg); }},
      {QStringLiteral("mqtt"), [&cfg]() { return resolveMqttUrl(cfg); }},
      {QStringLiteral("keycloak"), [&cfg]() { return resolveKeycloakUrl(cfg); }},
      {QStringLiteral("zlm"), [&cfg]() { return resolveZlmUrl(cfg); }},
  };

  QStringList targetNames;
  const QString targetsRaw = env.value(QStringLiteral("CLIENT_STARTUP_TCP_TARGETS")).trimmed();
  if (targetsRaw.isEmpty()) {
    const StartupReadinessProfile rp = parseStartupReadinessProfile(env);
    targetNames = defaultTcpTargetNamesForReadinessProfile(rp);
    qInfo().noquote() << "[Client][StartupGate][Tcp] CLIENT_STARTUP_TCP_TARGETS 未设置，按 "
                         "CLIENT_STARTUP_READINESS_PROFILE/容器检测 默认 targets="
                      << targetNames.join(QLatin1Char(','));
  } else if (targetsRaw.compare(QLatin1String("none"), Qt::CaseInsensitive) == 0) {
    targetNames.clear();
  } else {
    for (const QString &part : targetsRaw.split(QLatin1Char(','))) {
      const QString t = part.trimmed().toLower();
      if (!t.isEmpty()) {
        targetNames.append(t);
      }
    }
  }

  QList<Endpoint> endpoints;
  QSet<QString> dedupe;

  for (const QString &name : targetNames) {
    auto it = namedResolvers.constFind(name);
    if (it == namedResolvers.cend()) {
      qCritical().noquote() << "[Client][StartupGate][Tcp] 未知目标别名:" << name
                            << "（允许: backend, mqtt, keycloak, zlm）";
      printTcpGateFailureHelp();
      return 96;
    }
    const QString urlStr = it.value()();
    Endpoint ep;
    QString perr;
    if (!buildEndpointFromUrl(name, urlStr, &ep, &perr)) {
      qCritical().noquote() << "[Client][StartupGate][Tcp] 配置 URL 无效 name=" << name
                            << " url=" << urlStr << " detail=" << perr;
      printTcpGateFailureHelp();
      return 96;
    }
    const QString key = ep.host + QLatin1Char(':') + QString::number(ep.port);
    if (dedupe.contains(key)) {
      continue;
    }
    dedupe.insert(key);
    endpoints.append(ep);
  }

  const QString extraRaw = env.value(QStringLiteral("CLIENT_STARTUP_TCP_EXTRA_URLS")).trimmed();
  if (!extraRaw.isEmpty()) {
    int extraIdx = 0;
    for (const QString &part : extraRaw.split(QLatin1Char(','))) {
      const QString urlStr = part.trimmed();
      if (urlStr.isEmpty()) {
        continue;
      }
      Endpoint ep;
      QString perr;
      const QString label =
          QStringLiteral("extra[") + QString::number(extraIdx++) + QLatin1Char(']');
      if (!buildEndpointFromUrl(label, urlStr, &ep, &perr)) {
        qCritical().noquote() << "[Client][StartupGate][Tcp] EXTRA_URLS 无效 url=" << urlStr
                              << " detail=" << perr;
        printTcpGateFailureHelp();
        return 96;
      }
      const QString key = ep.host + QLatin1Char(':') + QString::number(ep.port);
      if (dedupe.contains(key)) {
        continue;
      }
      dedupe.insert(key);
      endpoints.append(ep);
    }
  }

  if (endpoints.isEmpty()) {
    qInfo().noquote()
        << "[Client][StartupGate][Tcp] 无待检测端点（TARGETS=none 且无 EXTRA_URLS），跳过";
    return 0;
  }

  qInfo().noquote() << "[Client][StartupGate][Tcp] 开始并行探测 端点数=" << endpoints.size()
                    << " perAttemptTimeoutMs=" << perAttemptTimeoutMs << " attempts=" << attempts
                    << " retryGapMs=" << gapMs;

  QStringList failures;
  std::vector<std::future<std::pair<QString, QString>>> futures;
  futures.reserve(static_cast<size_t>(endpoints.size()));
  for (const Endpoint &ep : endpoints) {
    futures.push_back(std::async(std::launch::async, [=]() {
      const QString err = tcpProbeWithRetries(ep.host, ep.port, perAttemptTimeoutMs, attempts,
                                              gapMs, ep.label, logRetries);
      if (!err.isEmpty()) {
        const QString line =
            QStringLiteral("%1 (%2 → %3:%4): %5")
                .arg(ep.label, ep.sourceUrl, ep.host, QString::number(ep.port), err);
        qCritical().noquote() << "[Client][StartupGate][Tcp] FAIL" << line;
        return std::make_pair(line, err);
      }
      qInfo().noquote() << "[Client][StartupGate][Tcp] OK" << ep.label << ep.host << ep.port;
      return std::make_pair(QString(), QString());
    }));
  }
  for (auto &fu : futures) {
    const std::pair<QString, QString> r = fu.get();
    if (!r.first.isEmpty()) {
      failures.append(r.first);
    }
  }

  if (!failures.isEmpty()) {
    printTcpGateFailureHelp();
    for (const QString &f : failures) {
      std::fprintf(stderr, "  • %s\n", qPrintable(f));
    }
    qCritical().noquote() << "[Client][StartupGate][Tcp] 共" << failures.size()
                          << "个端点失败 exit=96";
    return 96;
  }

  qInfo().noquote() << "[Client][StartupGate][Tcp] 全部端点 TCP 连通";
  return 0;
}

}  // namespace ClientApp
