#include "media/VideoInterlacedPolicy.h"

#include <QByteArray>
#include <QtTest/QtTest>

class TestVideoInterlacedPolicy : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY(TestVideoInterlacedPolicy)
 public:
  explicit TestVideoInterlacedPolicy(QObject* parent = nullptr) : QObject(parent), m_savedIlace() {}
 private slots:
  void init() {
    m_savedIlace = qgetenv("CLIENT_VIDEO_INTERLACED_POLICY");
  }
  void cleanup() {
    if (m_savedIlace.isNull())
      qunsetenv("CLIENT_VIDEO_INTERLACED_POLICY");
    else
      qputenv("CLIENT_VIDEO_INTERLACED_POLICY", m_savedIlace);
  }

  void default_env_is_blend() {
    qunsetenv("CLIENT_VIDEO_INTERLACED_POLICY");
    QCOMPARE(VideoInterlacedPolicy::diagnosticsTag(), QStringLiteral("blend"));
    QCOMPARE(static_cast<int>(VideoInterlacedPolicy::currentFromEnv()),
             static_cast<int>(VideoInterlacedPolicy::Policy::BlendLines));
  }

  void off_env() {
    qputenv("CLIENT_VIDEO_INTERLACED_POLICY", QByteArrayLiteral("off"));
    QCOMPARE(VideoInterlacedPolicy::diagnosticsTag(), QStringLiteral("off"));
  }

  void warn_only_env() {
    qputenv("CLIENT_VIDEO_INTERLACED_POLICY", QByteArrayLiteral("warn_only"));
    QCOMPARE(VideoInterlacedPolicy::diagnosticsTag(), QStringLiteral("warn"));
  }

 private:
  QByteArray m_savedIlace;
};

QTEST_MAIN(TestVideoInterlacedPolicy)
#include "test_video_interlaced_policy.moc"
