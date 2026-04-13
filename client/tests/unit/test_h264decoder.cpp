#include "h264decoder.h"
#include "media/ClientVideoStreamHealth.h"

#include <QByteArray>
#include <QImage>
#include <QSignalSpy>
#include <QtTest/QtTest>

#include <cstring>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/error.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

namespace {

QByteArray makeRtp(quint16 seq, quint32 ts, quint32 ssrc, uint8_t byte0, uint8_t byte1,
                   const QByteArray &payload = {}) {
  QByteArray p(12 + payload.size(), Qt::Uninitialized);
  p[0] = static_cast<char>(byte0);
  p[1] = static_cast<char>(byte1);
  p[2] = static_cast<char>((seq >> 8) & 0xff);
  p[3] = static_cast<char>(seq & 0xff);
  p[4] = static_cast<char>((ts >> 24) & 0xff);
  p[5] = static_cast<char>((ts >> 16) & 0xff);
  p[6] = static_cast<char>((ts >> 8) & 0xff);
  p[7] = static_cast<char>(ts & 0xff);
  p[8] = static_cast<char>((ssrc >> 24) & 0xff);
  p[9] = static_cast<char>((ssrc >> 16) & 0xff);
  p[10] = static_cast<char>((ssrc >> 8) & 0xff);
  p[11] = static_cast<char>(ssrc & 0xff);
  if (!payload.isEmpty())
    memcpy(p.data() + 12, payload.constData(), static_cast<size_t>(payload.size()));
  return p;
}

/** V=2, PT=96, minimal 12-byte header */
QByteArray makeH264Rtp(quint16 seq, quint32 ts, quint32 ssrc, const QByteArray &payload = {}) {
  return makeRtp(seq, ts, ssrc, 0x80, 0x60, payload);
}

QByteArray makeH264RtpMarker(quint16 seq, quint32 ts, quint32 ssrc, bool marker,
                             const QByteArray &payload = {}) {
  const auto b1 = static_cast<uint8_t>((marker ? 0x80 : 0) | 0x60);
  return makeRtp(seq, ts, ssrc, 0x80, static_cast<uint8_t>(b1), payload);
}

/** 下一个 Annex B 起始码位置；找不到返回 len。 */
int nextAnnexBStart(const uint8_t *d, int len, int pos) {
  for (int i = pos; i + 3 <= len; ++i) {
    if (d[i] == 0 && d[i + 1] == 0) {
      if (d[i + 2] == 1)
        return i;
      if (i + 4 <= len && d[i + 2] == 0 && d[i + 3] == 1)
        return i;
    }
  }
  return len;
}

/** 将 Annex B 码流拆成 NAL（不含起始码）。 */
QVector<QByteArray> splitAnnexB(const uint8_t *d, int len) {
  QVector<QByteArray> nals;
  int pos = 0;
  while (pos < len) {
    const int sc = nextAnnexBStart(d, len, pos);
    if (sc >= len)
      break;
    int scLen = 3;
    if (sc + 3 < len && d[sc + 2] == 0 && d[sc + 3] == 1)
      scLen = 4;
    else if (sc + 2 < len && d[sc + 2] == 1)
      scLen = 3;
    else
      break;
    const int nalStart = sc + scLen;
    const int next = nextAnnexBStart(d, len, nalStart);
    if (next > nalStart)
      nals.append(QByteArray(reinterpret_cast<const char *>(d + nalStart), next - nalStart));
    pos = next;
  }
  return nals;
}

/**
 * 将 RGB888 图像编码为单帧 Baseline H.264（Annex B，含 SPS/PPS+IDR），用于模拟车端送入 FFmpeg 前的像素
 * 与客户端 H264Decoder 之间的「离线闭环」。需运行时存在 libx264。
 */
QByteArray encodeRgbToAnnexBH264IdrFrame(const QImage &rgbIn) {
  QImage rgb = rgbIn;
  if (rgb.format() != QImage::Format_RGB888)
    rgb = rgb.convertToFormat(QImage::Format_RGB888);
  const int w = rgb.width();
  const int h = rgb.height();
  if (w < 2 || h < 2 || (w & 1) || (h & 1))
    return {};

  const AVCodec *codec = avcodec_find_encoder_by_name("libx264");
  if (!codec)
    return {};

  AVCodecContext *ctx = avcodec_alloc_context3(codec);
  if (!ctx)
    return {};
  ctx->width = w;
  ctx->height = h;
  ctx->time_base = AVRational{1, 30};
  ctx->framerate = AVRational{30, 1};
  ctx->pix_fmt = AV_PIX_FMT_YUV420P;
  ctx->gop_size = 1;
  ctx->max_b_frames = 0;
  ctx->profile = FF_PROFILE_H264_BASELINE;
  ctx->level = 30;
  av_opt_set(ctx->priv_data, "preset", "ultrafast", 0);
  av_opt_set(ctx->priv_data, "tune", "zerolatency", 0);

  if (avcodec_open2(ctx, codec, nullptr) < 0) {
    avcodec_free_context(&ctx);
    return {};
  }

  AVFrame *frame = av_frame_alloc();
  frame->format = ctx->pix_fmt;
  frame->width = w;
  frame->height = h;
  if (av_frame_get_buffer(frame, 0) < 0) {
    av_frame_free(&frame);
    avcodec_free_context(&ctx);
    return {};
  }

  SwsContext *sws = sws_getContext(w, h, AV_PIX_FMT_RGB24, w, h, AV_PIX_FMT_YUV420P, SWS_BILINEAR,
                                   nullptr, nullptr, nullptr);
  if (!sws) {
    av_frame_free(&frame);
    avcodec_free_context(&ctx);
    return {};
  }
  uint8_t *srcSlice[4] = {rgb.bits(), nullptr, nullptr, nullptr};
  int srcStride[4] = {rgb.bytesPerLine(), 0, 0, 0};
  sws_scale(sws, srcSlice, srcStride, 0, h, frame->data, frame->linesize);
  sws_freeContext(sws);

  frame->pts = 0;
  if (avcodec_send_frame(ctx, frame) < 0) {
    av_frame_free(&frame);
    avcodec_free_context(&ctx);
    return {};
  }
  av_frame_free(&frame);

  AVPacket *pkt = av_packet_alloc();
  QByteArray acc;
  for (;;) {
    const int ret = avcodec_receive_packet(ctx, pkt);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
      break;
    if (ret < 0)
      break;
    acc.append(reinterpret_cast<const char *>(pkt->data), pkt->size);
    av_packet_unref(pkt);
  }
  av_packet_free(&pkt);
  avcodec_free_context(&ctx);
  return acc;
}

/** 与车端 pre_encode PPM 一致：P6 全帧可经 QImage 加载（RGB 顺序）。 */
QImage sourceImageForPreEncodeClientTest() {
  const QByteArray ppmPath = qgetenv("TELEOP_PRE_ENCODE_TEST_PPM");
  if (!ppmPath.isEmpty()) {
    QImage loaded;
    if (loaded.load(QString::fromLocal8Bit(ppmPath)))
      return loaded.convertToFormat(QImage::Format_RGB888);
  }
  QImage img(64, 48, QImage::Format_RGB888);
  img.fill(Qt::black);
  for (int y = 0; y < img.height(); ++y) {
    auto *line = img.bits() + y * img.bytesPerLine();
    for (int x = 0; x < img.width() / 2; ++x) {
      line[x * 3 + 0] = 220;
      line[x * 3 + 1] = 40;
      line[x * 3 + 2] = 40;
    }
    for (int x = img.width() / 2; x < img.width(); ++x) {
      line[x * 3 + 0] = 60;
      line[x * 3 + 1] = 60;
      line[x * 3 + 2] = 240;
    }
  }
  return img;
}

bool colorClose(QRgb a, QRgb b, int tol) {
  return qAbs(qRed(a) - qRed(b)) <= tol && qAbs(qGreen(a) - qGreen(b)) <= tol &&
         qAbs(qBlue(a) - qBlue(b)) <= tol;
}

}  // namespace

