#include "DecoderFactory.h"

#include <QDebug>
#include <QFile>
#include <QProcess>
#include <QMutex>
#include <QMutexLocker>

#ifdef ENABLE_VAAPI
#include <adapters/media/VAAPIDecoder.h>
#include <fcntl.h>
#include <unistd.h>
#include <va/va.h>
#include <va/va_drm.h>

// ★ 架构增强：解决多线程并发初始化 libva 导致的 malloc() heap corruption 崩溃
static QMutex s_vaapiProbeMutex;
static bool s_vaapiProbeDone = false;
static bool s_vaapiLastResult = false;
#endif

#ifdef ENABLE_NVDEC
#include <adapters/media/NvdecDecoder.h>
#endif

#ifdef ENABLE_FFMPEG
#include <adapters/media/FFmpegSoftDecoder.h>
#endif

/**
 * 解码器优先级（由高到低）：
 *   1. VAAPIDecoder  — Intel/AMD Linux（DRM Prime 零拷贝，最优）
 *   2. NvdecDecoder  — NVIDIA（CUDA 硬件解码，CPU NV12 输出）
 *   3. FFmpegSoft    — 通用软件解码（YUV420P / NV12 CPU）
 */
std::unique_ptr<IHardwareDecoder> DecoderFactory::create(const QString& codec,
                                                         DecoderPreference pref) {
  DecoderConfig cfg;
  cfg.codec = codec;
  return create(cfg, pref);
}

std::unique_ptr<IHardwareDecoder> DecoderFactory::create(const DecoderConfig& cfgIn,
                                                         DecoderPreference pref) {
  const DecoderConfig& cfg = cfgIn;

  if (pref == DecoderPreference::HardwareFirst) {
#ifdef ENABLE_VAAPI
    if (isVaapiAvailable()) {
      auto dec = std::make_unique<VAAPIDecoder>();
      if (dec->initialize(cfg)) {
        qInfo() << "[Client][DecoderFactory] selected VAAPIDecoder (DRM-Prime zero-copy)"
                << "codec=" << cfg.codec;
        return dec;
      }
      qInfo() << "[Client][DecoderFactory] VAAPI initialization failed, trying next";
    } else {
      qDebug() << "[Client][DecoderFactory] VAAPI probe failed, skipping";
    }
#endif

#ifdef ENABLE_NVDEC
    {
      auto dec = std::make_unique<NvdecDecoder>();
      if (dec->initialize(cfg)) {
        qInfo() << "[Client][DecoderFactory] selected NvdecDecoder (NVIDIA)"
                << "codec=" << cfg.codec;
        return dec;
      }
      qInfo() << "[Client][DecoderFactory] NVDEC unavailable, trying next";
    }
#endif
  }

#ifdef ENABLE_FFMPEG
  {
    auto dec = std::make_unique<FFmpegSoftDecoder>();
    if (dec->initialize(cfg)) {
      qInfo() << "[Client][DecoderFactory] selected FFmpegSoftDecoder (CPU)"
              << "codec=" << cfg.codec;
      return dec;
    }
    qWarning() << "[Client][DecoderFactory] FFmpeg soft decode init failed";
  }
#endif

  qCritical() << "[Client][DecoderFactory] NO decoder available for codec=" << cfg.codec
              << "available=" << availableDecoders();
  return nullptr;
}

QStringList DecoderFactory::availableDecoders() {
  QStringList list;
#ifdef ENABLE_VAAPI
  if (isVaapiAvailable()) {
    list << "VAAPI(DRM-Prime)";
  }
#endif
#ifdef ENABLE_NVDEC
  if (isNvdecAvailable()) {
    list << "NVDEC(CUDA)";
  }
#endif
#ifdef ENABLE_FFMPEG
  list << "FFmpeg(CPU)";
#endif
  if (list.isEmpty())
    list << "(none)";
  return list;
}

