#include "core/performancemonitor.h"
#include "services/diagnosticsservice.h"

#include <QMetaObject>
#include <QSignalSpy>
#include <QtTest/QtTest>

class TestDiagnosticsService : public QObject {
  Q_OBJECT

 private slots:
  void buildSnapshot_withoutPerf_hasCoreFields() {
    DiagnosticsService svc(nullptr);
    const QJsonObject o = svc.buildSnapshot();
    QCOMPARE(o[QStringLiteral("format_version")].toInt(), 1);
    QVERIFY(o.contains(QStringLiteral("timestamp")));
    QVERIFY(o.contains(QStringLiteral("system")));
    QVERIFY(!o.contains(QStringLiteral("latency")));
  }

  void buildSnapshot_withPerf_includesLatencyThroughputQuality() {
    PerformanceMonitor perf;
    perf.recordVideoE2E(50'000);
    perf.recordControlRTT(100'000);
    DiagnosticsService svc(&perf);
    const QJsonObject o = svc.buildSnapshot();
    QVERIFY(o.contains(QStringLiteral("latency")));
    QVERIFY(o.contains(QStringLiteral("throughput")));
    QVERIFY(o.contains(QStringLiteral("quality")));
    const QJsonObject lat = o[QStringLiteral("latency")].toObject();
    QVERIFY(lat.contains(QStringLiteral("video_e2e_ms")));
    QVERIFY(lat[QStringLiteral("video_e2e_ms")].toDouble() > 0.0);
  }

  void collect_emitsDiagnosticsAvailable() {
    PerformanceMonitor perf;
    DiagnosticsService svc(&perf);
    QSignalSpy spy(&svc, &DiagnosticsService::diagnosticsAvailable);
    QMetaObject::invokeMethod(&svc, "collect", Qt::DirectConnection);
    QCOMPARE(spy.count(), 1);
    QVERIFY(spy.at(0).at(0).toJsonObject().contains(QStringLiteral("timestamp")));
  }
};

QTEST_MAIN(TestDiagnosticsService)
#include "test_diagnosticsservice.moc"
