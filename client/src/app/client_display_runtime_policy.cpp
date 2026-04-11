#include "client_display_runtime_policy.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QString>
#include <QtGlobal>

#include <cstdio>

#if defined(Q_OS_LINUX)
#include <sys/stat.h>
#endif

namespace {

bool envTruthy(const QString &v) {
  const QString s = v.trimmed().toLower();
  return s == QLatin1String("1") || s == QLatin1String("true") || s == QLatin1String("yes") ||
         s == QLatin1String("on");
}

#if defined(Q_OS_LINUX)

enum class GlStackClass { Hardware, Software };

enum class GlxinfoClass { SoftwareRaster, HardwareRaster, Unknown };

/** glxinfo -B 文本分类：先识别软件光栅，再识别常见硬件渲染器串。 */
GlxinfoClass classifyGlxinfoText(const QString &text) {
  if (text.isEmpty()) {
    return GlxinfoClass::Unknown;
  }
  const QString low = text.toLower();
  if (low.contains(QLatin1String("llvmpipe")) || low.contains(QLatin1String("softpipe")) ||
      low.contains(QLatin1String("software rasterizer")) || low.contains(QLatin1String("swrast")) ||
      low.contains(QLatin1String("lavapipe"))) {
    return GlxinfoClass::SoftwareRaster;
  }
  // 常见硬件标识（顺序在软件判定之后，避免误伤）
  if (low.contains(QLatin1String("nvidia")) || low.contains(QLatin1String("geforce")) ||
      low.contains(QLatin1String("quadro")) || low.contains(QLatin1String("rtx ")) ||
      low.contains(QLatin1String("gtx ")) || low.contains(QLatin1String("tesla"))) {
    return GlxinfoClass::HardwareRaster;
  }
  if (low.contains(QLatin1String("amd")) || low.contains(QLatin1String("radeon")) ||
      low.contains(QLatin1String("radv"))) {
    return GlxinfoClass::HardwareRaster;
  }
  if (low.contains(QLatin1String("intel"))) {
    return GlxinfoClass::HardwareRaster;
  }
  if (low.contains(QLatin1String("virgl")) || low.contains(QLatin1String("zink"))) {
    return GlxinfoClass::HardwareRaster;
  }
  if (low.contains(QLatin1String("nouveau")) || low.contains(QLatin1String("panfrost")) ||
      low.contains(QLatin1String("freedreno")) || low.contains(QLatin1String("etnaviv"))) {
    return GlxinfoClass::HardwareRaster;
  }
  // 有输出但无明确 token：无法安全判定为硬件（可能是罕见驱动或解析失败）
  return GlxinfoClass::Unknown;
}

struct GlxinfoProbeOutcome {
  bool ran = false;
  bool haveOutput = false;
  GlxinfoClass klass = GlxinfoClass::Unknown;
  QString text;
};

GlxinfoProbeOutcome runGlxinfoProbe() {
  GlxinfoProbeOutcome out;
  const QByteArray disp = qgetenv("DISPLAY");
  if (disp.isEmpty()) {
    return out;
  }
  FILE *pipe = ::popen(
      "sh -c 'command -v glxinfo >/dev/null 2>&1 && exec timeout 4 glxinfo -B 2>/dev/null' || true",
      "r");
  if (!pipe) {
    return out;
  }
  out.ran = true;
  QString acc;
  char buf[512];
  while (std::fgets(buf, sizeof(buf), pipe)) {
    acc += QString::fromLocal8Bit(buf);
  }
  (void)::pclose(pipe);
  if (acc.isEmpty()) {
    return out;
  }
  out.haveOutput = true;
  out.klass = classifyGlxinfoText(acc);
  constexpr int kMaxDiagChars = 800;
  out.text = acc.length() > kMaxDiagChars
                 ? acc.left(kMaxDiagChars).append(QLatin1String("\n...<truncated>"))
                 : acc;
  return out;
}

/** nvidia-smi -L 有 GPU 行时，在缺少 glxinfo 或 glx 上下文异常时辅助假定 NVIDIA 硬件栈可用。 */
bool nvidiaSmiListsGpu() {
  FILE *pipe = ::popen(
      "sh -c 'command -v nvidia-smi >/dev/null 2>&1 && exec timeout 3 nvidia-smi -L 2>/dev/null' "
      "|| true",
      "r");
  if (!pipe) {
    return false;
  }
  QString acc;
  char buf[256];
  while (std::fgets(buf, sizeof(buf), pipe)) {
    acc += QString::fromLocal8Bit(buf);
  }
  (void)::pclose(pipe);
  static const QRegularExpression re(QStringLiteral(R"(GPU\s+\d+:)"));
  return re.match(acc).hasMatch();
}

/** 常见 NVIDIA 容器挂载：无 nvidia-smi 时仍可能有设备节点。 */
bool nvidiaGpuDeviceNodePresent() {
  return QFile::exists(QStringLiteral("/dev/nvidia0"));
}

/** Intel/AMD 等：compose 常挂载 /dev/dri；有 render 节点则倾向硬件栈（glxinfo 失败时不应默认软件）。 */
bool driRenderNodePresent() {
  const QDir dir(QStringLiteral("/dev/dri"));
  if (!dir.exists()) {
    return false;
  }
  const QFileInfoList nodes = dir.entryInfoList(QStringList{QStringLiteral("renderD*")},
                                                QDir::System | QDir::NoDotAndDotDot);
  for (const QFileInfo &fi : nodes) {
    if (!fi.exists()) {
      continue;
    }
    struct stat st {};
    const QByteArray path = fi.absoluteFilePath().toLocal8Bit();
    if (::stat(path.constData(), &st) == 0 && S_ISCHR(st.st_mode)) {
      return true;
    }
  }
  return false;
}

/** 唯一结论行：grep [Client][DisplayPolicy][GpuAccel]；与诊断日志分离，避免「又硬又软」混读。 */
void emitGpuAccelerationVerdict(bool hardwareOn, const char *reason) {
  if (hardwareOn) {
    std::fprintf(stderr,
                 "[Client][DisplayPolicy][GpuAccel] GPU_HARDWARE_ACCELERATION=ON "
                 "QT_XCB_GL_INTEGRATION=xcb_egl LIBGL_ALWAYS_SOFTWARE=unset | %s\n",
                 reason != nullptr ? reason : "");
  } else {
    std::fprintf(stderr,
                 "[Client][DisplayPolicy][GpuAccel] GPU_HARDWARE_ACCELERATION=OFF "
                 "QT_XCB_GL_INTEGRATION=glx LIBGL_ALWAYS_SOFTWARE=1 (CPU software raster) | %s\n",
                 reason != nullptr ? reason : "");
  }
}

void applyHardwareStack(const char *reason) {
  if (!qunsetenv("LIBGL_ALWAYS_SOFTWARE")) {
    std::fprintf(stderr, "[Client][DisplayPolicy] 警告: qunsetenv(LIBGL_ALWAYS_SOFTWARE) 失败\n");
  }
  if (!qputenv("QT_XCB_GL_INTEGRATION", "xcb_egl")) {
    std::fprintf(stderr,
                 "[Client][DisplayPolicy] 警告: qputenv(QT_XCB_GL_INTEGRATION=xcb_egl) 失败\n");
    std::fprintf(stderr,
                 "[Client][DisplayPolicy][GpuAccel] GPU_HARDWARE_ACCELERATION=FAILED "
                 "(env apply error)\n");
    return;
  }
  emitGpuAccelerationVerdict(true, reason);
}

void applySoftwareStack(const char *reason) {
  if (!qputenv("LIBGL_ALWAYS_SOFTWARE", "1")) {
    std::fprintf(stderr, "[Client][DisplayPolicy] 警告: qputenv(LIBGL_ALWAYS_SOFTWARE=1) 失败\n");
  }
  if (!qputenv("QT_XCB_GL_INTEGRATION", "glx")) {
    std::fprintf(stderr, "[Client][DisplayPolicy] 警告: qputenv(QT_XCB_GL_INTEGRATION=glx) 失败\n");
    std::fprintf(stderr,
                 "[Client][DisplayPolicy][GpuAccel] GPU_HARDWARE_ACCELERATION=FAILED "
                 "(env apply error)\n");
    return;
  }
  emitGpuAccelerationVerdict(false, reason);
  std::fprintf(stderr,
               "[Client][DisplayPolicy][GpuAccel][OFF] 若需开启硬件加速："
               "scripts/verify-client-nvidia-gl-prereqs.sh + docker-compose.client-nvidia-gl.yml\n");
}

void logGlxinfoDiagnostic(const GlxinfoProbeOutcome &gx, bool skipProbe) {
  if (skipProbe) {
    std::fprintf(stderr,
                 "[Client][DisplayPolicy] glxinfo 探测已跳过（CLIENT_SKIP_GLXINFO_PROBE）。\n");
    return;
  }
  if (!gx.ran) {
    std::fprintf(stderr, "[Client][DisplayPolicy] glxinfo 未运行（DISPLAY 为空）。\n");
    return;
  }
  if (!gx.haveOutput) {
    std::fprintf(stderr,
                 "[Client][DisplayPolicy] glxinfo 无输出 — 常见: 未安装 "
                 "mesa-utils、timeout、或当前 DISPLAY 上"
                 "无法建立 GLX 上下文。\n");
    return;
  }
  std::fprintf(stderr,
               "[Client][DisplayPolicy][GlxinfoDiag] glxinfo -B（诊断参考；与 [GpuAccel] 结论分开阅读）\n"
               "%s\n",
               qUtf8Printable(gx.text));
}

#endif

}  // namespace

