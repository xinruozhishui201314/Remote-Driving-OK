#include "VideoInterlacedPolicy.h"

#include "infrastructure/media/IHardwareDecoder.h"

#include <QDebug>
#include <QtGlobal>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <vector>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}

namespace {

using VideoInterlacedPolicy::Policy;

Policy parsePolicy(const QByteArray &raw) {
  if (raw.isEmpty())
    return Policy::BlendLines;
  const QString t = QString::fromLatin1(raw).trimmed().toLower();
  if (t == QLatin1String("0") || t == QLatin1String("off") || t == QLatin1String("false") ||
      t == QLatin1String("no") || t == QLatin1String("none"))
    return Policy::Off;
  if (t == QLatin1String("warn") || t == QLatin1String("warn_only") || t == QLatin1String("log"))
    return Policy::WarnOnly;
  if (t == QLatin1String("blend") || t == QLatin1String("blend_lines") || t == QLatin1String("linear"))
    return Policy::BlendLines;
  if (t == QLatin1String("top") || t == QLatin1String("top_field") || t == QLatin1String("tff_dup") ||
      t == QLatin1String("field_top"))
    return Policy::TopFieldDup;
  if (t == QLatin1String("bottom") || t == QLatin1String("bottom_field") || t == QLatin1String("bff_dup") ||
      t == QLatin1String("field_bottom"))
    return Policy::BottomFieldDup;
  return Policy::BlendLines;
}

bool interlacedFlags(const AVFrame *f) {
  if (!f)
    return false;
  // 与 libavutil AV_FRAME_FLAG_INTERLACED（1<<5）一致，避免旧版头文件宏差异
  return (f->flags & (1 << 5)) != 0;
}

void blendPlane(uint8_t *base, int linesize, int w, int h) {
  if (h < 3 || w <= 0 || !base)
    return;
  std::vector<uint8_t> line(static_cast<size_t>(w));
  for (int y = 1; y < h - 1; y += 2) {
    const uint8_t *prev = base + static_cast<ptrdiff_t>(y - 1) * linesize;
    const uint8_t *next = base + static_cast<ptrdiff_t>(y + 1) * linesize;
    uint8_t *row = base + static_cast<ptrdiff_t>(y) * linesize;
    for (int x = 0; x < w; ++x)
      line[static_cast<size_t>(x)] =
          static_cast<uint8_t>((static_cast<int>(prev[x]) + static_cast<int>(next[x]) + 1) >> 1);
    memcpy(row, line.data(), static_cast<size_t>(w));
  }
}

void fieldDupPlane(uint8_t *base, int linesize, int w, int h, bool topField) {
  if (h < 2 || w <= 0 || !base)
    return;
  std::vector<uint8_t> line(static_cast<size_t>(w));
  for (int y = h - 1; y >= 0; --y) {
    const int srcY = topField ? (y / 2) * 2 : (std::min)((y / 2) * 2 + 1, h - 1);
    const uint8_t *src = base + static_cast<ptrdiff_t>(srcY) * linesize;
    memcpy(line.data(), src, static_cast<size_t>(w));
    uint8_t *dst = base + static_cast<ptrdiff_t>(y) * linesize;
    memcpy(dst, line.data(), static_cast<size_t>(w));
  }
}

void processYuv420p(AVFrame *frame, Policy p) {
  const int w = frame->width;
  const int h = frame->height;
  uint8_t *y = frame->data[0];
  int lsY = frame->linesize[0];
  uint8_t *u = frame->data[1];
  int lsU = frame->linesize[1];
  uint8_t *v = frame->data[2];
  int lsV = frame->linesize[2];
  const int ch = std::max(1, h / 2);

  switch (p) {
    case Policy::Off:
    case Policy::WarnOnly:
      break;
    case Policy::BlendLines:
      blendPlane(y, lsY, w, h);
      blendPlane(u, lsU, w / 2, ch);
      blendPlane(v, lsV, w / 2, ch);
      break;
    case Policy::TopFieldDup:
      fieldDupPlane(y, lsY, w, h, true);
      fieldDupPlane(u, lsU, w / 2, ch, true);
      fieldDupPlane(v, lsV, w / 2, ch, true);
      break;
    case Policy::BottomFieldDup:
      fieldDupPlane(y, lsY, w, h, false);
      fieldDupPlane(u, lsU, w / 2, ch, false);
      fieldDupPlane(v, lsV, w / 2, ch, false);
      break;
  }
}

void processNv12(AVFrame *frame, Policy p) {
  const int w = frame->width;
  const int h = frame->height;
  uint8_t *y = frame->data[0];
  int lsY = frame->linesize[0];
  uint8_t *uv = frame->data[1];
  int lsUv = frame->linesize[1];
  const int ch = std::max(1, h / 2);

  switch (p) {
    case Policy::Off:
    case Policy::WarnOnly:
      break;
    case Policy::BlendLines:
      blendPlane(y, lsY, w, h);
      blendPlane(uv, lsUv, w, ch);
      break;
    case Policy::TopFieldDup:
      fieldDupPlane(y, lsY, w, h, true);
      fieldDupPlane(uv, lsUv, w, ch, true);
      break;
    case Policy::BottomFieldDup:
      fieldDupPlane(y, lsY, w, h, false);
      fieldDupPlane(uv, lsUv, w, ch, false);
      break;
  }
}

}  // namespace

