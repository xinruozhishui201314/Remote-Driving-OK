#include "TelemetryModel.h"

#include <QDateTime>
#include <QDebug>

TelemetryModel::TelemetryModel(QObject* parent)
    : QObject(parent),
      m_speed(0.0),
      m_throttle(0.0),
      m_brake(0.0),
      m_steering(0.0),
      m_gear(0),
      m_battery(100.0),
      m_rpm(0.0),
      m_heading(0.0),
      m_latitude(0.0),
      m_longitude(0.0),
      m_vehicleReady(false),
      m_lastUpdateTimestamp(0) {}

void TelemetryModel::update(double speed, double throttle, double brake, double steering, int gear,
                            double battery) {
  m_lastUpdateTimestamp = QDateTime::currentMSecsSinceEpoch();
  emit lastUpdateTimestampChanged(m_lastUpdateTimestamp);

  if (m_speed != speed) {
    m_speed = speed;
    emit speedChanged(speed);
  }
  if (m_throttle != throttle) {
    m_throttle = throttle;
    emit throttleChanged(throttle);
  }
  if (m_brake != brake) {
    m_brake = brake;
    emit brakeChanged(brake);
  }
  if (m_steering != steering) {
    m_steering = steering;
    emit steeringChanged(steering);
  }
  if (m_gear != gear) {
    m_gear = gear;
    emit gearChanged(gear);
  }
  if (m_battery != battery) {
    m_battery = battery;
    emit batteryChanged(battery);
  }
}

void TelemetryModel::setRPM(double rpm) {
  if (m_rpm != rpm) {
    m_rpm = rpm;
    emit rpmChanged(rpm);
  }
}

void TelemetryModel::setHeading(double deg) {
  if (m_heading != deg) {
    m_heading = deg;
    emit headingChanged(deg);
  }
}

void TelemetryModel::setPosition(double lat, double lon) {
  m_latitude = lat;
  m_longitude = lon;
  emit positionChanged();
}

void TelemetryModel::setVehicleReady(bool ready) {
  if (m_vehicleReady != ready) {
    m_vehicleReady = ready;
    emit vehicleReadyChanged(ready);
  }
}