namespace ClientDisplayRuntimePolicy {

void applyPreQGuiApplicationDisplayPolicy() {
#if defined(Q_OS_LINUX)
  const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();

  if (envTruthy(env.value(QStringLiteral("CLIENT_SKIP_AUTO_GL_STACK")))) {
    std::fprintf(
        stderr, "[Client][DisplayPolicy] CLIENT_SKIP_AUTO_GL_STACK=1 — 不修改 GL 相关环境变量。\n");
    std::fprintf(stderr,
                 "[Client][DisplayPolicy][GpuAccel] GPU_HARDWARE_ACCELERATION=UNSPECIFIED "
                 "(CLIENT_SKIP_AUTO_GL_STACK)\n");
    return;
  }

  const QString qpa = env.value(QStringLiteral("QT_QPA_PLATFORM")).trimmed().toLower();
  if (!qpa.isEmpty() && qpa != QLatin1String("xcb")) {
    std::fprintf(stderr,
                 "[Client][DisplayPolicy][GpuAccel] GPU_HARDWARE_ACCELERATION=UNSPECIFIED "
                 "(platform=%s)\n",
                 qUtf8Printable(qpa));
    return;
  }

  const QByteArray disp = qgetenv("DISPLAY");
  if (disp.isEmpty()) {
    std::fprintf(
        stderr,
        "[Client][DisplayPolicy] DISPLAY 未设置 — 跳过自动 GL 栈选择（常见于离屏/测试）。\n");
    std::fprintf(stderr,
                 "[Client][DisplayPolicy][GpuAccel] GPU_HARDWARE_ACCELERATION=UNSPECIFIED "
                 "(DISPLAY unset)\n");
    return;
  }

  if (envTruthy(env.value(QStringLiteral("CLIENT_FORCE_XCB_EGL")))) {
    std::fprintf(stderr,
                 "[Client][DisplayPolicy] CLIENT_FORCE_XCB_EGL=1 — 不修改 QT_XCB_GL_INTEGRATION / "
                 "LIBGL_ALWAYS_SOFTWARE（仅调试，软件栈下可能触发 XCB 超长请求断连）。\n");
    std::fprintf(stderr,
                 "[Client][DisplayPolicy][GpuAccel] GPU_HARDWARE_ACCELERATION=UNSPECIFIED "
                 "(CLIENT_FORCE_XCB_EGL)\n");
    return;
  }

  if (envTruthy(env.value(QStringLiteral("CLIENT_ASSUME_HARDWARE_GL")))) {
    applyHardwareStack("CLIENT_ASSUME_HARDWARE_GL=1");
    return;
  }
  if (envTruthy(env.value(QStringLiteral("CLIENT_ASSUME_SOFTWARE_GL")))) {
    applySoftwareStack("CLIENT_ASSUME_SOFTWARE_GL=1");
    return;
  }

  // 用户显式要求软件 GL：与自动结果对齐为完整软件栈
  if (envTruthy(env.value(QStringLiteral("LIBGL_ALWAYS_SOFTWARE")))) {
    applySoftwareStack("环境变量 LIBGL_ALWAYS_SOFTWARE 已启用");
    return;
  }

  const bool skipGlx = envTruthy(env.value(QStringLiteral("CLIENT_SKIP_GLXINFO_PROBE")));
  GlxinfoProbeOutcome gx;
  if (!skipGlx) {
    gx = runGlxinfoProbe();
  }

  const bool nvidiaSmiLists = nvidiaSmiListsGpu();
  const bool nvidiaDevNode = nvidiaGpuDeviceNodePresent();
  const bool nvidiaHardwareAvailable = nvidiaSmiLists || nvidiaDevNode;

  // ── 自动策略底线（与 client_display_runtime_policy.h 一致，勿改为「glxinfo 优先于 NVIDIA」）──
  // 1) NVIDIA 提示存在 → 强制硬件栈；2) 仅无 NVIDIA 时 → glxinfo 判定软/硬光栅。
  if (nvidiaHardwareAvailable) {
    if (!skipGlx) {
      logGlxinfoDiagnostic(gx, false);
      if (gx.klass == GlxinfoClass::SoftwareRaster) {
        std::fprintf(stderr,
                     "[Client][DisplayPolicy][GlxinfoDiag] classifier=SOFTWARE_RASTER "
                     "（与 [GpuAccel] 无关：已检测到 NVIDIA → 策略仅为 ON，不以 glxinfo 否定硬件加速）\n");
      }
    } else {
      logGlxinfoDiagnostic(gx, true);
    }
    if (nvidiaSmiLists) {
      applyHardwareStack("policy=NVIDIA_SMI_LISTS_GPU");
    } else {
      applyHardwareStack("policy=NVIDIA_DEV_NODE_/dev/nvidia0");
    }
    return;
  }

  if (!skipGlx && gx.klass == GlxinfoClass::SoftwareRaster) {
    logGlxinfoDiagnostic(gx, false);
    applySoftwareStack(
        "policy=NO_NVIDIA_HINT policy=GLXINFO_SOFTWARE_RASTER（llvmpipe/lavapipe 等）");
    return;
  }
  if (!skipGlx && gx.klass == GlxinfoClass::HardwareRaster) {
    logGlxinfoDiagnostic(gx, false);
    applyHardwareStack("policy=NO_NVIDIA_HINT policy=GLXINFO_HARDWARE_RASTER（AMD/Intel 等）");
    return;
  }

  // 无 NVIDIA、glxinfo 未给出硬件结论：若存在 DRM render 节点（常见于挂载了 /dev/dri 的容器/宿主）→ 硬件栈
  if (driRenderNodePresent()) {
    if (!skipGlx) {
      logGlxinfoDiagnostic(gx, false);
    } else {
      logGlxinfoDiagnostic(gx, true);
    }
    applyHardwareStack(
        "policy=NO_NVIDIA_HINT policy=DRI_RENDER_NODE（/dev/dri/renderD*；glxinfo 未知/跳过）");
    return;
  }

  // 无 NVIDIA、无 DRI、glxinfo 未明确硬件或未跑 glxinfo：保守软件栈
  if (!skipGlx) {
    logGlxinfoDiagnostic(gx, false);
  } else {
    logGlxinfoDiagnostic(gx, true);
  }

  applySoftwareStack(
      "policy=NO_NVIDIA_HINT policy=NO_DRI_RENDER_NODE policy=GLXINFO_UNKNOWN_OR_SKIPPED → OFF（保守 "
      "CPU 软件光栅）；有 GPU 请挂载 /dev/dri 或 CLIENT_ASSUME_HARDWARE_GL=1");
#else
  (void)0;
#endif
}

}  // namespace ClientDisplayRuntimePolicy
