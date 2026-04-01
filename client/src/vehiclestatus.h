#ifndef VEHICLESTATUS_H
#define VEHICLESTATUS_H

#include <QObject>
#include <QString>
#include <QLatin1String>

/** 驾驶模式字符串常量，全系统统一引用，避免硬编码中文字符串分散导致不一致 */
namespace DrivingMode {
static const QLatin1String Remote("远驾");
static const QLatin1String Auto("自驾");
}

/**
 * @brief 车辆状态信息类
 * 用于在 QML 中显示车辆状态（速度、连接状态等）
 */
class VehicleStatus : public QObject
{
    Q_OBJECT
    Q_PROPERTY(double speed READ speed NOTIFY speedChanged)
    Q_PROPERTY(double batteryLevel READ batteryLevel NOTIFY batteryLevelChanged)
    Q_PROPERTY(QString gear READ gear NOTIFY gearChanged)
    Q_PROPERTY(double steering READ steering NOTIFY steeringChanged)
    Q_PROPERTY(bool videoConnected READ videoConnected NOTIFY videoConnectedChanged)
    Q_PROPERTY(bool mqttConnected READ mqttConnected NOTIFY mqttConnectedChanged)
    Q_PROPERTY(QString connectionStatus READ connectionStatus NOTIFY connectionStatusChanged)
    Q_PROPERTY(double odometer READ odometer NOTIFY odometerChanged)
    Q_PROPERTY(double voltage READ voltage NOTIFY voltageChanged)
    Q_PROPERTY(double current READ current NOTIFY currentChanged)
    Q_PROPERTY(double temperature READ temperature NOTIFY temperatureChanged)
    Q_PROPERTY(double networkRtt READ networkRtt NOTIFY networkRttChanged)
    Q_PROPERTY(bool remoteControlEnabled READ remoteControlEnabled NOTIFY remoteControlEnabledChanged)
    Q_PROPERTY(QString drivingMode READ drivingMode NOTIFY drivingModeChanged)
    Q_PROPERTY(bool sweepActive READ sweepActive NOTIFY sweepActiveChanged)
    Q_PROPERTY(bool brakeActive READ brakeActive NOTIFY brakeActiveChanged)
    Q_PROPERTY(double waterTankLevel READ waterTankLevel NOTIFY waterTankLevelChanged)
    Q_PROPERTY(double trashBinLevel READ trashBinLevel NOTIFY trashBinLevelChanged)
    Q_PROPERTY(int cleaningCurrent READ cleaningCurrent NOTIFY cleaningCurrentChanged)
    Q_PROPERTY(int cleaningTotal READ cleaningTotal NOTIFY cleaningTotalChanged)

public:
    explicit VehicleStatus(QObject *parent = nullptr);

    double speed() const { return m_speed; }
    double batteryLevel() const { return m_batteryLevel; }
    QString gear() const { return m_gear; }
    double steering() const { return m_steering; }
    bool videoConnected() const { return m_videoConnected; }
    bool mqttConnected() const { return m_mqttConnected; }
    QString connectionStatus() const;
    double odometer() const { return m_odometer; }
    double voltage() const { return m_voltage; }
    double current() const { return m_current; }
    double temperature() const { return m_temperature; }
    double networkRtt() const { return m_networkRtt; }
    bool remoteControlEnabled() const { return m_remoteControlEnabled; }
    QString drivingMode() const { return m_drivingMode; }
    bool sweepActive() const { return m_sweepActive; }
    bool brakeActive() const { return m_brakeActive; }
    double waterTankLevel() const { return m_waterTankLevel; }
    double trashBinLevel() const { return m_trashBinLevel; }
    int cleaningCurrent() const { return m_cleaningCurrent; }
    int cleaningTotal() const { return m_cleaningTotal; }

public slots:
    void setSpeed(double speed);
    void setBatteryLevel(double level);
    void setGear(const QString &gear);
    void setSteering(double steering);
    void setVideoConnected(bool connected);
    void setMqttConnected(bool connected);
    void setOdometer(double odometer);
    void setVoltage(double voltage);
    void setCurrent(double current);
    void setTemperature(double temperature);
    void setNetworkRtt(double rtt);
    void setRemoteControlEnabled(bool enabled);
    void setDrivingMode(const QString &mode);
    void setSweepActive(bool active);
    void setBrakeActive(bool active);
    void setWaterTankLevel(double level);
    void setTrashBinLevel(double level);
    void setCleaningCurrent(int current);
    void setCleaningTotal(int total);
    void updateStatus(const QJsonObject &status);

signals:
    void speedChanged(double speed);
    void batteryLevelChanged(double level);
    void gearChanged(const QString &gear);
    void steeringChanged(double steering);
    void videoConnectedChanged(bool connected);
    void mqttConnectedChanged(bool connected);
    void connectionStatusChanged();
    void odometerChanged(double odometer);
    void voltageChanged(double voltage);
    void currentChanged(double current);
    void temperatureChanged(double temperature);
    void networkRttChanged(double rtt);
    void remoteControlEnabledChanged(bool enabled);
    void drivingModeChanged(const QString &mode);
    void sweepActiveChanged(bool active);
    void brakeActiveChanged(bool active);
    void waterTankLevelChanged(double level);
    void trashBinLevelChanged(double level);
    void cleaningCurrentChanged(int current);
    void cleaningTotalChanged(int total);

private:
    double m_speed = 0.0;
    double m_batteryLevel = 100.0;
    QString m_gear = "N";
    double m_steering = 0.0;
    bool m_videoConnected = false;
    bool m_mqttConnected = false;
    double m_odometer = 0.0;
    double m_voltage = 48.0;
    double m_current = 0.0;
    double m_temperature = 25.0;
    double m_networkRtt = 0.0;
    bool m_remoteControlEnabled = false;
    QString m_drivingMode = "自驾";
    bool m_sweepActive = false;
    bool m_brakeActive = false;
    double m_waterTankLevel = 75.0;
    double m_trashBinLevel = 40.0;
    int m_cleaningCurrent = 400;
    int m_cleaningTotal = 500;
};

#endif // VEHICLESTATUS_H
