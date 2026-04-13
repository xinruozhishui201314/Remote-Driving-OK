#include "H264ClientDiag.h"

#include <QDir>
#include <QFile>
#include <QHash>
#include <QSaveFile>
#include <QStandardPaths>
#include <QtGlobal>

#include <cmath>
#include <cstring>

namespace {

bool envTruthy(const QByteArray &raw) {
  if (raw.isEmpty())
    return false;
  const QByteArray s = raw.trimmed().toLower();
  return s != "0" && s != "false" && s != "off" && s != "no";
}

QString sanitizedTag(const QString &streamTag) {
  QString s;
  s.reserve(streamTag.size());
  for (QChar c : streamTag) {
    if (c.isLetterOrNumber() || c == QLatin1Char('-') || c == QLatin1Char('_'))
      s.append(c);
    else if (c == QLatin1Char(':'))
      s.append(QLatin1Char('_'));
    else
      s.append(QLatin1Char('_'));
  }
  if (s.isEmpty())
    s = QLatin1String("stream");
  return s;
}

QString saveFrameMode() {
  const QByteArray v = qgetenv("CLIENT_VIDEO_SAVE_FRAME").trimmed().toLower();
  return QString::fromLatin1(v);
}

int saveFrameMax() {
  bool ok = false;
  const int n = qEnvironmentVariableIntValue("CLIENT_VIDEO_SAVE_FRAME_MAX", &ok);
  if (!ok || n <= 0)
    return 3;
  return qMin(n, 1000);
}

QString saveFrameDir() {
  QByteArray d = qgetenv("CLIENT_VIDEO_SAVE_FRAME_DIR").trimmed();
  if (d.isEmpty()) {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    return QDir(base).filePath(QStringLiteral("remote-driving-frame-diag"));
  }
  return QString::fromLocal8Bit(d);
}

// ── H.264 RBSP bit reader (ITU-T H.264 7.2) ─────────────────────────────────

struct BitReader {
  const uint8_t *d = nullptr;
  size_t nbits = 0;
  size_t pos = 0;

  BitReader(const uint8_t *bytes, size_t nbytes) : d(bytes), nbits(nbytes * 8u) {}

  bool haveBits(size_t n) const { return pos + n <= nbits; }

  unsigned u(int nbits) {
    unsigned v = 0;
    for (int i = 0; i < nbits; ++i) {
      if (!haveBits(1))
        return v;
      const size_t byteIdx = pos >> 3;
      const int bitIdx = 7 - static_cast<int>(pos & 7u);
      v = (v << 1) | ((d[byteIdx] >> bitIdx) & 1u);
      ++pos;
    }
    return v;
  }

  unsigned ue() {
    int lz = 0;
    while (haveBits(1) && u(1) == 0 && lz < 31)
      ++lz;
    if (lz >= 31)
      return 0;
    const unsigned suffix = (lz > 0) ? u(lz) : 0;
    return (1u << lz) - 1u + suffix;
  }

