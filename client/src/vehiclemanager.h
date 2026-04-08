#ifndef VEHICLEMANAGER_H
#define VEHICLEMANAGER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QNetworkReply>

/**
 * @brief 车辆管理器
 * 管理车辆列表、VIN 编号、当前选择的车辆
 */
class VehicleManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QStringList vehicleList READ vehicleList NOTIFY vehicleListChanged)
    Q_PROPERTY(QString currentVin READ currentVin WRITE setCurrentVin NOTIFY currentVinChanged)
    Q_PROPERTY(QString currentVehicleName READ currentVehicleName NOTIFY currentVehicleChanged)
    Q_PROPERTY(bool hasVehicles READ hasVehicles NOTIFY vehicleListChanged)
    Q_PROPERTY(QString lastSessionId READ lastSessionId NOTIFY sessionInfoChanged)
    Q_PROPERTY(QString lastWhipUrl READ lastWhipUrl NOTIFY sessionInfoChanged)
    Q_PROPERTY(QString lastWhepUrl READ lastWhepUrl NOTIFY sessionInfoChanged)
    Q_PROPERTY(QJsonObject lastControlConfig READ lastControlConfig NOTIFY sessionInfoChanged)

public:
    struct VehicleInfo {
        QString vin;
        QString name;
        QString model;
        QString status;
        QJsonObject metadata;
    };

    explicit VehicleManager(QObject *parent = nullptr);
    ~VehicleManager();

    QStringList vehicleList() const { return m_vehicleList; }
    QString currentVin() const { return m_currentVin; }
    void setCurrentVin(const QString &vin);
    QString currentVehicleName() const;
    bool hasVehicles() const { return !m_vehicleList.isEmpty(); }

    Q_INVOKABLE QJsonObject getVehicleInfo(const QString &vin) const;
    Q_INVOKABLE void refreshVehicleList(const QString &serverUrl, const QString &authToken);
    /** 测试用：注入一辆车到列表（便于 123/123 登录后选 VIN 123456789 进入主页面） */
    Q_INVOKABLE void addTestVehicle(const QString &vin, const QString &name);
    /** 为当前 VIN 创建会话（调用 backend POST /api/v1/vins/{vin}/sessions） */
    Q_INVOKABLE void startSessionForCurrentVin(const QString &serverUrl, const QString &authToken);
    
    QString lastSessionId() const { return m_lastSessionId; }
    QString lastWhipUrl() const { return m_lastWhipUrl; }
    QString lastWhepUrl() const { return m_lastWhepUrl; }
    QJsonObject lastControlConfig() const { return m_lastControlConfig; }

public slots:
    void loadVehicleList(const QString &serverUrl, const QString &authToken);
    void selectVehicle(const QString &vin);

signals:
    void vehicleListChanged();
    void currentVinChanged(const QString &vin);
    void currentVehicleChanged();
    void vehicleListLoaded(const QJsonArray &vehicles);
    void vehicleListLoadFailed(const QString &error);
    void vehicleSelected(const QString &vin, const QJsonObject &info);
    void sessionCreated(const QString &sessionId, const QString &whipUrl, const QString &whepUrl, const QJsonObject &controlConfig);
    void sessionCreateFailed(const QString &error);
    void sessionInfoChanged();

private slots:
    void onVehicleListReply(QNetworkReply *reply);
    void onSessionCreateReply(QNetworkReply *reply);

private:
    void updateVehicleList(const QJsonArray &vehicles);
    VehicleInfo parseVehicleInfo(const QJsonObject &json) const;

    /** 带超时的 GET 请求 */
    QNetworkReply* getWithTimeout(QNetworkAccessManager* nam, const QNetworkRequest& request, int timeoutMs = 10000);
    /** 带超时和请求体的 POST 请求 */
    QNetworkReply* postWithTimeout(QNetworkAccessManager* nam, const QNetworkRequest& request, const QByteArray& data, int timeoutMs = 10000);

    QStringList m_vehicleList;
    QMap<QString, VehicleInfo> m_vehicles;
    QString m_currentVin;
    QNetworkAccessManager *m_networkManager = nullptr;
    QNetworkReply *m_currentReply = nullptr;
    QNetworkReply *m_sessionReply = nullptr;
    
    // 会话信息
    QString m_lastSessionId;
    QString m_lastWhipUrl;
    QString m_lastWhepUrl;
    QJsonObject m_lastControlConfig;
};

#endif // VEHICLEMANAGER_H
