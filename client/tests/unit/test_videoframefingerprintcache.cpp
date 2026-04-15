#include "media/VideoFrameFingerprintCache.h"

#include <QtTest/QtTest>

class TestVideoFrameFingerprintCache : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY(TestVideoFrameFingerprintCache)
 public:
  explicit TestVideoFrameFingerprintCache(QObject* parent = nullptr) : QObject(parent) {}
 private slots:
  void empty_stream_tag_record_is_ignored();
  void empty_stream_tag_peek_fails();
  void record_peek_roundtrip();
  void clearStream_removes_only_matching_prefix();
  void trim_keeps_size_bounded();
};

void TestVideoFrameFingerprintCache::empty_stream_tag_record_is_ignored() {
  VideoFrameFingerprintCache &c = VideoFrameFingerprintCache::instance();
  VideoFrameFingerprintCache::Fingerprint fp;
  fp.rowHash = 1;
  fp.fullCrc = 2;
  fp.width = 3;
  fp.height = 4;
  c.record(QString(), 99ULL, fp);
  VideoFrameFingerprintCache::Fingerprint out;
  QVERIFY(!c.peek(QString(), 99ULL, &out));
}

void TestVideoFrameFingerprintCache::empty_stream_tag_peek_fails() {
  VideoFrameFingerprintCache &c = VideoFrameFingerprintCache::instance();
  VideoFrameFingerprintCache::Fingerprint out;
  QVERIFY(!c.peek(QString(), 1ULL, &out));
}

void TestVideoFrameFingerprintCache::record_peek_roundtrip() {
  VideoFrameFingerprintCache &c = VideoFrameFingerprintCache::instance();
  const QString tag = QStringLiteral("cam_front");
  c.clearStream(tag);
  VideoFrameFingerprintCache::Fingerprint fp;
  fp.rowHash = 0xAABBCCDD;
  fp.fullCrc = 0;
  fp.width = 1920;
  fp.height = 1080;
  const quint64 fid = 42ULL;
  c.record(tag, fid, fp);
  VideoFrameFingerprintCache::Fingerprint out;
  QVERIFY(c.peek(tag, fid, &out));
  QCOMPARE(out.rowHash, fp.rowHash);
  QCOMPARE(out.fullCrc, fp.fullCrc);
  QCOMPARE(out.width, fp.width);
  QCOMPARE(out.height, fp.height);
  c.clearStream(tag);
}

void TestVideoFrameFingerprintCache::clearStream_removes_only_matching_prefix() {
  VideoFrameFingerprintCache &c = VideoFrameFingerprintCache::instance();
  c.clearStream(QStringLiteral("a"));
  c.clearStream(QStringLiteral("ab"));
  VideoFrameFingerprintCache::Fingerprint fp;
  fp.rowHash = 1;
  c.record(QStringLiteral("a"), 1ULL, fp);
  c.record(QStringLiteral("ab"), 1ULL, fp);
  c.record(QStringLiteral("abc"), 1ULL, fp);
  c.clearStream(QStringLiteral("ab"));
  VideoFrameFingerprintCache::Fingerprint out;
  QVERIFY(c.peek(QStringLiteral("a"), 1ULL, &out));
  QVERIFY(!c.peek(QStringLiteral("ab"), 1ULL, &out));
  QVERIFY(!c.peek(QStringLiteral("abc"), 1ULL, &out));
  c.clearStream(QStringLiteral("a"));
}

void TestVideoFrameFingerprintCache::trim_keeps_size_bounded() {
  VideoFrameFingerprintCache &c = VideoFrameFingerprintCache::instance();
  const QString tag = QStringLiteral("trim_probe");
  c.clearStream(tag);
  VideoFrameFingerprintCache::Fingerprint fp;
  fp.rowHash = 0;
  constexpr int kOver = 520;
  for (int i = 0; i < kOver; ++i) {
    fp.rowHash = static_cast<quint32>(i);
    c.record(tag, static_cast<quint64>(i), fp);
  }
  int found = 0;
  for (int i = 0; i < kOver; ++i) {
    VideoFrameFingerprintCache::Fingerprint out;
    if (c.peek(tag, static_cast<quint64>(i), &out))
      ++found;
  }
  QVERIFY2(found <= 512 && found >= 500,
            "after trim, map keeps at most 512 entries; most recent ids should still resolve");
  c.clearStream(tag);
}

QTEST_MAIN(TestVideoFrameFingerprintCache)
#include "test_videoframefingerprintcache.moc"
