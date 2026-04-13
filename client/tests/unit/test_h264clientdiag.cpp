#include "media/H264ClientDiag.h"

#include <QDir>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QtTest/QtTest>

class TestH264ClientDiag : public QObject {
  Q_OBJECT
 private slots:
  void initTestCase();
  void cleanup();

  void maybeDump_null_image_noop();
  void maybeDump_null_counter_noop();
  void maybeDump_default_env_no_writes();
  void maybeDump_png_writes_file_and_increments_count();
  void maybeDump_respects_max();
  void stripeAlertCapture_writes_png_when_enabled();

  void logParams_env_off_no_crash();
  void logParams_empty_sps_no_crash();
  void logParams_wrong_nal_types_no_crash();
  void logParams_env_on_valid_sps_pps_no_crash();

 private:
  void clearFrameDumpEnv();
  void clearParamDiagEnv();
};

void TestH264ClientDiag::initTestCase() {
  clearFrameDumpEnv();
  clearParamDiagEnv();
}

void TestH264ClientDiag::cleanup() {
  clearFrameDumpEnv();
  clearParamDiagEnv();
}

void TestH264ClientDiag::clearFrameDumpEnv() {
  qunsetenv("CLIENT_VIDEO_SAVE_FRAME");
  qunsetenv("CLIENT_VIDEO_SAVE_FRAME_DIR");
  qunsetenv("CLIENT_VIDEO_SAVE_FRAME_MAX");
  qunsetenv("CLIENT_VIDEO_STRIPE_ALERT_CAPTURE");
  qunsetenv("CLIENT_VIDEO_STRIPE_ALERT_CAPTURE_MAX");
}

void TestH264ClientDiag::clearParamDiagEnv() { qunsetenv("CLIENT_VIDEO_H264_PARAM_DIAG"); }

void TestH264ClientDiag::maybeDump_null_image_noop() {
  clearFrameDumpEnv();
  int n = 0;
  H264ClientDiag::maybeDumpDecodedFrame(QImage(), QStringLiteral("t"), 1ULL, &n);
  QCOMPARE(n, 0);
}

void TestH264ClientDiag::maybeDump_null_counter_noop() {
  clearFrameDumpEnv();
  qputenv("CLIENT_VIDEO_SAVE_FRAME", QByteArrayLiteral("png"));
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  qputenv("CLIENT_VIDEO_SAVE_FRAME_DIR", tmp.path().toUtf8());
  QImage img(2, 2, QImage::Format_RGBA8888);
  img.fill(Qt::red);
  H264ClientDiag::maybeDumpDecodedFrame(img, QStringLiteral("t"), 1ULL, nullptr);
}

void TestH264ClientDiag::maybeDump_default_env_no_writes() {
  clearFrameDumpEnv();
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  qputenv("CLIENT_VIDEO_SAVE_FRAME_DIR", tmp.path().toUtf8());
  QImage img(2, 2, QImage::Format_RGBA8888);
  img.fill(Qt::blue);
  int n = 0;
  H264ClientDiag::maybeDumpDecodedFrame(img, QStringLiteral("x"), 9ULL, &n);
  QCOMPARE(n, 0);
  QDir d(tmp.path());
  QCOMPARE(d.entryList(QDir::Files).size(), 0);
}

void TestH264ClientDiag::maybeDump_png_writes_file_and_increments_count() {
  clearFrameDumpEnv();
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  qputenv("CLIENT_VIDEO_SAVE_FRAME", QByteArrayLiteral("png"));
  qputenv("CLIENT_VIDEO_SAVE_FRAME_DIR", tmp.path().toUtf8());
  QImage img(3, 3, QImage::Format_RGBA8888);
  img.fill(Qt::green);
  int n = 0;
  H264ClientDiag::maybeDumpDecodedFrame(img, QStringLiteral("front:left"), 7ULL, &n);
  QCOMPARE(n, 1);
  QDir d(tmp.path());
  const auto files = d.entryList(QStringList{QStringLiteral("*.png")}, QDir::Files);
  QVERIFY(!files.isEmpty());
  QVERIFY(QFile::exists(d.filePath(files.first())));
}

