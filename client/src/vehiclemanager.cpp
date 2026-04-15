#include "vehiclemanager.h"

#include "services/remotesessionclient.h"
#include "services/vehiclecatalogclient.h"

#include <QDebug>
#include <QJsonDocument>
#include <QString>

VehicleManager::VehicleManager(QObject *parent)
    : QObject(parent),
      m_vehicleList(),
      m_vehicles(),
      m_currentVin(),
      m_networkManager(nullptr),
      m_catalog(nullptr),
      m_sessionClient(nullptr),
      m_lastSessionId(),
      m_lastSessionVin(),
      m_lastWhipUrl(),
      m_lastWhepUrl(),
      m_lastControlConfig() {
  m_networkManager = new QNetworkAccessManager(this);
  m_catalog = new VehicleCatalogClient(m_networkManager, this);
  m_sessionClient = new RemoteSessionClient(m_networkManager, this);
  connect(m_catalog, &VehicleCatalogClient::listSucceeded, this,
          [this](const QJsonArray &vehicles) {
            updateVehicleList(vehicles);
            QStringList vinList;
            for (const QJsonValue &v : vehicles) {
              if (v.isObject() && v.toObject().contains(QStringLiteral("vin")))
                vinList << v.toObject().value(QStringLiteral("vin")).toString();
            }
            qDebug().noquote() << "[Client][车辆列表] 已加载 count=" << vinList.size()
                               << " vins=" << vinList.join(QLatin1Char(','));
            emit vehicleListLoaded(vehicles);
          });
  connect(m_catalog, &VehicleCatalogClient::listFailed, this,
          &VehicleManager::vehicleListLoadFailed);

  connect(
      m_sessionClient, &RemoteSessionClient::sessionSucceeded, this,
      [this](const QString &requestVin, const QString &canonicalVin, const QString &sessionId,
             const QString &whipUrl, const QString &whepUrl, const QJsonObject &controlConfig) {
        if (m_currentVin != requestVin) {
          qWarning() << "[Client][Session] 丢弃过期会话响应（选车已变更） requestVin=" << requestVin
                     << " currentVin=" << m_currentVin << " sessionId=" << sessionId
                     << " — 不更新 lastWhep / 不发 sessionCreated";
          return;
        }

        m_lastSessionId = sessionId;
        m_lastSessionVin = canonicalVin;
        m_lastWhipUrl = whipUrl;
        m_lastWhepUrl = whepUrl;
        m_lastControlConfig = controlConfig;

        qInfo().noquote() << QStringLiteral("[Client][StreamE2E][VM_SESSION_OK] canonicalVin=")
                          << canonicalVin << "sessionId=" << sessionId
                          << "lastWhepUrl_empty=" << (whepUrl.isEmpty() ? 1 : 0)
                          << "lastWhepUrl_len=" << whepUrl.size()
                          << "★ QML DrivingTopChrome 25s 定时器用 vehicleManager.lastWhepUrl；空则 "
                             "connectFourStreams 仅依赖 ZLM_VIDEO_URL";

        qDebug() << "[CLIENT][Session] 会话创建成功 sessionVin=" << canonicalVin
                 << " requestVin=" << requestVin << " sessionId=" << sessionId << " whepUrl="
                 << (whepUrl.length() > 80 ? whepUrl.left(80) + QStringLiteral("...") : whepUrl);
        if (!controlConfig.isEmpty()) {
          QString broker = controlConfig.value(QStringLiteral("mqtt_broker_url")).toString();
          qDebug() << "[CLIENT][Session] control.mqtt_broker_url="
                   << (broker.isEmpty() ? QStringLiteral("(空，连接车端时需手动填 MQTT)") : broker);
        }

        emit sessionInfoChanged();
        emit sessionCreated(canonicalVin, sessionId, whipUrl, whepUrl, controlConfig);
      });
  connect(m_sessionClient, &RemoteSessionClient::sessionFailed, this,
          &VehicleManager::sessionCreateFailed);
}

VehicleManager::~VehicleManager() = default;

