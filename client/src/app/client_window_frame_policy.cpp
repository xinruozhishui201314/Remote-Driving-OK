#include "client_window_frame_policy.h"

namespace ClientApp {
namespace {

QString trimmedEnv(const QProcessEnvironment &env, const char *key) {
  return env.value(QString::fromUtf8(key)).trimmed();
}

bool cgroupSnippetLooksLikeContainer(const QString &text) {
  if (text.isEmpty()) {
    return false;
  }
  static const QLatin1String needles[] = {
      QLatin1String("docker"), QLatin1String("containerd"), QLatin1String("kubepods"),
      QLatin1String("/lxc/"),  QLatin1String("podman"),     QLatin1String("libpod"),
  };
  for (const QLatin1String &n : needles) {
    if (text.contains(n, Qt::CaseInsensitive)) {
      return true;
    }
  }
  return false;
}

}  // namespace

WindowFramePolicyResult evaluateWindowFramePolicy(const WindowFramePolicyInputs &in) {
  WindowFramePolicyResult r;
  r.cgroupHit = cgroupSnippetLooksLikeContainer(in.procSelfCgroupSnippet);
  r.likelyContainerRuntime =
      in.dockerEnvFileExists || r.cgroupHit ||
      (trimmedEnv(in.environment, "CLIENT_IN_CONTAINER") == QLatin1String("1"));

  const QString forceFrameless = trimmedEnv(in.environment, "CLIENT_FORCE_FRAMELESS");
  if (forceFrameless == QLatin1String("1")) {
    r.useWindowFrame = false;
    r.decisionReason = QStringLiteral("CLIENT_FORCE_FRAMELESS=1");
    return r;
  }

  if (trimmedEnv(in.environment, "CLIENT_USE_WINDOW_FRAME") == QLatin1String("1") ||
      trimmedEnv(in.environment, "CLIENT_DISABLE_FRAMELESS") == QLatin1String("1")) {
    r.useWindowFrame = true;
    r.decisionReason = QStringLiteral("explicit_env_system_frame");
    return r;
  }

  if (trimmedEnv(in.environment, "CLIENT_USE_WINDOW_FRAME") == QLatin1String("0")) {
    r.useWindowFrame = false;
    r.decisionReason = QStringLiteral("CLIENT_USE_WINDOW_FRAME=0");
    return r;
  }

  if (trimmedEnv(in.environment, "CLIENT_AUTO_WINDOW_FRAME_FOR_CONTAINER") == QLatin1String("0")) {
    r.useWindowFrame = false;
    r.decisionReason = QStringLiteral("CLIENT_AUTO_WINDOW_FRAME_FOR_CONTAINER=0");
    return r;
  }

  const bool isXcb = in.platformName.compare(QLatin1String("xcb"), Qt::CaseInsensitive) == 0;
  if (isXcb && r.likelyContainerRuntime) {
    r.useWindowFrame = true;
    r.decisionReason = QStringLiteral("auto_xcb_container_mitigate_transparency");
    return r;
  }

  r.useWindowFrame = false;
  if (!isXcb) {
    r.decisionReason = QStringLiteral("default_frameless_non_xcb");
  } else {
    r.decisionReason = QStringLiteral("default_frameless_native_host");
  }
  return r;
}

}  // namespace ClientApp
