#include "vehiclestatus.h"

#include <optional>

#include <QDateTime>
#include <QDebug>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>

VehicleStatus::VehicleStatus(QObject *parent)
    : QObject(parent),
      m_speed(0.0),
      m_batteryLevel(100.0),
      m_gear("N"),
      m_steering(0.0),
      m_videoConnected(false),
      m_mqttConnected(false),
      m_odometer(0.0),
      m_voltage(48.0),
      m_current(0.0),
      m_temperature(25.0),
      m_networkRtt(0.0),
      m_networkPacketLossPercent(0.0),
      m_networkBandwidthKbps(0.0),
      m_networkJitterMs(0.0),
      m_remoteControlEnabled(false),
      m_drivingMode("自驾"),
      m_sweepActive(false),
      m_brakeActive(false),
      m_waterTankLevel(75.0),
      m_trashBinLevel(40.0),
      m_cleaningCurrent(400),
      m_cleaningTotal(500),
      m_lastStatusTimestamp(0) {}

void VehicleStatus::setSpeed(double speed) {
  if (qAbs(m_speed - speed) > 0.01) {
    m_speed = speed;
    emit speedChanged(m_speed);
  }
}

void VehicleStatus::setBatteryLevel(double level) {
  if (qAbs(m_batteryLevel - level) > 0.1) {
    m_batteryLevel = qBound(0.0, level, 100.0);
    emit batteryLevelChanged(m_batteryLevel);
  }
}

void VehicleStatus::setGear(const QString &gear) {
  QString g = gear.trimmed().toUpper();
  if (g.isEmpty())
    g = "N";
  if (m_gear != g) {
    m_gear = g;
    emit gearChanged(m_gear);
  }
}

void VehicleStatus::setSteering(double steering) {
  if (qAbs(m_steering - steering) > 0.001) {
    m_steering = qBound(-1.0, steering, 1.0);
    emit steeringChanged(m_steering);
  }
}

void VehicleStatus::setVideoConnected(bool connected) {
  if (m_videoConnected != connected) {
    m_videoConnected = connected;
    qInfo().noquote() << "[Client][VehicleStatus] videoConnected 变更 →" << connected
                      << "（来自 WebRtcStreamManager.anyConnected 聚合，非独立 webrtcClient）";
    emit videoConnectedChanged(connected);
    emit connectionStatusChanged();
  }
}

void VehicleStatus::setMqttConnected(bool connected) {
  if (m_mqttConnected != connected) {
    m_mqttConnected = connected;
    emit mqttConnectedChanged(connected);
    emit connectionStatusChanged();
  }
}

void VehicleStatus::setOdometer(double odometer) {
  if (qAbs(m_odometer - odometer) > 0.01) {
    m_odometer = odometer;
    emit odometerChanged(m_odometer);
  }
}

void VehicleStatus::setVoltage(double voltage) {
  if (qAbs(m_voltage - voltage) > 0.1) {
    m_voltage = voltage;
    emit voltageChanged(m_voltage);
  }
}

void VehicleStatus::setCurrent(double current) {
  if (qAbs(m_current - current) > 0.1) {
    m_current = current;
    emit currentChanged(m_current);
  }
}

void VehicleStatus::setTemperature(double temperature) {
  if (qAbs(m_temperature - temperature) > 0.1) {
    m_temperature = temperature;
    emit temperatureChanged(m_temperature);
  }
}

void VehicleStatus::setNetworkRtt(double rtt) {
  if (qAbs(m_networkRtt - rtt) > 1.0) {
    m_networkRtt = rtt;
    emit networkRttChanged(m_networkRtt);
  }
}

void VehicleStatus::setNetworkPacketLossPercent(double percent) {
  const double c = qBound(0.0, percent, 100.0);
  if (qAbs(m_networkPacketLossPercent - c) > 0.05) {
    m_networkPacketLossPercent = c;
    emit networkPacketLossPercentChanged(m_networkPacketLossPercent);
  }
}

void VehicleStatus::setNetworkBandwidthKbps(double kbps) {
  const double c = qBound(0.0, kbps, 1e9);
  if (qAbs(m_networkBandwidthKbps - c) > 1.0) {
    m_networkBandwidthKbps = c;
    emit networkBandwidthKbpsChanged(m_networkBandwidthKbps);
  }
}

