#include "video_integrity_banner_bridge.h"

#include "core/eventbus.h"

#include <QDebug>

namespace {

QString decodeAlertTitle(const QString &code) {
  if (code == QLatin1String("MULTI_SLICE_MULTITHREAD_STRIPE_RISK"))
    return QStringLiteral("视频解码：多 slice + 多线程条状风险");
  if (code == QLatin1String("SWS_SCALE_INCOMPLETE"))
    return QStringLiteral("视频解码：色彩缩放未完成");
  if (code == QLatin1String("RGBA_STRIDE_ANOMALY"))
    return QStringLiteral("视频解码：RGBA 步幅异常");
  return QStringLiteral("视频解码自检：%1").arg(code);
}

QString presentAlertTitle(const QString &code) {
  if (code == QLatin1String("TEXTURE_SIZE_MISMATCH"))
    return QStringLiteral("视频呈现：纹理尺寸与图像不一致");
  if (code == QLatin1String("DECODE_TO_PRESENT_SIZE_MISMATCH"))
    return QStringLiteral("视频呈现：解码输出与上传图像尺寸不一致");
  if (code == QLatin1String("DECODE_TO_SG_ROWHASH_MISMATCH"))
    return QStringLiteral("视频呈现：解码与 SceneGraph 像素指纹不一致");
  if (code == QLatin1String("DECODE_TO_SG_CRC_MISMATCH"))
    return QStringLiteral("视频呈现：解码与 SceneGraph 整帧 CRC 不一致");
  return QStringLiteral("视频呈现自检：%1").arg(code);
}

}  // namespace

VideoIntegrityBannerBridge::VideoIntegrityBannerBridge(QObject *parent)
    : QObject(parent), m_decodeSub(0), m_presentSub(0) {
  m_decodeSub = EventBus::instance().subscribe<VideoDecodeIntegrityEvent>(
      [this](const VideoDecodeIntegrityEvent &e) {
        const QString title = decodeAlertTitle(e.code);
        QString body = e.detail;
        if (e.mitigationApplied) {
          body += QStringLiteral(
              "\n【已自动处理】强制单线程解码并已请求关键帧；若仍花屏请查编码端 slice 配置。");
        }
        if (e.code == QLatin1String("MULTI_SLICE_MULTITHREAD_STRIPE_RISK")) {
          body += QStringLiteral(
              "\n【编码端建议】H.264 优先每帧单 slice（如 x264/ffmpeg "
              "slices=1）；H.264 无 HEVC 的 loop_filter_across_slices，"
              "跨 slice 去块依赖 slice header；多 slice + 并行 slice 解码易在边界产生带状伪影。");
        }
        if (!e.healthContractLine.isEmpty()) {
          body += QStringLiteral("\n【契约快照 grep [Client][VideoHealth]】") + e.healthContractLine;
        }
        body += QStringLiteral(
            "\n【优先取证】CLIENT_VIDEO_SAVE_FRAME=png + 同 frameId 搜 DECODE_OUT/[FrameDump]；"
            "PNG 与屏显对比区分解码 vs 显示。");
        emit decodeIntegrityBanner(e.stream, e.code, title, body, e.mitigationApplied);
      });

  m_presentSub = EventBus::instance().subscribe<VideoPresentIntegrityEvent>(
      [this](const VideoPresentIntegrityEvent &e) {
        const QString title = presentAlertTitle(e.code);
        QString body = e.detail;
        if (e.suspectGpuCompositor) {
          body += QStringLiteral(
              "\n【判读】疑 GPU/RHI/驱动或窗口合成器；若 DECODE_OUT 与 SG_UPLOAD 的 rowHash 一致则 CPU "
              "缓冲未坏。");
        } else {
          body += QStringLiteral(
              "\n【判读】疑解码→主线程→SceneGraph 之间数据不一致；对照 CLIENT_VIDEO_EVIDENCE_CHAIN 各 "
              "stage。");
        }
        body += QStringLiteral(
            "\n【优先取证】落盘 PNG（CLIENT_VIDEO_SAVE_FRAME=png，文件名 _f<frameId>）与屏显对比；"
            "同 frameId 对齐 DECODE_OUT 与 SG_UPLOAD。");
        emit presentIntegrityBanner(e.stream, e.code, title, body, e.suspectGpuCompositor);
      });

  qInfo() << "[Client][UI][VideoIntegrityBannerBridge] subscribed decode=" << m_decodeSub
          << " present=" << m_presentSub;
}

VideoIntegrityBannerBridge::~VideoIntegrityBannerBridge() {
  if (m_decodeSub > 0)
    EventBus::instance().unsubscribe(m_decodeSub);
  if (m_presentSub > 0)
    EventBus::instance().unsubscribe(m_presentSub);
}
