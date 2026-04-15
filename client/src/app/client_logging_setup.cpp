#include "client_logging_setup.h"

#include "../core/logger.h"
#include "client_crash_diagnostics.h"

#include <QDate>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSocketNotifier>
#include <QtGlobal>

#include <atomic>
#include <cstdio>
#include <cstring>

#if defined(Q_OS_UNIX)
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#endif

namespace {

#if defined(Q_OS_UNIX)
int g_sigPipe[2] = {-1, -1};

void sigintSigtermHandler(int sig) {
  const unsigned char b = (sig == SIGINT) ? 1 : 2;
  if (g_sigPipe[1] >= 0) {
    (void)::write(g_sigPipe[1], &b, 1);
  }
}
#endif

static std::atomic<bool> s_loggingShutdownDone{false};

}  // namespace

namespace ClientLogging {

bool init() {
  QString logPath =
      QProcessEnvironment::systemEnvironment().value(QStringLiteral("CLIENT_LOG_FILE"));
  if (logPath.isEmpty()) {
    const QString day = QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"));
    if (QDir(QStringLiteral("/workspace/logs")).exists()) {
      logPath = QStringLiteral("/workspace/logs/client-") + day + QStringLiteral(".log");
    } else {
      logPath = QStringLiteral("/tmp/remote-driving-client-") + day + QStringLiteral(".log");
    }
  }

  // ── 使用新版结构化异步日志系统 (Logger::instance()) ──────────────────
  Logger::instance().initialize(logPath);
  
  const bool rotationEnabled =
      QProcessEnvironment::systemEnvironment()
          .value(QStringLiteral("CLIENT_LOG_ROTATION_ENABLED"), QStringLiteral("1"))
          .toInt() == 1;
  const int maxKeepFiles =
      QProcessEnvironment::systemEnvironment()
          .value(QStringLiteral("CLIENT_LOG_ROTATION_MAX_FILES"), QStringLiteral("5"))
          .toInt();
          
  Logger::RotationConfig rot;
  rot.enabled = rotationEnabled;
  rot.maxFiles = maxKeepFiles;
  rot.maxSizeMb = 100;
  Logger::instance().setRotationConfig(rot);
  
  // 安装异步消息处理器
  Logger::instance().setMessageInterceptor([](QtMsgType type, const QString& msg) -> QString {
    if (type == QtWarningMsg) {
      return ClientCrashDiagnostics::annotateIfX11Broke(msg);
    }
    return {};
  });
  Logger::instance().installMessageHandler();

  qDebug().noquote() << "[Client][Main] 结构化异步日志已启动，写入:" << logPath
                     << " 轮转:" << (rotationEnabled ? "启用" : "禁用")
                     << " 保留文件数:" << maxKeepFiles;
  return true;
}

void shutdown() {
  if (s_loggingShutdownDone.exchange(true)) {
    return;
  }
  // 恢复默认处理器，防止 shutdown 过程中继续入队
  qInstallMessageHandler(nullptr);
  
  // Logger 的析构函数会等待工作线程完成并关闭文件
  // 这里可以根据需要显式控制清理顺序，目前 Logger 采用单例生存期
  qDebug() << "[Client][Main] 异步日志系统正在关闭...";
}

void installUnixSignalLogFlushAndQuit(QGuiApplication &app) {
#if !defined(Q_OS_UNIX)
  (void)app;
#else
  if (pipe(g_sigPipe) != 0) {
    std::perror("[Client][Main] pipe(SIGINT/SIGTERM) failed");
    return;
  }
  for (int fd : g_sigPipe) {
    const int flags = fcntl(fd, F_GETFL);
    if (flags >= 0) {
      (void)fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
  }

  struct sigaction sa;
  std::memset(&sa, 0, sizeof(sa));
  sa.sa_handler = sigintSigtermHandler;
  sigemptyset(&sa.sa_mask);
  sigaddset(&sa.sa_mask, SIGINT);
  sigaddset(&sa.sa_mask, SIGTERM);
  sa.sa_flags = SA_RESTART;
  if (sigaction(SIGINT, &sa, nullptr) != 0) {
    std::perror("[Client][Main] sigaction(SIGINT) failed");
  }
  if (sigaction(SIGTERM, &sa, nullptr) != 0) {
    std::perror("[Client][Main] sigaction(SIGTERM) failed");
  }

  auto *notifier = new QSocketNotifier(g_sigPipe[0], QSocketNotifier::Read, &app);
  QObject::connect(notifier, &QSocketNotifier::activated, &app, [&app](QSocketDescriptor, QSocketNotifier::Type) {
    unsigned char buf[32];
    for (;;) {
      const ssize_t n = ::read(g_sigPipe[0], buf, sizeof(buf));
      if (n <= 0) {
        break;
      }
      for (ssize_t i = 0; i < n; ++i) {
        if (buf[i] == 1) {
          std::fprintf(stderr,
                       "[Client][Lifecycle] SIGINT (^C): 主线程 flush 日志后退出，预期 exit=130\n");
          ClientLogging::shutdown();
          QCoreApplication::exit(130);
          return;
        }
        if (buf[i] == 2) {
          std::fprintf(stderr,
                       "[Client][Lifecycle] SIGTERM: 主线程 flush 日志后退出，预期 exit=143\n");
          ClientLogging::shutdown();
          QCoreApplication::exit(143);
          return;
        }
      }
    }
  });
#endif
}

}  // namespace ClientLogging
