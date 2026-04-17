#include "MqttControlEnvelope.h"

#include <QtGlobal>

using namespace MqttControlEnvelope;

PreferredChannel MqttControlEnvelope::parsePreferredChannel(const QString& channelType) {
  const QString lower = channelType.toLower().trimmed();
  if (lower == QStringLiteral("data_channel") || lower == QStringLiteral("datachannel") ||
      lower == QStringLiteral("webrtc")) {
    return PreferredChannel::DataChannel;
  }
  if (lower == QStringLiteral("mqtt")) {
    return PreferredChannel::Mqtt;
  }
  if (lower == QStringLiteral("websocket") || lower == QStringLiteral("ws")) {
    return PreferredChannel::WebSocket;
  }
  return PreferredChannel::Auto;
}

PrepareResult MqttControlEnvelope::prepareForSend(const QJsonObject& command,
                                                  const QString& currentVin,
                                                  const QString& sessionId, qint64 timestampMs,
                                                  uint32_t& seqCounter) {
  QJsonObject cmd = command;
  if (!currentVin.isEmpty() && !cmd.contains(QStringLiteral("vin"))) {
    cmd[QStringLiteral("vin")] = currentVin;
  }
  if (!sessionId.isEmpty() && !cmd.contains(QStringLiteral("sessionId"))) {
    cmd[QStringLiteral("sessionId")] = sessionId;
  }
  if (!cmd.contains(QStringLiteral("timestampMs"))) {
    cmd[QStringLiteral("timestampMs")] = timestampMs;
  }
  if (!cmd.contains(QStringLiteral("seq"))) {
    cmd[QStringLiteral("seq")] = static_cast<int>(++seqCounter);
  } else {
    qint64 existingSeq = cmd.value(QStringLiteral("seq")).toVariant().toLongLong();
    if (existingSeq > 1000000000) {
        qWarning().noquote() << "★★★ [关键证据][取证] prepareForSend 发现已有超大 seq=" << existingSeq
                             << " type=" << cmd.value(QStringLiteral("type")).toString()
                             << " 正在保留该错误数值";
    }
  }
  if (!cmd.contains(QStringLiteral("schemaVersion"))) {
    cmd[QStringLiteral("schemaVersion")] = controlSchemaVersionString();
  }
  const QString vinIn = cmd.value(QStringLiteral("vin")).toString();
  return {cmd, !vinIn.isEmpty()};
}

QJsonObject MqttControlEnvelope::buildSteering(double angle, qint64 timestampMs) {
  QJsonObject cmd;
  cmd[QStringLiteral("type")] = QStringLiteral("steering");
  cmd[QStringLiteral("value")] = qBound(-1.0, angle, 1.0);
  cmd[QStringLiteral("timestampMs")] = timestampMs;
  return cmd;
}

QJsonObject MqttControlEnvelope::buildThrottle(double throttle, qint64 timestampMs) {
  QJsonObject cmd;
  cmd[QStringLiteral("type")] = QStringLiteral("throttle");
  cmd[QStringLiteral("value")] = qBound(0.0, throttle, 1.0);
  cmd[QStringLiteral("timestampMs")] = timestampMs;
  return cmd;
}

QJsonObject MqttControlEnvelope::buildBrake(double brake, qint64 timestampMs, uint32_t seq) {
  QJsonObject cmd;
  cmd[QStringLiteral("type")] = QStringLiteral("brake");
  cmd[QStringLiteral("value")] = qBound(0.0, brake, 1.0);
  cmd[QStringLiteral("timestampMs")] = timestampMs;
  cmd[QStringLiteral("seq")] = static_cast<int>(seq);
  return cmd;
}

QJsonObject MqttControlEnvelope::buildTargetSpeed(double speedKmh, qint64 timestampMs) {
  QJsonObject cmd;
  cmd[QStringLiteral("type")] = QStringLiteral("target_speed");
  cmd[QStringLiteral("value")] = qBound(0.0, speedKmh, 100.0);
  cmd[QStringLiteral("timestampMs")] = timestampMs;
  return cmd;
}

