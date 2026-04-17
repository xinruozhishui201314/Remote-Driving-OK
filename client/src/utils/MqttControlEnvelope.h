#pragma once

#include <QJsonObject>
#include <QString>

/**
 * 纯函数：MQTT / DataChannel 控制 JSON 的构造与发送前规整（便于 L1 单测，不依赖 broker）。
 * 生产路径由 MqttController::sendControlCommand 调用。
 */
namespace MqttControlEnvelope {

/** Semver wired on vehicle/control; keep in sync with mqtt/schemas/vehicle_control.json */
inline QString controlSchemaVersionString() { return QStringLiteral("1.2.0"); }

enum class PreferredChannel { Auto, DataChannel, Mqtt, WebSocket };

PreferredChannel parsePreferredChannel(const QString& channelType);

struct PrepareResult {
  QJsonObject cmd;
  bool ok = false;  // false = VIN 仍为空，应拒绝发送
};

/** 与 MqttController::sendControlCommand 一致：补 vin / timestampMs / seq，并校验 vin 非空。 */
PrepareResult prepareForSend(const QJsonObject& command, const QString& currentVin,
                             const QString& sessionId, qint64 timestampMs, uint32_t& seqCounter);

QJsonObject buildSteering(double angle, qint64 timestampMs);
QJsonObject buildThrottle(double throttle, qint64 timestampMs);
QJsonObject buildBrake(double brake, qint64 timestampMs, uint32_t seq);
QJsonObject buildTargetSpeed(double speedKmh, qint64 timestampMs);
QJsonObject buildEmergencyStop(bool enable, qint64 timestampMs);
QJsonObject buildGear(int gear, qint64 timestampMs);
QJsonObject buildSweep(const QString& sweepType, bool active, qint64 timestampMs);
QJsonObject buildLight(const QString& lightType, bool active);
QJsonObject buildDrive(double steering, double throttle, double brake, int gear,
                       qint64 timestampMs);
QJsonObject buildStartStream(qint64 timestampMs);
QJsonObject buildStopStream(qint64 timestampMs);
QJsonObject buildRemoteControl(bool enable, qint64 timestampMs);

/**
 * UI 控制指令统一信封（schemaVersion 与 mqtt/schemas/vehicle_control.json 对齐）。
 * VehicleControlService::sendUiCommand 与历史 QML 路径必须仅此函数构造。
 */
QJsonObject buildUiCommandEnvelope(const QString& type, const QJsonObject& payload, const QString& vin,
                                   const QString& sessionId, qint64 timestampMs, qint64 seq,
                                   const QString& traceId);

}  // namespace MqttControlEnvelope