void VehicleStatus::setNetworkJitterMs(double jitterMs) {
  const double c = qBound(0.0, jitterMs, 60000.0);
  if (qAbs(m_networkJitterMs - c) > 0.5) {
    m_networkJitterMs = c;
    emit networkJitterMsChanged(m_networkJitterMs);
  }
}

void VehicleStatus::setRemoteControlEnabled(bool enabled) {
  if (m_remoteControlEnabled != enabled) {
    m_remoteControlEnabled = enabled;
    qDebug() << "[VEHICLE_STATUS] 远驾接管状态更新:" << (enabled ? "已启用" : "已禁用");
    emit remoteControlEnabledChanged(m_remoteControlEnabled);
  }
}

void VehicleStatus::setStreamingReady(bool ready) {
  if (m_streamingReady != ready) {
    m_streamingReady = ready;
    qInfo() << "[Client][VehicleStatus] streamingReady 变更 →" << ready
            << "（来自车端 status.streaming_ready）";
    emit streamingReadyChanged(m_streamingReady);
  }
}

void VehicleStatus::setDrivingMode(const QString &mode) {
  QString m = mode.trimmed();
  if (m.isEmpty())
    m = "自驾";
  if (m_drivingMode != m) {
    m_drivingMode = m;
    emit drivingModeChanged(m_drivingMode);
  }
}

void VehicleStatus::setSweepActive(bool active) {
  if (m_sweepActive != active) {
    m_sweepActive = active;
    emit sweepActiveChanged(m_sweepActive);
  }
}

void VehicleStatus::setBrakeActive(bool active) {
  if (m_brakeActive != active) {
    m_brakeActive = active;
    qDebug() << "[CLIENT][VEHICLE_STATUS] brake_active=" << active;
    emit brakeActiveChanged(m_brakeActive);
  }
}

void VehicleStatus::setWaterTankLevel(double level) {
  double clamped = qBound(0.0, level, 100.0);
  if (qAbs(m_waterTankLevel - clamped) > 0.5) {
    m_waterTankLevel = clamped;
    emit waterTankLevelChanged(m_waterTankLevel);
  }
}

void VehicleStatus::setTrashBinLevel(double level) {
  double clamped = qBound(0.0, level, 100.0);
  if (qAbs(m_trashBinLevel - clamped) > 0.5) {
    m_trashBinLevel = clamped;
    emit trashBinLevelChanged(m_trashBinLevel);
  }
}

void VehicleStatus::setCleaningCurrent(int current) {
  if (current < 0)
    current = 0;
  if (m_cleaningCurrent != current) {
    m_cleaningCurrent = current;
    emit cleaningCurrentChanged(m_cleaningCurrent);
  }
}

void VehicleStatus::setCleaningTotal(int total) {
  if (total < 0)
    total = 0;
  if (m_cleaningTotal != total) {
    m_cleaningTotal = total;
    emit cleaningTotalChanged(m_cleaningTotal);
  }
}

