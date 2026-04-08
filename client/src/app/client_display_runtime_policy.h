#pragma once

/**
 * 显示栈运行时策略：在 QGuiApplication 之前修正环境，规避已知致命组合
 * （如 LIBGL_ALWAYS_SOFTWARE + QT_XCB_GL_INTEGRATION=xcb_egl 下 XCB_CONN_CLOSED_REQ_LEN_EXCEED）。
 */
namespace ClientDisplayRuntimePolicy {

/** 必须在 QGuiApplication 构造之前调用（Qt 平台插件读取上述环境变量）。 */
void applyPreQGuiApplicationDisplayPolicy();

} // namespace ClientDisplayRuntimePolicy
