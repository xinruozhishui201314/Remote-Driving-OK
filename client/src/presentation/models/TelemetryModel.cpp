#include "TelemetryModel.h"
#include <QDebug>

TelemetryModel::TelemetryModel(QObject* parent)
    : QObject(parent)
{}

void TelemetryModel::update(double speed, double throttle, double brake,
                              double steering, int gear, double battery)
{
    if (m_speed != speed)       { m_speed    = speed;    emit speedChanged(speed); }
    if (m_throttle != throttle) { m_throttle = throttle; emit throttleChanged(throttle); }
    if (m_brake != brake)       { m_brake    = brake;    emit brakeChanged(brake); }
    if (m_steering != steering) { m_steering = steering; emit steeringChanged(steering); }
    if (m_gear != gear)         { m_gear     = gear;     emit gearChanged(gear); }
    if (m_battery != battery)   { m_battery  = battery;  emit batteryChanged(battery); }
}

void TelemetryModel::setRPM(double rpm)
{
    if (m_rpm != rpm) { m_rpm = rpm; emit rpmChanged(rpm); }
}

void TelemetryModel::setHeading(double deg)
{
    if (m_heading != deg) { m_heading = deg; emit headingChanged(deg); }
}

void TelemetryModel::setPosition(double lat, double lon)
{
    m_latitude  = lat;
    m_longitude = lon;
    emit positionChanged();
}

void TelemetryModel::setVehicleReady(bool ready)
{
    if (m_vehicleReady != ready) {
        m_vehicleReady = ready;
        emit vehicleReadyChanged(ready);
    }
}
