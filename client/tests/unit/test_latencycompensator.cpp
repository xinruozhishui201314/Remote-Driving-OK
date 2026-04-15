#include "infrastructure/hardware/IInputDevice.h"
#include "services/latencycompensator.h"

#include <QtTest/QtTest>

class TestLatencyCompensator : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY(TestLatencyCompensator)
 public:
  explicit TestLatencyCompensator(QObject* parent = nullptr) : QObject(parent) {}
 private slots:
  void few_samples_returns_low_confidence_and_current_steering();
  void reset_clears_history();
  void prediction_clamped_to_max_delta();
};

void TestLatencyCompensator::few_samples_returns_low_confidence_and_current_steering() {
  LatencyCompensator c(5.0, 1.0);
  IInputDevice::InputState st{};
  st.steeringAngle = 0.1;
  auto r = c.predict(st, 20.0);
  QCOMPARE(r.predictedSteeringAngle, 0.1);
  QVERIFY(r.confidence <= 0.35);

  st.steeringAngle = 0.2;
  r = c.predict(st, 20.0);
  st.steeringAngle = 0.3;
  r = c.predict(st, 20.0);
  QVERIFY(r.predictionHorizonMs > 0.0);
}

void TestLatencyCompensator::reset_clears_history() {
  LatencyCompensator c(5.0, 1.0);
  IInputDevice::InputState st{};
  st.steeringAngle = 0.5;
  c.predict(st, 10.0);
  c.predict(st, 10.0);
  c.reset();
  auto r = c.predict(st, 10.0);
  QCOMPARE(r.predictedSteeringAngle, 0.5);
}

void TestLatencyCompensator::prediction_clamped_to_max_delta() {
  LatencyCompensator c(0.0, 0.05);  // max ±0.05 rad from current
  IInputDevice::InputState st{};
  // Build steep ramp so quadratic extrapolation wants large jump
  for (int i = 0; i < 8; ++i) {
    st.steeringAngle = static_cast<double>(i) * 0.2;
    c.predict(st, 80.0);
  }
  st.steeringAngle = 1.0;
  auto r = c.predict(st, 200.0);
  QVERIFY(r.predictedSteeringAngle <= st.steeringAngle + 0.051);
  QVERIFY(r.predictedSteeringAngle >= st.steeringAngle - 0.051);
}

QTEST_MAIN(TestLatencyCompensator)
#include "test_latencycompensator.moc"