class TestH264Decoder : public QObject {
  Q_OBJECT
 private slots:
  void init();
  void cleanup();

  void takeAndReset_emit_diag_starts_zero();
  void feedRtp_updates_lifecycle_even_on_short_packet();
  void feedRtp_rejects_too_short();
  void feedRtp_rejects_bad_version();
  void feedRtp_rejects_wrong_payload_type();
  void reset_clears_decoder_state();
  void drainIngress_without_queue_emits_finished();
  void rtp_with_extension_header_no_crash();
  void video_stream_health_dma_effective_respects_software_gl();
  void video_stream_health_force_single_thread_under_software_gl();

  /**
   * 用与 pre_encode 同类的 RGB 源图 → libx264 Annex B → RTP(H264 PT96) → H264Decoder，验证客户端解码出画。
   * 可选：TELEOP_PRE_ENCODE_TEST_PPM 指向车端落盘的 P6 PPM，做「真 pre_encode 文件」回归。
   */
  void pre_encode_style_rgb_roundtrip_client_decode_path();

 private:
  QByteArray m_savedPt;
};

void TestH264Decoder::init() { m_savedPt = qgetenv("CLIENT_H264_RTP_PAYLOAD_TYPE"); }

void TestH264Decoder::cleanup() {
  if (m_savedPt.isNull())
    qunsetenv("CLIENT_H264_RTP_PAYLOAD_TYPE");
  else
    qputenv("CLIENT_H264_RTP_PAYLOAD_TYPE", m_savedPt);
}