  int se() {
    const unsigned codeNum = ue();
    const unsigned k = (codeNum + 1u) / 2u;
    return ((codeNum & 1u) != 0) ? static_cast<int>(k) : -static_cast<int>(k);
  }
};

QByteArray nalToRbsp(const QByteArray &nalWithHeader) {
  if (nalWithHeader.size() < 2)
    return {};
  const QByteArray raw = nalWithHeader.mid(1);
  QByteArray out;
  out.reserve(raw.size());
  for (int i = 0; i < raw.size(); ++i) {
    if (i >= 2 && static_cast<uchar>(raw[i]) == 0x03u && static_cast<uchar>(raw[i - 1]) == 0x00u &&
        static_cast<uchar>(raw[i - 2]) == 0x00u) {
      continue;  // emulation prevention 0x03
    }
    out.append(raw[i]);
  }
  return out;
}

bool skipSpsHighProfileExtension(BitReader &br, int profileIdc, QString *err) {
  static const int kHighProfiles[] = {100, 110, 122, 244, 44, 83, 86, 118, 128, 138, 139, 134};
  bool high = false;
  for (int p : kHighProfiles) {
    if (p == profileIdc) {
      high = true;
      break;
    }
  }
  if (!high)
    return true;

  const unsigned chroma = br.ue();
  if (!br.haveBits(1)) {
    if (err)
      *err = QStringLiteral("SPS RBSP 截断（high profile 分支）");
    return false;
  }
  if (chroma == 3u)
    (void)br.u(1);  // separate_colour_plane_flag
  (void)br.ue();    // bit_depth_luma_minus8
  (void)br.ue();    // bit_depth_chroma_minus8
  (void)br.u(1);    // qpprime_y_zero_transform_bypass_flag
  if (!br.haveBits(1)) {
    if (err)
      *err = QStringLiteral("SPS RBSP 截断（high profile 分支）");
    return false;
  }
  const unsigned seqScaling = br.u(1);
  if (seqScaling != 0u) {
    if (err)
      *err =
          QStringLiteral("SPS seq_scaling_matrix_present_flag=1（未完整跳过），请用 ffprobe 查看");
    return false;
  }
  return true;
}

bool skipSliceGroupMap(BitReader &br, unsigned numSliceGroupsMinus1, QString *err) {
  if (numSliceGroupsMinus1 == 0u)
    return true;
  const unsigned mapType = br.ue();
  if (mapType == 0u) {
    for (unsigned i = 0; i <= numSliceGroupsMinus1; ++i) {
      (void)br.ue();  // run_length_minus1
    }
    return br.haveBits(0);
  }
  if (mapType == 2u) {
    for (unsigned i = 0; i < numSliceGroupsMinus1; ++i) {
      (void)br.ue();  // top_left
      (void)br.ue();  // bottom_right
    }
    return br.haveBits(0);
  }
  if (mapType == 3u || mapType == 4u || mapType == 5u) {
    (void)br.u(1);
    (void)br.ue();
    return br.haveBits(0);
  }
  if (mapType == 6u) {
    const unsigned picSizeInMapUnitsMinus1 = br.ue();
    const unsigned bitsPerId =
        static_cast<unsigned>(std::ceil(std::log2(static_cast<double>(numSliceGroupsMinus1 + 1u))));
    const int bits = static_cast<int>(qMin(bitsPerId, 31u));
    for (unsigned i = 0; i <= picSizeInMapUnitsMinus1; ++i) {
      if (!br.haveBits(static_cast<size_t>(bits))) {
        if (err)
          *err = QStringLiteral("PPS slice_group_map_type=6 RBSP 不足");
        return false;
      }
      (void)br.u(bits);
    }
    return true;
  }
  if (err)
    *err = QStringLiteral("未知 slice_group_map_type=%1").arg(mapType);
  return false;
}

}  // namespace

