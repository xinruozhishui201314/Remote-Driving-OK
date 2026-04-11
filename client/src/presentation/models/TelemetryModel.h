#pragma once
#include <QObject>

/**
 * 遥测数据 MVVM 模型（《客户端架构设计》§3.4.2）。
 * 暴露给 QML 的所有驾驶状态。
 */
class TelemetryModel : public QObject {
  Q_OBJECT

  Q_PROPERTY(double speed READ speed NOTIFY speedChanged)
  Q_PROPERTY(double throttle READ throttle NOTIFY throttleChanged)
  Q_PROPERTY(double brake READ brake NOTIFY brakeChanged)
  Q_PROPERTY(double steering READ steering NOTIFY steeringChanged)
  Q_PROPERTY(int gear READ gear NOTIFY gearChanged)
  Q_PROPERTY(double battery READ battery NOTIFY batteryChanged)
  Q_PROPERTY(double rpm READ rpm NOTIFY rpmChanged)
  Q_PROPERTY(double heading READ heading NOTIFY headingChanged)
  Q_PROPERTY(double latitude READ latitude NOTIFY positionChanged)
  Q_PROPERTY(double longitude READ longitude NOTIFY positionChanged)
  Q_PROPERTY(bool vehicleReady READ vehicleReady NOTIFY vehicleReadyChanged)

 public:
  explicit TelemetryModel(QObject* parent = nullptr);

  double speed() const { return m_speed; }
  double throttle() const { return m_throttle; }
  double brake() const { return m_brake; }
  double steering() const { return m_steering; }
  int gear() const { return m_gear; }
  double battery() const { return m_battery; }
  double rpm() const { return m_rpm; }
  double heading() const { return m_heading; }
  double latitude() const { return m_latitude; }
  double longitude() const { return m_longitude; }
  bool vehicleReady() const { return m_vehicleReady; }

  // Update from vehicle telemetry (called from C++ service)
  Q_INVOKABLE void update(double speed, double throttle, double brake, double steering, int gear,
                          double battery);

  void setRPM(double rpm);
  void setHeading(double deg);
  void setPosition(double lat, double lon);
  void setVehicleReady(bool ready);

 signals:
  void speedChanged(double speed);
  void throttleChanged(double throttle);
  void brakeChanged(double brake);
  void steeringChanged(double steering);
  void gearChanged(int gear);
  void batteryChanged(double battery);
  void rpmChanged(double rpm);
  void headingChanged(double heading);
  void positionChanged();
  void vehicleReadyChanged(bool ready);

 private:
  double m_speed = 0.0;
  double m_throttle = 0.0;
  double m_brake = 0.0;
  double m_steering = 0.0;
  int m_gear = 0;
  double m_battery = 100.0;
  double m_rpm = 0.0;
  double m_heading = 0.0;
  double m_latitude = 0.0;
  double m_longitude = 0.0;
  bool m_vehicleReady = false;
};
