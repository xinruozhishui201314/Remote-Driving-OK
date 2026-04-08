#include "client_display_runtime_policy.h"

#include <QProcessEnvironment>
#include <QString>
#include <QtGlobal>
#include <cstdio>

namespace {

bool envTruthy(const QString &v)
{
    const QString s = v.trimmed().toLower();
    return s == QLatin1String("1") || s == QLatin1String("true") || s == QLatin1String("yes")
        || s == QLatin1String("on");
}

} // namespace

namespace ClientDisplayRuntimePolicy {

void applyPreQGuiApplicationDisplayPolicy()
{
#if defined(Q_OS_LINUX)
    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    if (envTruthy(env.value(QStringLiteral("CLIENT_FORCE_XCB_EGL")))) {
        std::fprintf(stderr,
                     "[Client][DisplayPolicy] CLIENT_FORCE_XCB_EGL 已启用 — 不修改 QT_XCB_GL_INTEGRATION（仅调试）\n");
        return;
    }

    const QString qpa = env.value(QStringLiteral("QT_QPA_PLATFORM")).trimmed().toLower();
    if (!qpa.isEmpty() && qpa != QLatin1String("xcb")) {
        return;
    }

    if (!envTruthy(env.value(QStringLiteral("LIBGL_ALWAYS_SOFTWARE")))) {
        return;
    }

    const QString xcbGl = env.value(QStringLiteral("QT_XCB_GL_INTEGRATION")).trimmed().toLower();
    if (xcbGl == QLatin1String("glx")) {
        return;
    }

    // 默认或显式 xcb_egl：在 Mesa llvmpipe + 高 DPR 大窗场景下曾触发单条 X 请求超过
    // maximum_request_length（libxcb: XCB_CONN_CLOSED_REQ_LEN_EXCEED）。Qt 文档允许
    // QT_XCB_GL_INTEGRATION=glx | xcb_egl；软件 GL 下 glx 路径通常更稳定。
    if (xcbGl.isEmpty() || xcbGl == QLatin1String("xcb_egl")) {
        if (!qputenv("QT_XCB_GL_INTEGRATION", "glx")) {
            std::fprintf(stderr, "[Client][DisplayPolicy] 警告: qputenv(QT_XCB_GL_INTEGRATION=glx) 失败\n");
            return;
        }
        std::fprintf(stderr,
                     "[Client][DisplayPolicy] LIBGL_ALWAYS_SOFTWARE=1 + X11(xcb): 已设置 "
                     "QT_XCB_GL_INTEGRATION=glx，规避 xcb_egl+软件 GL 下 X 单包超长断连"
                     "（XCB_CONN_CLOSED_REQ_LEN_EXCEED）。恢复旧行为请设 CLIENT_FORCE_XCB_EGL=1。\n");
    }
#endif
}

} // namespace ClientDisplayRuntimePolicy