bool DecoderFactory::isVaapiAvailable() {
#ifdef ENABLE_VAAPI
  // 环境变量显式禁用 VA-API 探测（彻底隔离崩溃点）
  if (qEnvironmentVariableIsSet("CLIENT_DISABLE_VAAPI_PROBE")) {
    qInfo() << "[Client][DecoderFactory] VAAPI: probe skipped by environment";
    return false;
  }

  // ★ 架构修复：单例互斥探测，防止 4 路流并发初始化导致驱动层堆内存破坏
  QMutexLocker locker(&s_vaapiProbeMutex);
  if (s_vaapiProbeDone) {
    return s_vaapiLastResult;
  }

  // 标记探测已开始/完成（后续流将直接返回缓存结果）
  s_vaapiProbeDone = true;
  s_vaapiLastResult = false;

  // 尝试打开DRM设备并检查VAAPI能力
  static const char* kDrmNodes[] = {"/dev/dri/renderD128", "/dev/dri/renderD129",
                                    "/dev/dri/renderD130", nullptr};

  int drmFd = -1;
  const char* drmPath = nullptr;
  for (int i = 0; kDrmNodes[i]; ++i) {
    int fd = open(kDrmNodes[i], O_RDWR);
    if (fd >= 0) {
      // 检查 Vendor ID。在双显卡机器上，如果是 NVIDIA 则优先走 NVDEC 路径，
      // VA-API 探测 Intel/AMD 以外的驱动容易触发不稳定的 libva 崩溃。
      char vendorPath[256];
      snprintf(vendorPath, sizeof(vendorPath), "/sys/class/drm/%s/device/vendor", kDrmNodes[i] + 9);
      int vfd = open(vendorPath, O_RDONLY);
      if (vfd >= 0) {
        char vendorBuf[16];
        int len = read(vfd, vendorBuf, sizeof(vendorBuf) - 1);
        close(vfd);
        if (len > 0) {
          vendorBuf[len] = '\0';
          uint32_t vendorId = static_cast<uint32_t>(strtol(vendorBuf, nullptr, 16));
          if (vendorId == 0x10de) { // NVIDIA
            qDebug() << "[Client][DecoderFactory] VAAPI: skip probe on NVIDIA device" << kDrmNodes[i];
            close(fd);
            continue;
          }
        }
      }

      drmFd = fd;
      drmPath = kDrmNodes[i];
      break;
    }
  }

  if (drmFd < 0) {
    qDebug() << "[Client][DecoderFactory] VAAPI: no suitable DRM render device available";
    return false;
  }

  VADisplay vaDisplay = vaGetDisplayDRM(drmFd);
  if (!vaDisplay) {
    close(drmFd);
    qDebug() << "[Client][DecoderFactory] VAAPI: vaGetDisplayDRM failed on" << drmPath;
    return false;
  }

  // 重要：传递非空 version 指针，某些 libva 版本在 nullptr 时有崩溃风险；
  // 同时由于 vaInitialize 可能在驱动不匹配时发生无法捕获的 segfault (libva 内部行为)，
  // 之前的 vendor 过滤已经排除了大部分高风险路径。
  int major, minor;
  VAStatus status = vaInitialize(vaDisplay, &major, &minor);
  if (status != VA_STATUS_SUCCESS) {
    close(drmFd);
    qDebug() << "[Client][DecoderFactory] VAAPI: vaInitialize failed" << status << "on" << drmPath;
    return false;
  }

  // 检查H264解码能力
  VAConfigAttrib attrib;
  attrib.type = VAConfigAttribRTFormat;
  status = vaGetConfigAttributes(vaDisplay, VAProfileH264Main, VAEntrypointVLD, &attrib, 1);
  if (status != VA_STATUS_SUCCESS || !(attrib.value & VA_RT_FORMAT_YUV420)) {
    vaTerminate(vaDisplay);
    close(drmFd);
    qDebug() << "[Client][DecoderFactory] VAAPI: H264 decoding not supported on" << drmPath;
    return false;
  }

  vaTerminate(vaDisplay);
  close(drmFd);
  qInfo() << "[Client][DecoderFactory] VAAPI hardware decode available on" << drmPath 
          << "version" << major << "." << minor;
  s_vaapiLastResult = true; // 缓存成功结果
  return true;
#else
  return false;
#endif
}

bool DecoderFactory::isNvdecAvailable() {
#ifdef ENABLE_NVDEC
  // 检查NVIDIA GPU设备文件
  if (QFile::exists("/dev/nvidia0")) {
    qInfo() << "[Client][DecoderFactory] NVDEC: NVIDIA GPU device found via /dev/nvidia0";
    return true;
  }
  if (QFile::exists("/dev/nvidiactl")) {
    qInfo() << "[Client][DecoderFactory] NVDEC: NVIDIA GPU device found via /dev/nvidiactl";
    return true;
  }

  // 检查nvidia-smi命令
  QProcess proc;
  proc.start("nvidia-smi", {"--query-gpu=name", "--format=csv,noheader"});
  if (proc.waitForFinished(2000) && proc.exitCode() == 0) {
    QString gpuName = QString::fromLocal8Bit(proc.readAllStandardOutput()).trimmed();
    if (!gpuName.isEmpty()) {
      qInfo() << "[Client][DecoderFactory] NVDEC: NVIDIA GPU found via nvidia-smi:" << gpuName;
      return true;
    }
  }
  qDebug() << "[Client][DecoderFactory] NVDEC: no NVIDIA GPU detected";
  return false;
#else
  return false;
#endif
}

bool DecoderFactory::isHardwareDecodeAvailable(const QString& type) {
  if (type == "vaapi" || type == "VAAPI") {
    return isVaapiAvailable();
  }
  if (type == "nvdec" || type == "NVDEC" || type == "cuda" || type == "CUDA") {
    return isNvdecAvailable();
  }
  qWarning() << "[Client][DecoderFactory] Unknown hardware decode type:" << type;
  return false;
}
