#pragma once

/**
 * 显示 / OpenGL 栈自动策略（须在 QGuiApplication 之前执行）。
 *
 * 【自动策略底线 — 不可违背】（在下方「显式覆盖」未触发时）：
 *   1) 若检测到 NVIDIA GPU：`nvidia-smi -L` 列出 GPU，或存在 `/dev/nvidia0`
 *      → 必须硬件 GL 栈：qunsetenv(LIBGL_ALWAYS_SOFTWARE)，QT_XCB_GL_INTEGRATION=xcb_egl
 *      （仍会打印 glxinfo 摘要供对照；glxinfo 显示软光栅不改变此决策。）
 *   2) 仅当上述 NVIDIA 提示均不存在时，才根据 glxinfo -B 判定：
 *      - 软件光栅（llvmpipe/lavapipe 等）→ LIBGL_ALWAYS_SOFTWARE=1，QT_XCB_GL_INTEGRATION=glx
 *      - 硬件渲染器串（AMD/Intel 等）→ 硬件栈
 *      - 未判定或跳过 glxinfo：若存在 /dev/dri/renderD*（DRM）→ 硬件栈（与 compose 挂载 /dev/dri 对齐）
 *      - 否则 → 保守软件栈
 *      软件栈用于规避 xcb_egl + llvmpipe → XCB_CONN_CLOSED_REQ_LEN_EXCEED。
 *
 * 结论日志（勿与 glxinfo 诊断混读）：
 *   - grep `[Client][DisplayPolicy][GpuAccel]`：仅三态之一
 *     `GPU_HARDWARE_ACCELERATION=ON` | `OFF` | `UNSPECIFIED`（未跑自动选栈或显式跳过）。
 *   - `[Client][DisplayPolicy][GlxinfoDiag]`：仅诊断参考；NVIDIA 已检测时选栈以 ON 为准，glxinfo 不否定。
 *
 * 显式覆盖 / 跳过（优先生效，不属于「自动策略」）：
 *   - CLIENT_SKIP_AUTO_GL_STACK=1：完全不修改 GL 环境变量
 *   - CLIENT_FORCE_XCB_EGL=1：不修改（调试用）
 *   - CLIENT_ASSUME_HARDWARE_GL=1 / CLIENT_ASSUME_SOFTWARE_GL=1：强制对应栈
 *   - LIBGL_ALWAYS_SOFTWARE=1（启动前已设置）：强制完整软件栈
 *   - CLIENT_SKIP_GLXINFO_PROBE=1：不跑 glxinfo；有 NVIDIA 或 /dev/dri/renderD* 仍倾向硬件栈，否则保守软件栈
 * DISPLAY 未设置：不修改（离屏等场景）
 *
 * 硬件呈现门禁（在 QGuiApplication 之后由 ClientApp::runDisplayEnvironmentCheck 执行）：
 *   - Linux + 交互式 DISPLAY/WAYLAND + QT_QPA_PLATFORM 为空或 xcb：默认要求硬件呈现（等同远控台默认）；
 *     显式关闭：CLIENT_GPU_PRESENTATION_OPTIONAL=1
 *   - CLIENT_REQUIRE_HARDWARE_PRESENTATION=1 或 CLIENT_TELOP_STATION=1：同上强制语义
 *   - 若检测到软件光栅或 LIBGL_ALWAYS_SOFTWARE，则拒绝启动（退出码非 0）；无 DISPLAY/WAYLAND
 *     的会话跳过门禁（CI/离屏）。
 *   - CLIENT_ALLOW_SOFTWARE_PRESENTATION=1：显式允许软光栅，忽略上述门禁。
 *   - 与 CLIENT_SKIP_OPENGL_PROBE=1 同时要求门禁时：无法验证 GL_RENDERER → 拒绝启动。
 *
 * 默认表面格式（在 QGuiApplication 之前由 ClientApp::applyPresentationSurfaceFormatDefaults）：
 *   - 默认请求 swapInterval=1（向驱动申请垂直同步，是否生效取决于栈/驱动）。
 *   - CLIENT_GL_SWAP_INTERVAL=n 覆盖；CLIENT_GL_DEFAULT_FORMAT_SKIP=1 跳过。
 */
namespace ClientDisplayRuntimePolicy {

void applyPreQGuiApplicationDisplayPolicy();

}  // namespace ClientDisplayRuntimePolicy
