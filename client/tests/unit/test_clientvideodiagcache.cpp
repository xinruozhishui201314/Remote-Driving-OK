#include "media/ClientVideoDiagCache.h"

#include <QtTest/QtTest>

class TestClientVideoDiagCache : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY(TestClientVideoDiagCache)
 public:
  explicit TestClientVideoDiagCache(QObject* parent = nullptr) : QObject(parent), m_savedRhi() {}
 private slots:
  void init();
  void cleanup();

  void set_empty_preserves_previous_wm();
  void set_and_read_wm_name();
  void renderStackSummary_null_window();
  void renderStackSummary_includes_rhi_env();

 private:
  QByteArray m_savedRhi;
};

void TestClientVideoDiagCache::init() {
  m_savedRhi = qgetenv("QSG_RHI_BACKEND");
}

void TestClientVideoDiagCache::cleanup() {
  if (m_savedRhi.isNull())
    qunsetenv("QSG_RHI_BACKEND");
  else
    qputenv("QSG_RHI_BACKEND", m_savedRhi);
}

void TestClientVideoDiagCache::set_empty_preserves_previous_wm() {
  ClientVideoDiagCache::setX11NetWmName(QStringLiteral("PriorWm"));
  ClientVideoDiagCache::setX11NetWmName(QString());
  QCOMPARE(ClientVideoDiagCache::x11NetWmName(), QStringLiteral("PriorWm"));
}

void TestClientVideoDiagCache::set_and_read_wm_name() {
  ClientVideoDiagCache::setX11NetWmName(QStringLiteral("KWin"));
  QCOMPARE(ClientVideoDiagCache::x11NetWmName(), QStringLiteral("KWin"));
}

void TestClientVideoDiagCache::renderStackSummary_null_window() {
  ClientVideoDiagCache::setX11NetWmName(QStringLiteral("mutter"));
  const QString line = ClientVideoDiagCache::renderStackSummaryLine(nullptr);
  QVERIFY(line.contains(QStringLiteral("graphicsApi=-1")));
  QVERIFY(line.contains(QStringLiteral("x11NetWmName=\"mutter\"")));
}

void TestClientVideoDiagCache::renderStackSummary_includes_rhi_env() {
  qputenv("QSG_RHI_BACKEND", QByteArrayLiteral("vulkan"));
  ClientVideoDiagCache::setX11NetWmName(QStringLiteral("TestComp"));
  const QString line = ClientVideoDiagCache::renderStackSummaryLine(nullptr);
  QVERIFY(line.contains(QStringLiteral("QSG_RHI_BACKEND=\"vulkan\"")));
  QVERIFY(line.contains(QStringLiteral("x11NetWmName=\"TestComp\"")));
}

QTEST_MAIN(TestClientVideoDiagCache)
#include "test_clientvideodiagcache.moc"