void TestH264ClientDiag::maybeDump_respects_max() {
  clearFrameDumpEnv();
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  qputenv("CLIENT_VIDEO_SAVE_FRAME", QByteArrayLiteral("png"));
  qputenv("CLIENT_VIDEO_SAVE_FRAME_DIR", tmp.path().toUtf8());
  qputenv("CLIENT_VIDEO_SAVE_FRAME_MAX", QByteArrayLiteral("1"));
  QImage img(2, 2, QImage::Format_RGBA8888);
  img.fill(0);
  int n = 0;
  H264ClientDiag::maybeDumpDecodedFrame(img, QStringLiteral("m"), 1ULL, &n);
  H264ClientDiag::maybeDumpDecodedFrame(img, QStringLiteral("m"), 2ULL, &n);
  QCOMPARE(n, 1);
}

void TestH264ClientDiag::stripeAlertCapture_writes_png_when_enabled() {
  clearFrameDumpEnv();
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  qputenv("CLIENT_VIDEO_SAVE_FRAME_DIR", tmp.path().toUtf8());
  qputenv("CLIENT_VIDEO_STRIPE_ALERT_CAPTURE", QByteArrayLiteral("1"));
  qputenv("CLIENT_VIDEO_STRIPE_ALERT_CAPTURE_MAX", QByteArrayLiteral("2"));
  QImage img(16, 16, QImage::Format_RGBA8888);
  img.fill(Qt::cyan);
  H264ClientDiag::maybeDumpStripeAlertCapture(img, QStringLiteral("cam_front"), 42ULL,
                                              QStringLiteral("suspect"), 0, 100, 0, 0);
  const QDir alertDir(tmp.path() + QStringLiteral("/stripe-alerts"));
  QVERIFY(alertDir.exists());
  const auto pngs = alertDir.entryList(QStringList{QStringLiteral("*.png")}, QDir::Files);
  QCOMPARE(pngs.size(), 1);
  QVERIFY(pngs.first().contains(QStringLiteral("_f42_")));
  QVERIFY(pngs.first().contains(QStringLiteral("suspect")));
}

void TestH264ClientDiag::logParams_env_off_no_crash() {
  clearParamDiagEnv();
  H264ClientDiag::logParameterSetsIfRequested(QStringLiteral("s"), QByteArray("invalid"),
                                                QByteArray("invalid"));
}

void TestH264ClientDiag::logParams_empty_sps_no_crash() {
  clearParamDiagEnv();
  qputenv("CLIENT_VIDEO_H264_PARAM_DIAG", QByteArrayLiteral("1"));
  H264ClientDiag::logParameterSetsIfRequested(QStringLiteral("s"), QByteArray(),
                                                QByteArray::fromHex("68CE3C80"));
}

void TestH264ClientDiag::logParams_wrong_nal_types_no_crash() {
  clearParamDiagEnv();
  qputenv("CLIENT_VIDEO_H264_PARAM_DIAG", QByteArrayLiteral("1"));
  H264ClientDiag::logParameterSetsIfRequested(QStringLiteral("s"), QByteArray(1, char(0x01)),
                                                QByteArray(1, char(0x02)));
}

void TestH264ClientDiag::logParams_env_on_valid_sps_pps_no_crash() {
  clearParamDiagEnv();
  qputenv("CLIENT_VIDEO_H264_PARAM_DIAG", QByteArrayLiteral("1"));
  // Annex-B style NAL units (with NAL header byte); RBSP sufficient for ParamDiag bit reader
  const QByteArray sps =
      QByteArray::fromHex("6742E01F96A6840F84496243508080801E30D41802185A560C04600");
  const QByteArray pps = QByteArray::fromHex("68CE3C80");
  H264ClientDiag::logParameterSetsIfRequested(QStringLiteral("unit"), sps, pps);
}

QTEST_MAIN(TestH264ClientDiag)
#include "test_h264clientdiag.moc"
