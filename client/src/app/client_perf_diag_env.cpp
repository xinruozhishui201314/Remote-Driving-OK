#include "client_perf_diag_env.h"

#include <QProcessEnvironment>
#include <QtGlobal>

#include <cstdio>

namespace ClientPerfDiagEnv {
namespace {

bool envTruthy(const QString &v) {
  const QString s = v.trimmed().toLower();
  return s == QLatin1String("1") || s == QLatin1String("true") || s == QLatin1String("yes") ||
         s == QLatin1String("on");
}

void putIfUnset(const char *key, const char *value) {
  if (qgetenv(key).isEmpty())
    qputenv(key, value);
}

}  // namespace

void applyBeforeQGuiApplication() {
  const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  if (!envTruthy(env.value(QStringLiteral("CLIENT_PERF_DIAG"))))
    return;

  putIfUnset("CLIENT_VIDEO_PRESENT_1HZ_SUMMARY", "1");
  putIfUnset("CLIENT_MAIN_THREAD_STALL_DIAG", "1");
  putIfUnset("CLIENT_VIDEO_SCENE_GL_LOG", "1");
  putIfUnset("CLIENT_VIDEO_PERF_JSON_1HZ", "1");
  putIfUnset("CLIENT_VIDEO_PERF_METRICS_1HZ", "1");
  putIfUnset("CLIENT_VIDEO_PERF_TRACE_SPAN", "1");
  putIfUnset("CLIENT_VIDEO_PERF_RUSAGE_1HZ", "1");

  std::fprintf(stderr,
               "[Client][PerfDiag] CLIENT_PERF_DIAG=1: applied default env for video 1Hz "
               "JSON/metrics/trace/rusage "
               "(only where unset). See client_perf_diag_env.h\n");
}

}  // namespace ClientPerfDiagEnv
