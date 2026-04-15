#ifndef VEHICLE_API_PARSERS_H
#define VEHICLE_API_PARSERS_H

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>

namespace rd_client_api {

/** Parse GET /api/v1/vins body into a vehicle array (same rules as
 * VehicleManager::onVehicleListReply). */
bool parseVehicleListHttpBody(const QByteArray &data, QJsonArray *vehiclesOut, QString *errorOut);

struct SessionCreateParseOutcome {
  bool ok = false;
  QString error = {};
  QString sessionId = {};
  QString canonicalVin = {};
  QString whipUrl = {};
  QString whepUrl = {};
  QJsonObject controlConfig = {};
};

/**
 * Parse POST .../sessions JSON body. requestVin used for VIN/path consistency (same as
 * VehicleManager). Does not check stale currentVin — caller discards if selection changed.
 */
SessionCreateParseOutcome parseSessionCreateHttpBody(const QByteArray &data,
                                                     const QString &requestVin);

}  // namespace rd_client_api

#endif