void TestH264Decoder::takeAndReset_emit_diag_starts_zero() {
  H264Decoder dec(QStringLiteral("unit"));
  QCOMPARE(dec.takeAndResetFrameReadyEmitDiagCount(), 0);
}

void TestH264Decoder::feedRtp_updates_lifecycle_even_on_short_packet() {
  H264Decoder dec(QStringLiteral("unit"));
  const quint8 buf[8] = {0};
  dec.feedRtp(buf, sizeof buf, 12345ULL);
  QCOMPARE(dec.currentLifecycleId(), 12345ULL);
}

void TestH264Decoder::feedRtp_rejects_too_short() {
  H264Decoder dec(QStringLiteral("unit"));
  const quint8 buf[11] = {0};
  dec.feedRtp(buf, sizeof buf, 0);
  QCOMPARE(dec.receiverCountFrameReady(), 0);
}

void TestH264Decoder::feedRtp_rejects_bad_version() {
  H264Decoder dec(QStringLiteral("unit"));
  QByteArray p = makeH264Rtp(1, 1000, 0x11223344);
  p[0] = static_cast<char>(0x00);
  dec.feedRtp(reinterpret_cast<const uint8_t *>(p.constData()), static_cast<size_t>(p.size()), 0);
  QCOMPARE(dec.receiverCountFrameReady(), 0);
}

void TestH264Decoder::feedRtp_rejects_wrong_payload_type() {
  H264Decoder dec(QStringLiteral("unit"));
  QByteArray p = makeRtp(1, 1000, 0xAABBCCDD, 0x80, 0xC8);
  dec.feedRtp(reinterpret_cast<const uint8_t *>(p.constData()), static_cast<size_t>(p.size()), 0);
  QCOMPARE(dec.receiverCountFrameReady(), 0);
}

void TestH264Decoder::reset_clears_decoder_state() {
  H264Decoder dec(QStringLiteral("unit"));
  dec.reset();
  QCOMPARE(dec.takeAndResetFrameReadyEmitDiagCount(), 0);
  QCOMPARE(dec.receiverCountFrameReady(), 0);
}

