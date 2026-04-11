#pragma once

#include <QProcessEnvironment>

/**
 * PresentHealth 自动 15s 采样的环境判定（可单测，无 QGuiApplication 依赖）。
 */
namespace ClientPresentHealthAutoEnv {

bool looksLikeCiEnvironment(const QProcessEnvironment &env);
bool isSoftwareGlEnv(const QProcessEnvironment &env);
bool likelyContainerRuntimeEnv(const QProcessEnvironment &env);

}  // namespace ClientPresentHealthAutoEnv
