#include "vehiclemanager.h"
#include <QJsonDocument>
#include <QDebug>
#include <QString>

VehicleManager::VehicleManager(QObject *parent)
    : QObject(parent)
    , m_networkManager(new QNetworkAccessManager(this))
{
}

VehicleManager::~VehicleManager()
{
}

void VehicleManager::setCurrentVin(const QString &vin)
{
    if (m_currentVin != vin && m_vehicles.contains(vin)) {
        m_currentVin = vin;
        qDebug() << "[CLIENT][选车] 当前车辆 VIN=" << m_currentVin << "name=" << m_vehicles[vin].name;
        emit currentVinChanged(m_currentVin);
        emit currentVehicleChanged();

        VehicleInfo info = m_vehicles[vin];
        QJsonObject json;
        json["vin"] = info.vin;
        json["name"] = info.name;
        json["model"] = info.model;
        json["status"] = info.status;
        json["metadata"] = info.metadata;
        emit vehicleSelected(vin, json);
    }
}

QString VehicleManager::currentVehicleName() const
{
    if (m_currentVin.isEmpty() || !m_vehicles.contains(m_currentVin)) {
        return QString();
    }
    return m_vehicles[m_currentVin].name;
}

QJsonObject VehicleManager::getVehicleInfo(const QString &vin) const
{
    if (!m_vehicles.contains(vin)) {
        return QJsonObject();
    }
    
    VehicleInfo info = m_vehicles[vin];
    QJsonObject json;
    json["vin"] = info.vin;
    json["name"] = info.name;
    json["model"] = info.model;
    json["status"] = info.status;
    json["metadata"] = info.metadata;
    return json;
}

void VehicleManager::loadVehicleList(const QString &serverUrl, const QString &authToken)
{
    refreshVehicleList(serverUrl, authToken);
}

void VehicleManager::refreshVehicleList(const QString &serverUrl, const QString &authToken)
{
    if (m_currentReply) {
        m_currentReply->abort();
        m_currentReply->deleteLater();
    }

    QUrl vehiclesUrl(serverUrl + "/api/v1/vins");
    qDebug().noquote() << "[Client][车辆列表] 请求 GET" << vehiclesUrl.toString() << " hasToken=" << !authToken.isEmpty() << " tokenLen=" << authToken.size();

    QNetworkRequest request(vehiclesUrl);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    if (!authToken.isEmpty()) {
        request.setRawHeader("Authorization", ("Bearer " + authToken).toUtf8());
    }

    m_currentReply = m_networkManager->get(request);
    QNetworkReply *reply = m_currentReply;

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onVehicleListReply(reply);
    });
    connect(reply, &QNetworkReply::errorOccurred, this, [this, reply](QNetworkReply::NetworkError error) {
        Q_UNUSED(error)
        qDebug().noquote() << "[Client][车辆列表] request error err=E_BACKEND_UNREACHABLE cause=" << reply->errorString();
        emit vehicleListLoadFailed(tr("网络错误: %1").arg(reply->errorString()));
        reply->deleteLater();
        if (m_currentReply == reply)
            m_currentReply = nullptr;
    });
}

void VehicleManager::onVehicleListReply(QNetworkReply *reply)
{
    if (m_currentReply == reply)
        m_currentReply = nullptr;

    if (!reply) {
        qDebug().noquote() << "[Client][车辆列表] onVehicleListReply: reply=null";
        return;
    }
    int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() != QNetworkReply::NoError) {
        qDebug().noquote() << "[Client][车辆列表] HTTP error statusCode=" << statusCode << " err=E_BACKEND_UNREACHABLE cause=" << reply->errorString();
        emit vehicleListLoadFailed(tr("获取车辆列表失败: %1").arg(reply->errorString()));
        reply->deleteLater();
        return;
    }

    QByteArray data = reply->readAll();
    reply->deleteLater();
    qDebug().noquote() << "[Client][车辆列表] onVehicleListReply: HTTP" << statusCode << " bodySize=" << data.size();

    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(data, &error);
    if (error.error != QJsonParseError::NoError) {
        qDebug().noquote() << "[Client][车辆列表] response not JSON err=E_BACKEND_UNREACHABLE offset=" << error.offset;
        emit vehicleListLoadFailed(tr("服务器响应格式错误"));
        return;
    }

    QJsonObject json = doc.object();
    QJsonArray vehicles;

    if (json.contains("vins") && json["vins"].isArray()) {
        QJsonArray vinsArray = json["vins"].toArray();
        for (const QJsonValue &v : vinsArray) {
            if (v.isString()) {
                QJsonObject obj;
                obj["vin"] = v.toString();
                vehicles.append(obj);
            }
        }
    } else if (json.contains("data") && json["data"].isArray()) {
        vehicles = json["data"].toArray();
    } else if (json.contains("vehicles") && json["vehicles"].isArray()) {
        vehicles = json["vehicles"].toArray();
    } else if (doc.isArray()) {
        vehicles = doc.array();
    } else {
        qDebug().noquote() << "[Client][车辆列表] onVehicleListReply: 无效格式 keys=" << json.keys().join(QLatin1Char(',')) << " → vehicleListLoadFailed";
        emit vehicleListLoadFailed(tr("无效的车辆列表格式"));
        return;
    }

    updateVehicleList(vehicles);
    QStringList vinList;
    for (const QJsonValue &v : vehicles) {
        if (v.isObject() && v.toObject().contains("vin"))
            vinList << v.toObject().value("vin").toString();
    }
    qDebug().noquote() << "[Client][车辆列表] 已加载 count=" << vinList.size() << " vins=" << vinList.join(QLatin1Char(','));
    emit vehicleListLoaded(vehicles);
}