void TestH264Decoder::drainIngress_without_queue_emits_finished() {
  H264Decoder dec(QStringLiteral("unit"));
  QSignalSpy spy(&dec, &H264Decoder::ingressDrainFinished);
  dec.drainRtpIngressQueue();
  QCOMPARE(spy.count(), 1);
  QCOMPARE(spy.at(0).at(0).toBool(), false);
}

void TestH264Decoder::rtp_with_extension_header_no_crash() {
  H264Decoder dec(QStringLiteral("unit-ext"));
  QByteArray p(18, '\0');
  p[0] = static_cast<char>(0x90);
  p[1] = static_cast<char>(0x60);
  const quint16 seq = 3;
  const quint32 ts = 4000, ssrc = 0x99AABBCC;
  p[2] = static_cast<char>((seq >> 8) & 0xff);
  p[3] = static_cast<char>(seq & 0xff);
  p[4] = static_cast<char>((ts >> 24) & 0xff);
  p[5] = static_cast<char>((ts >> 16) & 0xff);
  p[6] = static_cast<char>((ts >> 8) & 0xff);
  p[7] = static_cast<char>(ts & 0xff);
  p[8] = static_cast<char>((ssrc >> 24) & 0xff);
  p[9] = static_cast<char>((ssrc >> 16) & 0xff);
  p[10] = static_cast<char>((ssrc >> 8) & 0xff);
  p[11] = static_cast<char>(ssrc & 0xff);
  p[12] = 0;
  p[13] = 0;
  p[14] = 0;
  p[15] = 0;
  p[16] = static_cast<char>(0x09);
  p[17] = static_cast<char>(0xF0);
  dec.feedRtp(reinterpret_cast<const uint8_t *>(p.constData()), static_cast<size_t>(p.size()), 0);
}

namespace {

void restoreEnvVar(const char *key, const QByteArray &saved) {
  if (saved.isNull())
    qunsetenv(key);
  else
    qputenv(key, saved);
}

}  // namespace

void TestH264Decoder::video_stream_health_dma_effective_respects_software_gl() {
  const QByteArray lib = qgetenv("LIBGL_ALWAYS_SOFTWARE");
  const QByteArray allow = qgetenv("CLIENT_VIDEO_ALLOW_DMABUF_UNDER_SOFTWARE_GL");
  const QByteArray exp = qgetenv("CLIENT_WEBRTC_HW_EXPORT_DMABUF");
  qputenv("LIBGL_ALWAYS_SOFTWARE", "1");
  qputenv("CLIENT_WEBRTC_HW_EXPORT_DMABUF", "1");
  qunsetenv("CLIENT_VIDEO_ALLOW_DMABUF_UNDER_SOFTWARE_GL");
  ClientVideoStreamHealth::debugResetPolicyCacheForTest();
  QVERIFY(!ClientVideoStreamHealth::effectivePreferDmaBufExport());
  qputenv("CLIENT_VIDEO_ALLOW_DMABUF_UNDER_SOFTWARE_GL", "1");
  QVERIFY(ClientVideoStreamHealth::effectivePreferDmaBufExport());
  restoreEnvVar("LIBGL_ALWAYS_SOFTWARE", lib);
  restoreEnvVar("CLIENT_VIDEO_ALLOW_DMABUF_UNDER_SOFTWARE_GL", allow);
  restoreEnvVar("CLIENT_WEBRTC_HW_EXPORT_DMABUF", exp);
  ClientVideoStreamHealth::debugResetPolicyCacheForTest();
}

