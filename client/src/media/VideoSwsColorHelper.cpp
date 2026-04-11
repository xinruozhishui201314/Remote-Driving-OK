#include "VideoSwsColorHelper.h"

#include <QByteArray>
#include <QDebug>
#include <QString>

#include <atomic>

extern "C" {
#include <libswscale/swscale.h>
}

namespace {

enum class MatrixMode { Bt709Limited, Bt601Limited, Default };

MatrixMode parseMatrixMode() {
  const QByteArray raw = qgetenv("CLIENT_VIDEO_SWS_COLORSPACE");
  if (raw.isEmpty())
    return MatrixMode::Bt709Limited;
  const QString t = QString::fromLatin1(raw).trimmed().toLower();
  if (t == QLatin1String("default") || t == QLatin1String("auto") || t == QLatin1String("ffmpeg"))
    return MatrixMode::Default;
  if (t == QLatin1String("bt601_limited") || t == QLatin1String("601") ||
      t == QLatin1String("itu601") || t == QLatin1String("smpte170m"))
    return MatrixMode::Bt601Limited;
  return MatrixMode::Bt709Limited;
}

}  // namespace

void videoSwsConfigureYuvToRgbaColorspace(SwsContext *sws) {
  if (!sws)
    return;
  const MatrixMode m = parseMatrixMode();
  if (m == MatrixMode::Default)
    return;

  const int cs = (m == MatrixMode::Bt601Limited) ? SWS_CS_ITU601 : SWS_CS_ITU709;
  const int *coeff = sws_getCoefficients(cs);
  if (!coeff)
    return;
  // srcRange=0：Y/Cb/Cr 按 MPEG 有限幅；dstRange=1：输出 RGB 0..255 全范围
  if (sws_setColorspaceDetails(sws, coeff, 0, coeff, 1, 0, 1 << 16, 1 << 16) < 0) {
    static std::atomic<int> s_log{0};
    if (s_log.fetch_add(1, std::memory_order_relaxed) < 6)
      qWarning() << "[Client][Video][SwsColor] sws_setColorspaceDetails failed cs=" << cs;
  }
}

QString videoSwsColorspaceDiagnosticsTag() {
  switch (parseMatrixMode()) {
    case MatrixMode::Bt709Limited:
      return QStringLiteral("bt709_limited");
    case MatrixMode::Bt601Limited:
      return QStringLiteral("bt601_limited");
    case MatrixMode::Default:
      return QStringLiteral("ffmpeg_default");
  }
  return QStringLiteral("bt709_limited");
}