void VehicleManager::setCurrentVin(const QString &vin) {
  if (vin.isEmpty()) {
    if (!m_currentVin.isEmpty()) {
      m_currentVin.clear();
      qInfo() << "[CLIENT][选车] VIN 已清空";
      emit currentVinChanged(m_currentVin);
      emit currentVehicleChanged();
    }
    return;
  }
  if (!m_vehicles.contains(vin)) {
    qWarning()
        << "[CLIENT][选车] setCurrentVin 已拒绝：VIN 不在当前列表（可能列表未加载完或已刷新） vin="
        << vin << " currentVin=" << m_currentVin;
    return;
  }
  if (m_currentVin == vin)
    return;
  m_currentVin = vin;
  qDebug() << "[CLIENT][选车] 当前车辆 VIN=" << m_currentVin << "name=" << m_vehicles[vin].name;
  emit currentVinChanged(m_currentVin);
  emit currentVehicleChanged();

  VehicleInfo info = m_vehicles[vin];
  QJsonObject json;
  json[QStringLiteral("vin")] = info.vin;
  json[QStringLiteral("name")] = info.name;
  json[QStringLiteral("model")] = info.model;
  json[QStringLiteral("status")] = info.status;
  json[QStringLiteral("metadata")] = info.metadata;
  emit vehicleSelected(vin, json);
}

QString VehicleManager::currentVehicleName() const {
  if (m_currentVin.isEmpty() || !m_vehicles.contains(m_currentVin)) {
    return QString();
  }
  return m_vehicles[m_currentVin].name;
}

QJsonObject VehicleManager::getVehicleInfo(const QString &vin) const {
  if (!m_vehicles.contains(vin)) {
    return QJsonObject();
  }

  VehicleInfo info = m_vehicles[vin];
  QJsonObject json;
  json[QStringLiteral("vin")] = info.vin;
  json[QStringLiteral("name")] = info.name;
  json[QStringLiteral("model")] = info.model;
  json[QStringLiteral("status")] = info.status;
  json[QStringLiteral("metadata")] = info.metadata;
  return json;
}

void VehicleManager::loadVehicleList(const QString &serverUrl, const QString &authToken) {
  refreshVehicleList(serverUrl, authToken);
}

void VehicleManager::refreshVehicleList(const QString &serverUrl, const QString &authToken) {
  m_catalog->abortCurrent();
  m_catalog->requestVehicleList(serverUrl, authToken);
}

void VehicleManager::updateVehicleList(const QJsonArray &vehicles) {
  m_vehicleList.clear();
  m_vehicles.clear();

  for (const QJsonValue &value : vehicles) {
    if (value.isObject()) {
      VehicleInfo info = parseVehicleInfo(value.toObject());
      if (!info.vin.isEmpty()) {
        m_vehicleList.append(info.vin);
        m_vehicles[info.vin] = info;
      }
    }
  }

  if (!m_currentVin.isEmpty() && !m_vehicles.contains(m_currentVin)) {
    qWarning() << "[CLIENT][选车] 车辆列表刷新后原 currentVin 已不在列表，已清空以免 MQTT/WebRTC "
                  "仍用旧 VIN vin="
               << m_currentVin;
    m_currentVin.clear();
    emit currentVinChanged(m_currentVin);
    emit currentVehicleChanged();
  }

  emit vehicleListChanged();
}

VehicleManager::VehicleInfo VehicleManager::parseVehicleInfo(const QJsonObject &json) const {
  VehicleInfo info;
  info.vin = json[QStringLiteral("vin")].toString();
  info.name =
      json.contains(QStringLiteral("name")) ? json[QStringLiteral("name")].toString() : info.vin;
  info.model =
      json.contains(QStringLiteral("model")) ? json[QStringLiteral("model")].toString() : QString();
  info.status = json.contains(QStringLiteral("status")) ? json[QStringLiteral("status")].toString()
                                                        : QStringLiteral("unknown");

  if (json.contains(QStringLiteral("metadata")) && json[QStringLiteral("metadata")].isObject()) {
    info.metadata = json[QStringLiteral("metadata")].toObject();
  }

  return info;
}

void VehicleManager::selectVehicle(const QString &vin) { setCurrentVin(vin); }

void VehicleManager::addTestVehicle(const QString &vin, const QString &name) {
  if (vin.isEmpty())
    return;
  if (!m_vehicleList.contains(vin)) {
    m_vehicleList.append(vin);
    VehicleInfo info;
    info.vin = vin;
    info.name = name.isEmpty() ? vin : name;
    info.model = QString();
    info.status = QStringLiteral("测试");
    m_vehicles[vin] = info;
    emit vehicleListChanged();
  }
}

void VehicleManager::startSessionForCurrentVin(const QString &serverUrl, const QString &authToken) {
  if (m_currentVin.isEmpty()) {
    qWarning()
        << "[Client][Session] create session failed err=E_CONFIG_MISSING (no vehicle selected)";
    emit sessionCreateFailed(QStringLiteral("未选择车辆"));
    return;
  }

  m_sessionClient->abortCurrent();
  m_sessionClient->requestCreateSession(serverUrl, authToken, m_currentVin);
}