void VehicleStatus::updateStatus(const QJsonObject &status) {
  m_lastStatusTimestamp = QDateTime::currentMSecsSinceEpoch();
  emit lastStatusTimestampChanged(m_lastStatusTimestamp);

  static int updateCount = 0;
  static QElapsedTimer logTimer;
  static QElapsedTimer firstUpdateTimer;

  updateCount++;

  // 初始化计时器
  if (updateCount == 1) {
    logTimer.start();
    firstUpdateTimer.start();
    qDebug() << "[VEHICLE_STATUS] 开始更新车辆状态";
  }

  bool hasChanges = false;
  QStringList changedFields;

  // 日志记录（每50次或每5秒记录一次）
  bool shouldLog = (updateCount % 50 == 0) || (logTimer.elapsed() >= 5000);

  if (status.contains("speed")) {
    double newSpeed = qBound(-200.0, status["speed"].toDouble(), 200.0);  // km/h 合理范围
    if (qAbs(m_speed - newSpeed) > 0.01) {
      setSpeed(newSpeed);
      hasChanges = true;
      if (shouldLog)
        changedFields << QString("speed:%1").arg(newSpeed, 0, 'f', 1);
    }
  }
  if (status.contains("battery")) {
    double newBattery = qBound(0.0, status["battery"].toDouble(), 100.0);  // 百分比 [0,100]
    if (qAbs(m_batteryLevel - newBattery) > 0.1) {
      setBatteryLevel(newBattery);
      hasChanges = true;
      if (shouldLog)
        changedFields << QString("battery:%1").arg(newBattery, 0, 'f', 1);
    }
  }
  if (status.contains("gear")) {
    QJsonValue g = status["gear"];
    QString newGear = m_gear;
    QString oldGear = m_gear;
    if (g.isString()) {
      newGear = g.toString().trimmed().toUpper();
      if (newGear.isEmpty())
        newGear = "N";
    } else if (g.isDouble()) {
      int gi = static_cast<int>(g.toDouble());
      if (gi == -1)
        newGear = "R";
      else if (gi == 0)
        newGear = "N";
      else if (gi == 1)
        newGear = "D";
      else if (gi == 2)
        newGear = "P";
      else
        newGear = "N";
    }
    if (m_gear != newGear) {
      qDebug() << "[CLIENT][VEHICLE_STATUS] gear:" << oldGear << "->" << newGear;
      setGear(newGear);
      hasChanges = true;
      if (shouldLog)
        changedFields << QString("gear:%1").arg(newGear);
    }
  }
  if (status.contains("steering")) {
    double newSteering = qBound(-1.0, status["steering"].toDouble(), 1.0);  // 归一化转向 [-1,1]
    if (qAbs(m_steering - newSteering) > 0.001) {
      setSteering(newSteering);
      hasChanges = true;
      if (shouldLog)
        changedFields << QString("steering:%1").arg(newSteering, 0, 'f', 3);
    }
  }
  if (status.contains("odometer")) {
    double newOdometer = qBound(0.0, status["odometer"].toDouble(), 1e7);  // 里程 [0, 10000km]
    if (qAbs(m_odometer - newOdometer) > 0.01) {
      setOdometer(newOdometer);
      hasChanges = true;
      if (shouldLog)
        changedFields << QString("odometer:%1").arg(newOdometer, 0, 'f', 2);
    }
  }
  if (status.contains("voltage")) {
    double newVoltage = qBound(0.0, status["voltage"].toDouble(), 1000.0);  // 电压 [0,1000V]
    if (qAbs(m_voltage - newVoltage) > 0.1) {
      setVoltage(newVoltage);
      hasChanges = true;
      if (shouldLog)
        changedFields << QString("voltage:%1").arg(newVoltage, 0, 'f', 1);
    }
  }
  if (status.contains("network_rtt")) {
    double newRtt = qBound(0.0, status["network_rtt"].toDouble(), 60000.0);  // RTT [0,60s]
    if (qAbs(m_networkRtt - newRtt) > 1.0) {
      setNetworkRtt(newRtt);
      hasChanges = true;
      if (shouldLog)
        changedFields << QString("rtt:%1ms").arg(newRtt, 0, 'f', 0);
    }
  }
  auto readLoss = [&status]() -> std::optional<double> {
    const char *keys[] = {"network_packet_loss_percent", "packet_loss_percent", "packetLossPercent",
                          "network_packet_loss"};
    for (const char *k : keys) {
      if (!status.contains(QLatin1String(k)))
        continue;
      const QJsonValue v = status[QLatin1String(k)];
      if (v.isDouble())
        return v.toDouble();
      if (v.isString()) {
        bool ok = false;
        const double d = v.toString().toDouble(&ok);
        if (ok)
          return d;
      }
    }
    return std::nullopt;
  };
  if (const auto loss = readLoss()) {
    const double newLoss = qBound(0.0, *loss, 100.0);
    if (qAbs(m_networkPacketLossPercent - newLoss) > 0.05) {
      setNetworkPacketLossPercent(newLoss);
      hasChanges = true;
      if (shouldLog)
        changedFields << QString("loss:%1%").arg(newLoss, 0, 'f', 2);
    }
  }
  auto readBandwidth = [&status]() -> std::optional<double> {
    const char *keys[] = {"network_bandwidth_kbps", "bandwidth_kbps", "bandwidthKbps", "downlink_kbps"};
    for (const char *k : keys) {
      if (!status.contains(QLatin1String(k)))
        continue;
      const QJsonValue v = status[QLatin1String(k)];
      if (v.isDouble())
        return v.toDouble();
      if (v.isString()) {
        bool ok = false;
        const double d = v.toString().toDouble(&ok);
        if (ok)
          return d;
      }
    }
    return std::nullopt;
  };
  if (const auto bw = readBandwidth()) {
    const double newBw = qBound(0.0, *bw, 1e9);
    if (qAbs(m_networkBandwidthKbps - newBw) > 1.0) {
      setNetworkBandwidthKbps(newBw);
      hasChanges = true;
      if (shouldLog)
        changedFields << QString("bw:%1kbps").arg(newBw, 0, 'f', 0);
    }
  }
  auto readJitter = [&status]() -> std::optional<double> {
    const char *keys[] = {"network_jitter_ms", "jitter_ms", "jitterMs", "network_jitter"};
    for (const char *k : keys) {
      if (!status.contains(QLatin1String(k)))
        continue;
      const QJsonValue v = status[QLatin1String(k)];
      if (v.isDouble())
        return v.toDouble();
      if (v.isString()) {
        bool ok = false;
        const double d = v.toString().toDouble(&ok);
        if (ok)
          return d;
      }
    }
    return std::nullopt;
  };
  if (const auto jit = readJitter()) {
    const double newJ = qBound(0.0, *jit, 60000.0);
    if (qAbs(m_networkJitterMs - newJ) > 0.5) {
      setNetworkJitterMs(newJ);
      hasChanges = true;
      if (shouldLog)
        changedFields << QString("jit:%1ms").arg(newJ, 0, 'f', 1);
    }
  }
  if (status.contains("current")) {
    double newCurrent = qBound(-1000.0, status["current"].toDouble(), 1000.0);  // 电流 [±1000A]
    if (qAbs(m_current - newCurrent) > 0.1) {
      setCurrent(newCurrent);
      hasChanges = true;
      if (shouldLog)
        changedFields << QString("current:%1").arg(newCurrent, 0, 'f', 1);
    }
  }
  if (status.contains("temperature")) {
    double newTemp = status["temperature"].toDouble();
    if (qAbs(m_temperature - newTemp) > 0.1) {
      setTemperature(newTemp);
      hasChanges = true;
      if (shouldLog)
        changedFields << QString("temperature:%1").arg(newTemp, 0, 'f', 1);
    }
  }
  bool isAckMessage = status.contains("type") && status["type"].toString() == "remote_control_ack";

  if (status.contains("remote_control_enabled")) {
    bool newRemoteControlEnabled = status["remote_control_enabled"].toBool();
    if (m_remoteControlEnabled != newRemoteControlEnabled) {
      qDebug() << "[CLIENT][VEHICLE_STATUS] remote_control_enabled:" << m_remoteControlEnabled
               << "->" << newRemoteControlEnabled;
      setRemoteControlEnabled(newRemoteControlEnabled);
      hasChanges = true;
      if (shouldLog || isAckMessage)
        changedFields << QString("rc_enabled:%1").arg(newRemoteControlEnabled ? "1" : "0");
    }
  }

  if (status.contains("streaming_ready")) {
    bool newStreamingReady = status["streaming_ready"].toBool();
    if (m_streamingReady != newStreamingReady) {
      setStreamingReady(newStreamingReady);
      hasChanges = true;
      if (shouldLog)
        changedFields << QString("streaming_ready:%1").arg(newStreamingReady ? "1" : "0");
    }
  }

  if (status.contains("driving_mode")) {
    QString newDrivingMode = status["driving_mode"].toString().trimmed();
    if (newDrivingMode.isEmpty())
      newDrivingMode = "自驾";
    if (m_drivingMode != newDrivingMode) {
      qDebug() << "[CLIENT][VEHICLE_STATUS] driving_mode:" << m_drivingMode << "->"
               << newDrivingMode;
      setDrivingMode(newDrivingMode);
      hasChanges = true;
      if (shouldLog || isAckMessage)
        changedFields << QString("mode:%1").arg(newDrivingMode);
    }
  }

  if (status.contains("sweep_active")) {
    bool newSweepActive = status["sweep_active"].toBool(false);
    if (m_sweepActive != newSweepActive) {
      setSweepActive(newSweepActive);
      hasChanges = true;
      if (shouldLog)
        changedFields << QString("sweep:%1").arg(newSweepActive ? "1" : "0");
    }
  }

  if (status.contains("brake")) {
    double brakeValue = status["brake"].toDouble(0.0);
    bool newBrakeActive = (brakeValue > 0.1);
    if (m_brakeActive != newBrakeActive) {
      setBrakeActive(newBrakeActive);
      hasChanges = true;
      if (shouldLog)
        changedFields << QString("brake:%1").arg(newBrakeActive ? "1" : "0");
    }
  }

  // ★ 水箱/垃圾箱/清扫进度（环卫车遥测）
  if (status.contains("water_tank_level") || status.contains("waterTankLevel")) {
    double v = status.contains("water_tank_level") ? status["water_tank_level"].toDouble(75.0)
                                                   : status["waterTankLevel"].toDouble(75.0);
    setWaterTankLevel(v);
    if (shouldLog)
      changedFields << QString("water_tank:%1").arg(v, 0, 'f', 1);
  }
  if (status.contains("trash_bin_level") || status.contains("trashBinLevel")) {
    double v = status.contains("trash_bin_level") ? status["trash_bin_level"].toDouble(40.0)
                                                  : status["trashBinLevel"].toDouble(40.0);
    setTrashBinLevel(v);
    if (shouldLog)
      changedFields << QString("trash_bin:%1").arg(v, 0, 'f', 1);
  }
  if (status.contains("cleaning_current") || status.contains("cleaningCurrent")) {
    int v = status.contains("cleaning_current") ? status["cleaning_current"].toInt(400)
                                                : status["cleaningCurrent"].toInt(400);
    setCleaningCurrent(v);
    if (shouldLog)
      changedFields << QString("cleaning_current:%1").arg(v);
  }
  if (status.contains("cleaning_total") || status.contains("cleaningTotal")) {
    int v = status.contains("cleaning_total") ? status["cleaning_total"].toInt(500)
                                              : status["cleaningTotal"].toInt(500);
    setCleaningTotal(v);
    if (shouldLog)
      changedFields << QString("cleaning_total:%1").arg(v);
  }

  if (isAckMessage) {
    qDebug() << "[CLIENT][VEHICLE_STATUS] ack processed: rc=" << m_remoteControlEnabled
             << "mode=" << m_drivingMode;
  }

  // 记录更新日志
  if (shouldLog && hasChanges) {
    // 计算实际更新频率
    qint64 totalElapsed = firstUpdateTimer.elapsed();
    double actualFreq = (totalElapsed > 0) ? (updateCount * 1000.0 / totalElapsed) : 0.0;

    qDebug() << "[VEHICLE_STATUS] 更新 #" << updateCount
             << " | 变化字段:" << changedFields.join(", ")
             << " | 实际频率:" << QString::number(actualFreq, 'f', 1) << "Hz"
             << " | 当前状态: 速度" << QString::number(m_speed, 'f', 1) << "km/h"
             << ", 电池" << QString::number(m_batteryLevel, 'f', 1) << "%"
             << ", 里程" << QString::number(m_odometer, 'f', 2) << "km";

    logTimer.restart();
  }

  // 底盘快照（节流）：grep "[Client][VehicleStatus][FEEDBACK]" 对照 UI / 键盘 / 桥日志
  static QElapsedTimer s_feedbackLog;
  if (!s_feedbackLog.isValid())
    s_feedbackLog.start();
  const bool chassisLike = !status.contains(QLatin1String("type")) ||
                           status.value(QLatin1String("type")).toString() == QLatin1String("vehicle_status");
  if (chassisLike && s_feedbackLog.elapsed() >= 400) {
    s_feedbackLog.restart();
    const QString vin = status.value(QLatin1String("vin")).toString();
    qInfo().noquote() << "[Client][VehicleStatus][FEEDBACK] n=" << updateCount << "vin=" << vin
                      << "speed_kmh=" << QString::number(m_speed, 'f', 2)
                      << "steering_norm=" << QString::number(m_steering, 'f', 4)
                      << "gear=" << m_gear << "rc=" << (m_remoteControlEnabled ? 1 : 0)
                      << "mode=" << m_drivingMode
                      << "delta=" << (hasChanges ? changedFields.join(QLatin1Char(',')) : QLatin1String("(none)"));
  }
}

QString VehicleStatus::connectionStatus() const {
  if (m_videoConnected && m_mqttConnected) {
    return "已连接";
  } else if (m_videoConnected || m_mqttConnected) {
    return "部分连接";
  } else {
    return "未连接";
  }
}