namespace VideoInterlacedPolicy {

Policy currentFromEnv() { return parsePolicy(qgetenv("CLIENT_VIDEO_INTERLACED_POLICY")); }

QString envRaw() { return QString::fromUtf8(qgetenv("CLIENT_VIDEO_INTERLACED_POLICY")); }

QString diagnosticsTag() {
  switch (currentFromEnv()) {
    case Policy::Off:
      return QStringLiteral("off");
    case Policy::WarnOnly:
      return QStringLiteral("warn");
    case Policy::BlendLines:
      return QStringLiteral("blend");
    case Policy::TopFieldDup:
      return QStringLiteral("top_dup");
    case Policy::BottomFieldDup:
      return QStringLiteral("bottom_dup");
  }
  return QStringLiteral("blend");
}

bool avFrameIsInterlaced(const AVFrame *frame) { return interlacedFlags(frame); }

void maybeApplyAvFrame(AVFrame *frame, const QString &streamTag) {
  if (!frame)
    return;
  if (!interlacedFlags(frame)) {
    // ★ v4 Hyper-Logging：记录为什么不应用策略，彻底排除此模块嫌疑
    static QHash<QString, int> s_nonInterlacedLog;
    if (++s_nonInterlacedLog[streamTag] <= 1 || (s_nonInterlacedLog[streamTag] % 1000 == 0)) {
        qInfo() << "[Client][Video][InterlacedDiag] stream=" << streamTag << " skip_policy: AVFrame NOT marked as interlaced (flags=" << frame->flags << ")";
    }
    return;
  }
  const Policy p = currentFromEnv();
  
  // ── ★ 深入诊断：隔行扫描策略触发 ──────────────────────────────────────────
  static QHash<QString, int> s_interlacedLog;
  if (++s_interlacedLog[streamTag] <= 10 || (s_interlacedLog[streamTag] % 100 == 0)) {
      qWarning().noquote() << QStringLiteral("[Client][Video][InterlacedDiag] stream=%1 fid=?(AVFrame) fmt=%2 sz=%3x%4 "
                                             "topFirst=%5 interlaced_frame=%6 flags=0x%7 policy=%8 ★检测到隔行元数据，可能导致 fine=200 条状")
                               .arg(streamTag).arg(frame->format).arg(frame->width).arg(frame->height)
                               .arg(frame->top_field_first).arg(frame->interlaced_frame).arg(frame->flags, 0, 16)
                               .arg(static_cast<int>(p));
  }

  if (p == Policy::Off)
    return;

  qInfo() << "[Client][Video][Interlaced] ACTUALLY applying policy=" << static_cast<int>(p) << " to stream=" << streamTag;

  if (p == Policy::WarnOnly) {
    static std::atomic<int> s_warn{0};
    const int n = s_warn.fetch_add(1, std::memory_order_relaxed);
    if (n < 12 || (n % 120) == 0) {
      qWarning().noquote()
          << "[Client][Video][Interlaced][WARN_ONLY] stream=" << streamTag << " wxh=" << frame->width << "x"
          << frame->height << " fmt=" << frame->format << " ★未改像素；可改 CLIENT_VIDEO_INTERLACED_POLICY=blend";
    }
    return;
  }

  const AVPixelFormat fmt = static_cast<AVPixelFormat>(frame->format);
  if (fmt == AV_PIX_FMT_YUV420P || fmt == AV_PIX_FMT_YUVJ420P) {
    qDebug() << "[Client][Video][Interlaced] Applying processYuv420p policy=" << static_cast<int>(p) << " stream=" << streamTag;
    processYuv420p(frame, p);
  } else if (fmt == AV_PIX_FMT_NV12) {
    qDebug() << "[Client][Video][Interlaced] Applying processNv12 policy=" << static_cast<int>(p) << " stream=" << streamTag;
    processNv12(frame, p);
  } else {
    static std::atomic<int> s_unsup{0};
    if (s_unsup.fetch_add(1, std::memory_order_relaxed) < 6) {
      qWarning() << "[Client][Video][Interlaced] 跳过不支持的像素格式 fmt=" << fmt << " stream=" << streamTag;
    }
    return;
  }
}

void maybeApplyToNv12VideoFrame(VideoFrame &vf, const QString &streamTag) {
  if (!vf.interlacedMetadata || vf.pixelFormat != VideoFrame::PixelFormat::NV12 ||
      vf.memoryType != VideoFrame::MemoryType::CPU_MEMORY)
    return;
  if (!vf.planes[0].data || !vf.planes[1].data)
    return;

  const Policy p = currentFromEnv();
  if (p == Policy::Off)
    return;
  if (p == Policy::WarnOnly) {
    static std::atomic<int> s_hwWarn{0};
    const int n = s_hwWarn.fetch_add(1, std::memory_order_relaxed);
    if (n < 12 || (n % 120) == 0) {
      qWarning().noquote()
          << "[Client][Video][Interlaced][WARN_ONLY][NV12] stream=" << streamTag << " wxh=" << vf.width << "x"
          << vf.height << " ★未改像素";
    }
    return;
  }

  AVFrame stub{};
  stub.format = AV_PIX_FMT_NV12;
  stub.width = static_cast<int>(vf.width);
  stub.height = static_cast<int>(vf.height);
  stub.data[0] = static_cast<uint8_t *>(vf.planes[0].data);
  stub.linesize[0] = static_cast<int>(vf.planes[0].stride);
  stub.data[1] = static_cast<uint8_t *>(vf.planes[1].data);
  stub.linesize[1] = static_cast<int>(vf.planes[1].stride);
  processNv12(&stub, p);
}

}  // namespace VideoInterlacedPolicy
