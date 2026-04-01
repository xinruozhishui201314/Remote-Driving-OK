#include "DecoderFactory.h"
#include <QDebug>

#ifdef ENABLE_VAAPI
#include "VAAPIDecoder.h"
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
    list << "VAAPI(DRM-Prime)";
#endif
#ifdef ENABLE_NVDEC
    list << "NVDEC(CUDA)";
#endif
#ifdef ENABLE_FFMPEG
    list << "FFmpeg(CPU)";
#endif
    if (list.isEmpty()) list << "(none)";
    return list;
}