void TestH264Decoder::video_stream_health_force_single_thread_under_software_gl() {
  const QByteArray lib = qgetenv("LIBGL_ALWAYS_SOFTWARE");
  const QByteArray mult = qgetenv("CLIENT_VIDEO_ALLOW_MULTITHREAD_DECODE_UNDER_SOFTWARE_GL");
  qputenv("LIBGL_ALWAYS_SOFTWARE", "1");
  qunsetenv("CLIENT_VIDEO_ALLOW_MULTITHREAD_DECODE_UNDER_SOFTWARE_GL");
  QVERIFY(ClientVideoStreamHealth::shouldForceSingleThreadDecodeUnderSoftwareRaster(4));
  QVERIFY(!ClientVideoStreamHealth::shouldForceSingleThreadDecodeUnderSoftwareRaster(1));
  qputenv("CLIENT_VIDEO_ALLOW_MULTITHREAD_DECODE_UNDER_SOFTWARE_GL", "1");
  QVERIFY(!ClientVideoStreamHealth::shouldForceSingleThreadDecodeUnderSoftwareRaster(4));
  restoreEnvVar("LIBGL_ALWAYS_SOFTWARE", lib);
  restoreEnvVar("CLIENT_VIDEO_ALLOW_MULTITHREAD_DECODE_UNDER_SOFTWARE_GL", mult);
  ClientVideoStreamHealth::debugResetPolicyCacheForTest();
}

void TestH264Decoder::pre_encode_style_rgb_roundtrip_client_decode_path() {
  QImage src = sourceImageForPreEncodeClientTest();
  QVERIFY2(!src.isNull() && src.width() >= 2 && src.height() >= 2, "source image");
  if ((src.width() & 1) || (src.height() & 1))
    src = src.copy(0, 0, src.width() & ~1, src.height() & ~1);

  const QByteArray annexB = encodeRgbToAnnexBH264IdrFrame(src);
  if (annexB.isEmpty())
    QSKIP("libx264 encoder unavailable or encode failed (install ffmpeg with libx264 for this test)");

  const QVector<QByteArray> nals =
      splitAnnexB(reinterpret_cast<const uint8_t *>(annexB.constData()),
                  static_cast<int>(annexB.size()));
  QVERIFY2(!nals.isEmpty(), "split Annex B NALs");

  int lastVcl = -1;
  for (int i = 0; i < nals.size(); ++i) {
    if (nals[i].isEmpty())
      continue;
    const int t = static_cast<uint8_t>(nals[i][0]) & 0x1f;
    if (t == 1 || t == 5)
      lastVcl = i;
  }
  QVERIFY2(lastVcl >= 0, "expected at least one VCL NAL in IDR frame");

  H264Decoder dec(QStringLiteral("pre_encode_roundtrip"));
  QSignalSpy spy(&dec, &H264Decoder::frameReady);

  const quint32 ssrc = 0xCAFEBABEu;
  const quint32 rtpTs = 3000;
  quint16 seq = 1;
  for (int i = 0; i < nals.size(); ++i) {
    const bool marker = (i == lastVcl);
    const QByteArray rtp = makeH264RtpMarker(seq++, rtpTs, ssrc, marker, nals[i]);
    dec.feedRtp(reinterpret_cast<const uint8_t *>(rtp.constData()), static_cast<size_t>(rtp.size()),
                0);
  }

  if (spy.count() == 0)
    QVERIFY2(spy.wait(8000), "frameReady not received within timeout");
  QCOMPARE(spy.count(), 1);
  const QImage out = spy.at(0).at(0).value<QImage>();
  QVERIFY(!out.isNull());
  QCOMPARE(out.width(), src.width());
  QCOMPARE(out.height(), src.height());

  const QImage srcRgb = src.convertToFormat(QImage::Format_ARGB32);
  const QImage outRgb = out.convertToFormat(QImage::Format_ARGB32);
  const int tol = 48;
  QVERIFY2(colorClose(srcRgb.pixel(4, 4), outRgb.pixel(4, 4), tol),
            "left region color diverges (lossy codec — large tolerance)");
  QVERIFY2(
      colorClose(srcRgb.pixel(srcRgb.width() - 5, 4), outRgb.pixel(outRgb.width() - 5, 4), tol),
      "right region color diverges");
}

QTEST_MAIN(TestH264Decoder)
#include "test_h264decoder.moc"