QJsonObject MqttControlEnvelope::buildEmergencyStop(bool enable, qint64 timestampMs) {
  QJsonObject cmd;
  cmd[QStringLiteral("type")] = QStringLiteral("emergency_stop");
  cmd[QStringLiteral("enable")] = enable;
  cmd[QStringLiteral("timestampMs")] = timestampMs;
  return cmd;
}

QJsonObject MqttControlEnvelope::buildGear(int gear, qint64 timestampMs) {
  QJsonObject cmd;
  cmd[QStringLiteral("type")] = QStringLiteral("gear");
  cmd[QStringLiteral("value")] = gear;
  cmd[QStringLiteral("timestampMs")] = timestampMs;
  return cmd;
}

QJsonObject MqttControlEnvelope::buildSweep(const QString& sweepType, bool active,
                                            qint64 timestampMs) {
  QJsonObject cmd;
  cmd[QStringLiteral("type")] = QStringLiteral("sweep");
  cmd[QStringLiteral("sweepType")] = sweepType;
  cmd[QStringLiteral("active")] = active;
  cmd[QStringLiteral("timestampMs")] = timestampMs;
  return cmd;
}

QJsonObject MqttControlEnvelope::buildLight(const QString& lightType, bool active) {
  QJsonObject cmd;
  cmd[QStringLiteral("type")] = QStringLiteral("mode");
  cmd[QStringLiteral("subType")] = QStringLiteral("light");
  cmd[QStringLiteral("lightType")] = lightType;
  cmd[QStringLiteral("active")] = active;
  return cmd;
}

QJsonObject MqttControlEnvelope::buildDrive(double steering, double throttle, double brake,
                                            int gear, double speed, qint64 timestampMs) {
  QJsonObject cmd;
  cmd[QStringLiteral("type")] = QStringLiteral("drive");
  cmd[QStringLiteral("steering")] = qBound(-1.0, steering, 1.0);
  cmd[QStringLiteral("throttle")] = qBound(0.0, throttle, 1.0);
  cmd[QStringLiteral("brake")] = qBound(0.0, brake, 1.0);
  cmd[QStringLiteral("gear")] = gear;
  cmd[QStringLiteral("speed")] = qBound(0.0, speed, 100.0);
  cmd[QStringLiteral("emergency_stop")] = false;
  cmd[QStringLiteral("timestampMs")] = timestampMs;
  return cmd;
}

QJsonObject MqttControlEnvelope::buildStartStream(qint64 timestampMs) {
  QJsonObject cmd;
  cmd[QStringLiteral("type")] = QStringLiteral("start_stream");
  cmd[QStringLiteral("timestampMs")] = timestampMs;
  return cmd;
}

QJsonObject MqttControlEnvelope::buildStopStream(qint64 timestampMs) {
  QJsonObject cmd;
  cmd[QStringLiteral("type")] = QStringLiteral("stop_stream");
  cmd[QStringLiteral("timestampMs")] = timestampMs;
  return cmd;
}

QJsonObject MqttControlEnvelope::buildRemoteControl(bool enable, qint64 timestampMs) {
  QJsonObject cmd;
  cmd[QStringLiteral("type")] = QStringLiteral("remote_control");
  cmd[QStringLiteral("enable")] = enable;
  cmd[QStringLiteral("timestampMs")] = timestampMs;
  return cmd;
}

QJsonObject MqttControlEnvelope::buildUiCommandEnvelope(const QString& type, const QJsonObject& payload,
                                                        const QString& vin, const QString& sessionId,
                                                        qint64 timestampMs, qint64 seq,
                                                        const QString& traceId) {
  QJsonObject json;
  json[QStringLiteral("schemaVersion")] = controlSchemaVersionString();
  json[QStringLiteral("type")] = type;
  json[QStringLiteral("payload")] = payload;
  json[QStringLiteral("vin")] = vin;
  json[QStringLiteral("sessionId")] = sessionId;
  json[QStringLiteral("timestampMs")] = timestampMs;
  json[QStringLiteral("seq")] = seq;
  json[QStringLiteral("trace_id")] = traceId;
  return json;
}
