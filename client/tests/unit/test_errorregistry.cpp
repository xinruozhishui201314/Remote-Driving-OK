#include "core/errorregistry.h"

#include <QtTest/QtTest>

class TestErrorRegistry : public QObject {
  Q_OBJECT
 private slots:
  void init();
  void singleton_same_instance();
  void report_and_getErrors();
  void duplicate_aggregates_occurrence();
  void clearErrors_unknown_clears_all();
  void clearErrors_single_category();
  void getErrorSummary_is_json();
  void category_string_roundtrip();
  void level_string_roundtrip();
  void stats_counters();

 private:
  void clearAll() { ErrorRegistry::instance().clearErrors(ErrorRegistry::Category::Unknown); }
};

void TestErrorRegistry::init() { clearAll(); }

void TestErrorRegistry::singleton_same_instance() {
  QVERIFY(&ErrorRegistry::instance() == &ErrorRegistry::instance());
}

void TestErrorRegistry::report_and_getErrors() {
  clearAll();
  const int id =
      ErrorRegistry::instance().report(ErrorRegistry::Category::Network, QStringLiteral("net fail"),
                                       ErrorRegistry::Level::Error, QStringLiteral("WebRTC"));
  QVERIFY(id >= 0);
  auto list = ErrorRegistry::instance().getErrors(ErrorRegistry::Category::Network);
  QCOMPARE(list.size(), 1);
  QCOMPARE(list[0].message, QStringLiteral("net fail"));
  QCOMPARE(list[0].occurrenceCount, 1);
}

void TestErrorRegistry::duplicate_aggregates_occurrence() {
  clearAll();
  ErrorRegistry::instance().report(ErrorRegistry::Category::Control, QStringLiteral("same"),
                                   ErrorRegistry::Level::Warn, QStringLiteral("c"));
  ErrorRegistry::instance().report(ErrorRegistry::Category::Control, QStringLiteral("same"),
                                   ErrorRegistry::Level::Warn, QStringLiteral("c"));
  auto list = ErrorRegistry::instance().getErrors(ErrorRegistry::Category::Control);
  QCOMPARE(list.size(), 1);
  QCOMPARE(list[0].occurrenceCount, 2);
}

void TestErrorRegistry::clearErrors_unknown_clears_all() {
  ErrorRegistry::instance().report(ErrorRegistry::Category::System, QStringLiteral("x"),
                                   ErrorRegistry::Level::Info, QStringLiteral("y"));
  ErrorRegistry::instance().clearErrors(ErrorRegistry::Category::Unknown);
  QVERIFY(ErrorRegistry::instance().getErrors(ErrorRegistry::Category::Unknown).isEmpty());
}

void TestErrorRegistry::clearErrors_single_category() {
  clearAll();
  ErrorRegistry::instance().report(ErrorRegistry::Category::Video, QStringLiteral("v"),
                                   ErrorRegistry::Level::Error, QStringLiteral("c"));
  ErrorRegistry::instance().report(ErrorRegistry::Category::Auth, QStringLiteral("a"),
                                   ErrorRegistry::Level::Error, QStringLiteral("c"));
  ErrorRegistry::instance().clearErrors(ErrorRegistry::Category::Video);
  QVERIFY(ErrorRegistry::instance().getErrors(ErrorRegistry::Category::Video).isEmpty());
  QCOMPARE(ErrorRegistry::instance().getErrors(ErrorRegistry::Category::Auth).size(), 1);
}

void TestErrorRegistry::getErrorSummary_is_json() {
  clearAll();
  ErrorRegistry::instance().report(ErrorRegistry::Category::Session, QStringLiteral("s"),
                                   ErrorRegistry::Level::Warn, QStringLiteral("comp"));
  const QString j = ErrorRegistry::instance().getErrorSummary();
  QVERIFY(j.startsWith('{'));
  QVERIFY(j.contains(QStringLiteral("totalErrors")));
}

void TestErrorRegistry::category_string_roundtrip() {
  using C = ErrorRegistry::Category;
  QCOMPARE(ErrorRegistry::categoryToString(C::Network), QStringLiteral("Network"));
  QCOMPARE(ErrorRegistry::stringToCategory(QStringLiteral("Network")), C::Network);
  QCOMPARE(ErrorRegistry::stringToCategory(QStringLiteral("unknown_xyz")), C::Unknown);
}

void TestErrorRegistry::level_string_roundtrip() {
  using L = ErrorRegistry::Level;
  QCOMPARE(ErrorRegistry::levelToString(L::Fatal), QStringLiteral("Fatal"));
  QCOMPARE(ErrorRegistry::stringToLevel(QStringLiteral("Error")), L::Error);
}

void TestErrorRegistry::stats_counters() {
  clearAll();
  ErrorRegistry::instance().report(ErrorRegistry::Category::Safety, QStringLiteral("f"),
                                   ErrorRegistry::Level::Fatal, QStringLiteral("c"));
  ErrorRegistry::instance().report(ErrorRegistry::Category::Safety, QStringLiteral("e"),
                                   ErrorRegistry::Level::Error, QStringLiteral("c"));
  QVERIFY(ErrorRegistry::instance().fatalErrors() >= 1);
  QVERIFY(ErrorRegistry::instance().errorErrors() >= 1);
  // totalErrorCount 仅在「聚合重复上报」路径递增；新错误条数见列表规模
  QCOMPARE(ErrorRegistry::instance().getErrors(ErrorRegistry::Category::Unknown).size(), 2);
}

QTEST_MAIN(TestErrorRegistry)
#include "test_errorregistry.moc"