namespace H264ClientDiag {

void maybeDumpDecodedFrame(const QImage &frameRgba8888, const QString &streamTag, quint64 frameId,
                           int *inOutSavedCount) {
  if (!inOutSavedCount || frameRgba8888.isNull())
    return;
  const QString mode = saveFrameMode();
  if (mode.isEmpty() || mode == QLatin1String("0") || mode == QLatin1String("off"))
    return;

  const int cap = saveFrameMax();
  if (*inOutSavedCount >= cap)
    return;

  const QString dirPath = saveFrameDir();
  QDir dir;
  if (!dir.mkpath(dirPath)) {
    qWarning() << "[H264][FrameDump] mkdir failed:" << dirPath;
    return;
  }

  const QString tag = sanitizedTag(streamTag);
  const QString base = QStringLiteral("%1/%2_f%3").arg(dirPath, tag).arg(frameId, 0, 10);

  const bool wantPng =
      (mode == QLatin1String("png") || mode == QLatin1String("both") || mode == QLatin1String("1"));
  const bool wantRaw =
      (mode == QLatin1String("raw") || mode == QLatin1String("both") || mode == QLatin1String("1"));

  bool anyOk = false;
  const QImage img = frameRgba8888.format() == QImage::Format_RGBA8888
                         ? frameRgba8888
                         : frameRgba8888.convertToFormat(QImage::Format_RGBA8888);

  if (wantPng) {
    const QString pngPath = base + QStringLiteral(".png");
    if (!img.save(pngPath, "PNG")) {
      qWarning() << "[H264][FrameDump] PNG save failed:" << pngPath;
    } else {
      anyOk = true;
      qInfo().noquote()
          << "[H264][FrameDump] ★ saved PNG（解码后 RGBA，SceneGraph 前）" << pngPath
          << "stream=" << streamTag << " frameId=" << frameId << " size=" << img.size()
          << "bpl=" << img.bytesPerLine()
          << "★优先对照：同 frameId 搜 DECODE_OUT|VideoEvidence|RS_APPLY|SG_UPLOAD；PNG 花=上游解码/sws，"
             "PNG 好屏花=GL/纹理/合成";
    }
  }
  if (wantRaw) {
    const QString rawPath =
        base + QStringLiteral(".w%1_h%2.rgba").arg(img.width()).arg(img.height());
    QSaveFile f(rawPath);
    if (!f.open(QIODevice::WriteOnly)) {
      qWarning() << "[H264][FrameDump] RAW open failed:" << rawPath << f.errorString();
    } else {
      const QByteArray bytes(reinterpret_cast<const char *>(img.constBits()),
                             static_cast<int>(img.sizeInBytes()));
      if (f.write(bytes) != bytes.size()) {
        qWarning() << "[H264][FrameDump] RAW write incomplete:" << rawPath;
        f.cancelWriting();
      } else if (!f.commit()) {
        qWarning() << "[H264][FrameDump] RAW commit failed:" << rawPath;
      } else {
        anyOk = true;
        qInfo().noquote()
            << "[H264][FrameDump] ★ saved RAW RGBA row-major, stride=bpl" << rawPath
            << "stream=" << streamTag << " frameId=" << frameId << " bytes=" << bytes.size()
            << "bpl=" << img.bytesPerLine()
            << "★ ffplay -f rawvideo -pixel_format rgba -video_size"
            << QStringLiteral("%1x%2").arg(img.width()).arg(img.height()) << rawPath
            << "★ 与 PNG 同语义；对照同 frameId 的 DECODE_OUT/Evidence 行";
      }
    }
  }

  if (anyOk)
    ++(*inOutSavedCount);
}

bool stripeAlertCaptureEnabled() {
  return envTruthy(qgetenv("CLIENT_VIDEO_STRIPE_ALERT_CAPTURE"));
}

int stripeAlertCaptureMaxPerStream() {
  bool ok = false;
  const int n = qEnvironmentVariableIntValue("CLIENT_VIDEO_STRIPE_ALERT_CAPTURE_MAX", &ok);
  if (!ok || n <= 0)
    return 24;
  return qMin(n, 500);
}

void maybeDumpStripeAlertCapture(const QImage &frameRgba8888, const QString &streamTag, quint64 frameId,
                                 const QString &verdictTag, int shift, int fineTop, int fineMid, int fineBot) {
  if (!stripeAlertCaptureEnabled() || frameRgba8888.isNull())
    return;

  static QHash<QString, int> s_savedByStream;
  const int cap = stripeAlertCaptureMaxPerStream();
  if (s_savedByStream.value(streamTag, 0) >= cap)
    return;

  const QString dirPath = QDir(saveFrameDir()).filePath(QStringLiteral("stripe-alerts"));
  QDir dir;
  if (!dir.mkpath(dirPath)) {
    qWarning() << "[H264][StripeAlertCapture] mkdir failed:" << dirPath;
    return;
  }

  const QString tag = sanitizedTag(streamTag);
  const QString vt = verdictTag.isEmpty() ? QStringLiteral("unknown") : verdictTag;
  const QString pngPath =
      QStringLiteral("%1/%2_f%3_%4_sh%5_t%6_m%7_b%8.png")
          .arg(dirPath, tag)
          .arg(frameId, 0, 10)
          .arg(vt)
          .arg(shift)
          .arg(fineTop)
          .arg(fineMid)
          .arg(fineBot);

  const QImage img = frameRgba8888.format() == QImage::Format_RGBA8888
                         ? frameRgba8888
                         : frameRgba8888.convertToFormat(QImage::Format_RGBA8888);
  if (!img.save(pngPath, "PNG")) {
    qWarning() << "[H264][StripeAlertCapture] PNG save failed:" << pngPath;
    return;
  }

  ++s_savedByStream[streamTag];
  qInfo().noquote()
      << "[H264][StripeAlertCapture] ★ saved PNG verdict=" << vt << " path=" << pngPath
      << " stream=" << streamTag << " frameId=" << frameId << " shift=" << shift << " top=" << fineTop
      << " mid=" << fineMid << " bot=" << fineBot
      << " ★ 对照同 fid 的 [H264][STRIPE_VERDICT] 与 DECODE_OUT；fp_top+PNG 仅顶平→误报 suspect+全帧坏→真损坏";
}

void logParameterSetsIfRequested(const QString &streamTag, const QByteArray &spsNalWithHeader,
                                 const QByteArray &ppsNalWithHeader) {
  if (!envTruthy(qgetenv("CLIENT_VIDEO_H264_PARAM_DIAG")))
    return;
  if (spsNalWithHeader.isEmpty() || ppsNalWithHeader.isEmpty())
    return;

  const int spsNalType = static_cast<uchar>(spsNalWithHeader[0]) & 0x1f;
  const int ppsNalType = static_cast<uchar>(ppsNalWithHeader[0]) & 0x1f;
  if (spsNalType != 7 || ppsNalType != 8) {
    qWarning() << "[H264][ParamDiag] unexpected NAL types SPS=" << spsNalType
               << "PPS=" << ppsNalType;
    return;
  }

  const QByteArray spsRbsp = nalToRbsp(spsNalWithHeader);
  const QByteArray ppsRbsp = nalToRbsp(ppsNalWithHeader);
  if (spsRbsp.isEmpty() || ppsRbsp.isEmpty()) {
    qWarning() << "[H264][ParamDiag] empty RBSP stream=" << streamTag;
    return;
  }

  QString spsExtra;
  int profileIdc = -1;
  int levelIdc = -1;
  int constraintByte = 0;
  unsigned spsId = 0;
  {
    BitReader sbr(reinterpret_cast<const uint8_t *>(spsRbsp.constData()),
                  static_cast<size_t>(spsRbsp.size()));
    profileIdc = static_cast<int>(sbr.u(8));
    constraintByte = static_cast<int>(sbr.u(8));
    levelIdc = static_cast<int>(sbr.u(8));
    spsId = sbr.ue();
    if (!skipSpsHighProfileExtension(sbr, profileIdc, &spsExtra)) {
      spsExtra = QStringLiteral("（SPS 未完整展开） ") + spsExtra;
    } else {
      (void)sbr.ue();  // log2_max_frame_num_minus4
      const unsigned pocType = sbr.ue();
      if (pocType == 0u) {
        (void)sbr.ue();
      } else if (pocType == 1u) {
        (void)sbr.u(1);
        const int n = static_cast<int>(sbr.se());
        for (int i = 0; i < n; ++i)
          (void)sbr.se();
        const unsigned numRef = sbr.ue();
        for (unsigned i = 0; i <= numRef; ++i)
          (void)sbr.u(1);
      }
      (void)sbr.ue();
      (void)sbr.u(1);
      (void)sbr.ue();
      (void)sbr.ue();
      const unsigned frameMbsOnly = sbr.u(1);
      if (frameMbsOnly == 0u)
        (void)sbr.u(1);
      (void)sbr.u(1);
      const unsigned frameCropping = sbr.u(1);
      if (frameCropping != 0u) {
        (void)sbr.ue();
        (void)sbr.ue();
        (void)sbr.ue();
        (void)sbr.ue();
      }
      (void)sbr.u(1);
      spsExtra = QStringLiteral("SPS 语法解析到 vui_parameters_present_flag（未展开 VUI）");
    }
  }

  QString ppsErr;
  unsigned ppsId = 0;
  unsigned ppsSpsId = 0;
  unsigned numSg = 0;
  unsigned deblockCtlPresent = 0;
  {
    BitReader br(reinterpret_cast<const uint8_t *>(ppsRbsp.constData()),
                 static_cast<size_t>(ppsRbsp.size()));
    ppsId = br.ue();
    ppsSpsId = br.ue();
    (void)br.u(1);
    (void)br.u(1);
    numSg = br.ue();
    if (!skipSliceGroupMap(br, numSg, &ppsErr)) {
      qInfo() << "[H264][ParamDiag] stream=" << streamTag << "SPS profile_idc=" << profileIdc
              << "level_idc=" << levelIdc << "sps_id=" << spsId << "constraint=0x"
              << QString::number(constraintByte, 16) << spsExtra
              << "| PPS slice_groups 解析失败:" << ppsErr
              << "★ H.264 无 loop_filter_across_slices（HEVC 术语）；见 PPS "
                 "deblocking_filter_control_present_flag 需完整 PPS";
      return;
    }
    (void)br.ue();
    (void)br.ue();
    (void)br.u(1);
    (void)br.u(2);
    (void)br.se();
    (void)br.se();
    (void)br.se();
    deblockCtlPresent = br.u(1);
  }

  qInfo() << "[H264][ParamDiag] stream=" << streamTag << "SPS profile_idc=" << profileIdc
          << "level_idc=" << levelIdc << "sps_id=" << spsId << "constraint=0x"
          << QString::number(constraintByte, 16) << spsExtra << "| PPS pps_id=" << ppsId
          << "pps_sps_id=" << ppsSpsId << "num_slice_groups_minus1=" << numSg
          << "deblocking_filter_control_present_flag=" << deblockCtlPresent
          << "★ H.264 无 loop_filter_across_slices（该位在 HEVC PPS）；"
             "slice 级去块见 ITU-T H.264 slice header disable_deblocking_filter_idc；"
             "CLIENT_VIDEO_SAVE_FRAME=png 对比 PNG 与屏显可区分解码花屏 vs 仅 GPU";
}

}  // namespace H264ClientDiag