void VehicleManager::updateVehicleList(const QJsonArray &vehicles)
{
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
    
    emit vehicleListChanged();
}

VehicleManager::VehicleInfo VehicleManager::parseVehicleInfo(const QJsonObject &json) const
{
    VehicleInfo info;
    info.vin = json["vin"].toString();
    info.name = json.contains("name") ? json["name"].toString() : info.vin;
    info.model = json.contains("model") ? json["model"].toString() : QString();
    info.status = json.contains("status") ? json["status"].toString() : "unknown";
    
    if (json.contains("metadata") && json["metadata"].isObject()) {
        info.metadata = json["metadata"].toObject();
    }
    
    return info;
}

void VehicleManager::selectVehicle(const QString &vin)
{
    setCurrentVin(vin);
}

void VehicleManager::addTestVehicle(const QString &vin, const QString &name)
{
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

void VehicleManager::startSessionForCurrentVin(const QString &serverUrl, const QString &authToken)
{
    if (m_currentVin.isEmpty()) {
        qWarning() << "[Client][Session] create session failed err=E_CONFIG_MISSING (no vehicle selected)";
        emit sessionCreateFailed("未选择车辆");
        return;
    }
    
    if (m_sessionReply) {
        m_sessionReply->abort();
        m_sessionReply->deleteLater();
    }
    
    // 构建创建会话 API URL: POST /api/v1/vins/{vin}/sessions
    QUrl sessionUrl(serverUrl + "/api/v1/vins/" + m_currentVin + "/sessions");
    qDebug() << "[CLIENT][Session] 创建会话 vin=" << m_currentVin << " url=" << sessionUrl.toString();
    
    QNetworkRequest request(sessionUrl);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    if (!authToken.isEmpty()) {
        request.setRawHeader("Authorization", ("Bearer " + authToken).toUtf8());
    }
    
    m_sessionReply = m_networkManager->post(request, QByteArray());
    QNetworkReply *reply = m_sessionReply;
    
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onSessionCreateReply(reply);
    });
    
    connect(reply, &QNetworkReply::errorOccurred, this, [this, reply](QNetworkReply::NetworkError error) {
        Q_UNUSED(error)
        // 503 等错误时响应体可能仍有 backend 返回的 details，稍后在 onSessionCreateReply 中统一读取并打印
        qWarning() << "[Client][Session] request error vin=" << m_currentVin << " err=E_BACKEND_UNREACHABLE cause=" << reply->errorString();
        emit sessionCreateFailed("网络错误: " + reply->errorString());
        reply->deleteLater();
        if (m_sessionReply == reply)
            m_sessionReply = nullptr;
    });
}

void VehicleManager::onSessionCreateReply(QNetworkReply *reply)
{
    if (!reply)
        return;
    if (reply->error() != QNetworkReply::NoError) {
        int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QByteArray body = reply->readAll();
        qWarning() << "[Client][Session] create session failed vin=" << m_currentVin
                   << " err=E_BACKEND_UNREACHABLE cause=" << reply->errorString() << " httpCode=" << code
                   << " body=" << (body.isEmpty() ? "(empty)" : QString::fromUtf8(body.constData(), body.size()));
        emit sessionCreateFailed("创建会话失败: " + reply->errorString());
        reply->deleteLater();
        return;
    }
    
    QByteArray data = reply->readAll();
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(data, &error);
    
    if (error.error != QJsonParseError::NoError) {
        emit sessionCreateFailed("服务器响应格式错误");
        reply->deleteLater();
        return;
    }
    
    QJsonObject json = doc.object();
    
    // 解析响应: {"sessionId":"...", "media":{"whip":"...","whep":"..."}, "control":{...}}
    QString sessionId = json["sessionId"].toString();
    if (sessionId.isEmpty()) {
        emit sessionCreateFailed("响应中缺少 sessionId");
        reply->deleteLater();
        return;
    }
    
    QString whipUrl, whepUrl;
    QJsonObject controlConfig;
    
    if (json.contains("media") && json["media"].isObject()) {
        QJsonObject media = json["media"].toObject();
        whipUrl = media["whip"].toString();
        whepUrl = media["whep"].toString();
    }
    
    if (json.contains("control") && json["control"].isObject()) {
        controlConfig = json["control"].toObject();
    }
    
    m_lastSessionId = sessionId;
    m_lastWhipUrl = whipUrl;
    m_lastWhepUrl = whepUrl;
    m_lastControlConfig = controlConfig;
    
    qDebug() << "[CLIENT][Session] 会话创建成功 vin=" << m_currentVin
             << " sessionId=" << sessionId
             << " whepUrl=" << (whepUrl.length() > 80 ? whepUrl.left(80) + "..." : whepUrl);
    if (!controlConfig.isEmpty()) {
        QString broker = controlConfig.value(QStringLiteral("mqtt_broker_url")).toString();
        qDebug() << "[CLIENT][Session] control.mqtt_broker_url=" << (broker.isEmpty() ? "(空，连接车端时需手动填 MQTT)" : broker);
    }
    
    emit sessionInfoChanged();
    emit sessionCreated(sessionId, whipUrl, whepUrl, controlConfig);

    // 清除 reply 指针，防止后续请求时误判为"已有进行中请求"
    if (m_sessionReply == reply)
        m_sessionReply = nullptr;
    reply->deleteLater();
}
