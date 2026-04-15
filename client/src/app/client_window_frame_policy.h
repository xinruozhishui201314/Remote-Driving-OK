#pragma once

#include <QProcessEnvironment>
#include <QString>

namespace ClientApp {

/**
 * 窗口装饰策略输入（可由运行时采集或单测构造）。
 * 用于缓解 Docker+X11 下 Qt.FramelessWindowHint 导致客户区「透出」宿桌面的问题。
 */
struct WindowFramePolicyInputs {
  WindowFramePolicyInputs() : platformName(), dockerEnvFileExists(false), procSelfCgroupSnippet(), environment() {}
  QString platformName;
  bool dockerEnvFileExists = false;
  /** /proc/self/cgroup 片段（通常 readAll 前 16KiB 即可） */
  QString procSelfCgroupSnippet;
  QProcessEnvironment environment;
};

struct WindowFramePolicyResult {
  WindowFramePolicyResult() : useWindowFrame(false), decisionReason(), cgroupHit(false), likelyContainerRuntime(false) {}
  bool useWindowFrame = false;
  /** 人类可读决策原因，供日志与 rd_windowFramePolicyReason */
  QString decisionReason;
  bool cgroupHit = false;
  bool likelyContainerRuntime = false;
};

/** 纯函数：无 I/O，便于单测。 */
WindowFramePolicyResult evaluateWindowFramePolicy(const WindowFramePolicyInputs &in);

}  // namespace ClientApp
