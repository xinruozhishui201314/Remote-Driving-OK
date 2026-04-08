#include "DecoderFactory.h"
#include <QDebug>
#include <QFile>
#include <QProcess>

#ifdef ENABLE_VAAPI
#include <va/va.h>
#include <va/va_drm.h>
#include <fcntl.h>
#include <unistd.h>
#endif

#ifdef ENABLE_NVDEC
#include "NvdecDecoder.h"
#endif

#ifdef ENABLE_FFMPEG
#include "FFmpegSoftDecoder.h"
#endif

/**
 * 解码器优先级（由高到低）：
 *   1. VAAPIDecoder  — Intel/AMD Linux（DRM Prime 零拷贝，最优）
 *   2. NvdecDecoder  — NVIDIA（CUDA 硬件解码，CPU NV12 输出）
 *   3. FFmpegSoft    — 通用软件解码（YUV420P / NV12 CPU）
 */
std::unique_ptr<IHardwareDecoder> DecoderFactory::create(
    const QString& codec, DecoderPreference pref)
{
    DecoderConfig cfg;
    cfg.codec = codec;

    if (pref == DecoderPreference::HardwareFirst) {

#ifdef ENABLE_VAAPI
        {
            auto dec = std::make_unique<VAAPIDecoder>();
            if (dec->initialize(cfg)) {
                qInfo() << "[Client][DecoderFactory] selected VAAPIDecoder (DRM-Prime zero-copy)"
                        << "codec=" << codec;
                return dec;
            }
            qInfo() << "[Client][DecoderFactory] VAAPI unavailable, trying next";
        }
#endif

#ifdef ENABLE_NVDEC
        {
            auto dec = std::make_unique<NvdecDecoder>();
            if (dec->initialize(cfg)) {
                qInfo() << "[Client][DecoderFactory] selected NvdecDecoder (NVIDIA)"
                        << "codec=" << codec;
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
                    << "codec=" << codec;
            return dec;
        }
        qWarning() << "[Client][DecoderFactory] FFmpeg soft decode init failed";
    }
#endif

    qCritical() << "[Client][DecoderFactory] NO decoder available for codec=" << codec
                << "available=" << availableDecoders();
    return nullptr;
}

QStringList DecoderFactory::availableDecoders()
{
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
    if (list.isEmpty()) list << "(none)";
    return list;
}

bool DecoderFactory::isVaapiAvailable()
{
#ifdef ENABLE_VAAPI
    // 尝试打开DRM设备并检查VAAPI能力
    int drmFd = open("/dev/dri/renderD128", O_RDWR);
    if (drmFd < 0) {
        drmFd = open("/dev/dri/renderD129", O_RDWR);
    }
    if (drmFd < 0) {
        qDebug() << "[Client][DecoderFactory] VAAPI: no DRM render device available";
        return false;
    }

    VADisplay vaDisplay = vaGetDisplayDRM(drmFd);
    if (!vaDisplay) {
        close(drmFd);
        qDebug() << "[Client][DecoderFactory] VAAPI: vaGetDisplayDRM failed";
        return false;
    }

    VAStatus status = vaInitialize(vaDisplay, nullptr, nullptr);
    if (status != VA_STATUS_SUCCESS) {
        close(drmFd);
        qDebug() << "[Client][DecoderFactory] VAAPI: vaInitialize failed" << status;
        return false;
    }

    // 检查H264解码能力
    VAConfigAttrib attrib;
    attrib.type = VAConfigAttribRTFormat;
    status = vaGetConfigAttributes(vaDisplay, VAProfileH264Main, VAEntrypointVLD,
                                   &attrib, 1);
    if (status != VA_STATUS_SUCCESS || !(attrib.value & VA_RT_FORMAT_YUV420)) {
        vaTerminate(vaDisplay);
        close(drmFd);
        qDebug() << "[Client][DecoderFactory] VAAPI: H264 decoding not supported";
        return false;
    }

    vaTerminate(vaDisplay);
    close(drmFd);
    qInfo() << "[Client][DecoderFactory] VAAPI hardware decode available";
    return true;
#else
    return false;
#endif
}

bool DecoderFactory::isNvdecAvailable()
{
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

bool DecoderFactory::isHardwareDecodeAvailable(const QString& type)
{
    if (type == "vaapi" || type == "VAAPI") {
        return isVaapiAvailable();
    }
    if (type == "nvdec" || type == "NVDEC" || type == "cuda" || type == "CUDA") {
        return isNvdecAvailable();
    }
    qWarning() << "[Client][DecoderFactory] Unknown hardware decode type:" << type;
    return false;
}
