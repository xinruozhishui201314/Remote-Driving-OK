#include "InputSampler.h"

#include <QDebug>

#include <cmath>

InputSampler::InputSampler(QObject* parent) : QObject(parent) {}

InputSampler::~InputSampler() { stop(); }

void InputSampler::setDevice(std::shared_ptr<IInputDevice> device) { m_device = std::move(device); }

void InputSampler::start(uint32_t sampleRateHz) {
  if (m_samplerThread)
    stop();

  m_samplerThread = new QThread(this);
  m_samplerThread->setObjectName("InputSampler");
  m_samplerThread->setPriority(QThread::HighestPriority);

  m_timer = new QTimer();
  m_timer->setTimerType(Qt::PreciseTimer);
  m_timer->setInterval(static_cast<int>(1000 / sampleRateHz));
  m_timer->moveToThread(m_samplerThread);

  connect(m_timer, &QTimer::timeout, this, &InputSampler::onTimer, Qt::DirectConnection);
  connect(m_samplerThread, &QThread::started, m_timer, QOverload<>::of(&QTimer::start));

  moveToThread(m_samplerThread);
  m_samplerThread->start();

  qInfo() << "[Client][InputSampler] started at" << sampleRateHz << "Hz";
}

void InputSampler::stop() {
  if (!m_samplerThread)
    return;
  if (m_timer) {
    QMetaObject::invokeMethod(m_timer, &QTimer::stop, Qt::QueuedConnection);
  }
  m_samplerThread->quit();
  m_samplerThread->wait(2000);
  delete m_timer;
  m_timer = nullptr;
  delete m_samplerThread;
  m_samplerThread = nullptr;
  qInfo() << "[Client][InputSampler] stopped";
}

void InputSampler::onTimer() {
  if (!m_device)
    return;

  IInputDevice::InputState raw = m_device->poll();
  raw.timestamp = TimeUtils::nowUs();

  auto filtered = applyFilter(raw);
  m_latestInput.store(filtered);
  emit inputSampled(filtered);
}

IInputDevice::InputState InputSampler::applyFilter(const IInputDevice::InputState& raw) {
  IInputDevice::InputState out = raw;

  // Dead zones
  out.steeringAngle = applyDeadzone(raw.steeringAngle, m_filterCfg.steeringDeadzone);
  out.throttle = applyDeadzone(raw.throttle, m_filterCfg.throttleDeadzone);
  out.brake = applyDeadzone(raw.brake, m_filterCfg.brakeDeadzone);

  // Exponential smoothing
  const double s = m_filterCfg.smoothingFactor;
  out.steeringAngle = applySmoothing(m_previousFiltered.steeringAngle, out.steeringAngle, s);
  out.throttle = applySmoothing(m_previousFiltered.throttle, out.throttle, s);
  out.brake = applySmoothing(m_previousFiltered.brake, out.brake, s);

  m_previousFiltered = out;
  return out;
}

double InputSampler::applyDeadzone(double value, double deadzone) {
  if (std::abs(value) < deadzone)
    return 0.0;
  // Rescale to [0, 1] outside deadzone
  const double sign = value > 0 ? 1.0 : -1.0;
  return sign * (std::abs(value) - deadzone) / (1.0 - deadzone);
}

double InputSampler::applySmoothing(double current, double target, double factor) {
  return current * (1.0 - factor) + target * factor;
}
