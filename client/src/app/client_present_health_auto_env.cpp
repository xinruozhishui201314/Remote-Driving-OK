#include "client_present_health_auto_env.h"

#include <QFile>

namespace ClientPresentHealthAutoEnv {
namespace {

bool envNonempty(const QProcessEnvironment &env, const char *k) {
  return !env.value(QString::fromUtf8(k)).trimmed().isEmpty();
}

}  // namespace

bool looksLikeCiEnvironment(const QProcessEnvironment &env) {
  const QString ci = env.value(QStringLiteral("CI")).trimmed().toLower();
  if (ci == QLatin1String("1") || ci == QLatin1String("true")) {
    return true;
  }
  static const char *kMarkers[] = {
      "GITHUB_ACTIONS", "GITLAB_CI",        "BUILDKITE",        "JENKINS_URL",
      "TEAMCITY_VERSION", "CIRCLECI",       "TRAVIS",           "APPVEYOR",
      "AZURE_HTTP_USER_AGENT", "CONTINUOUS_INTEGRATION",
  };
  for (const char *m : kMarkers) {
    if (envNonempty(env, m)) {
      return true;
    }
  }
  return false;
}

bool isSoftwareGlEnv(const QProcessEnvironment &env) {
  return env.value(QStringLiteral("LIBGL_ALWAYS_SOFTWARE")).trimmed() == QLatin1String("1");
}

bool likelyContainerRuntimeEnv(const QProcessEnvironment &env) {
  if (env.value(QStringLiteral("CLIENT_IN_CONTAINER")).trimmed() == QLatin1String("1")) {
    return true;
  }
#if defined(Q_OS_LINUX)
  if (QFile::exists(QStringLiteral("/.dockerenv"))) {
    return true;
  }
#endif
  return false;
}

}  // namespace ClientPresentHealthAutoEnv
