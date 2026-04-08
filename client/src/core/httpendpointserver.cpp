#include "httpendpointserver.h"
#include <QDateTime>
#include <QJsonArray>
#include <QHostAddress>
#include <QNetworkInterface>
#include <QUuid>
#include <atomic>

HttpEndpointServer::HttpEndpointServer(QObject* parent)
    : QObject(parent)
    , m_server(new QTcpServer(this))
{
    // 连接服务器信号
    connect(m_server, &QTcpServer::newConnection, this, [this]() {
        while (m_server->hasPendingConnections()) {
            QTcpSocket* socket = m_server->nextPendingConnection();
            if (socket) {
                // 使用 Qt::QueuedConnection 确保在主线程处理
                connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
                    handleRequest(socket);
                }, Qt::QueuedConnection);
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        }
    });

    connect(m_server, &QTcpServer::acceptError, this, [this](QAbstractSocket::SocketError socketError) {
        QString errorMsg = QString("Server accept error: %1").arg(socketError);
        qWarning() << "[Client][HTTP] " << errorMsg;
        emit serverError(errorMsg);
    });

    // 初始化内置指标
    m_numericMetrics["http_requests_total"] = 0;
    m_numericMetrics["http_errors_total"] = 0;
    m_numericMetrics["http_request_duration_seconds"] = 0;
}

HttpEndpointServer::~HttpEndpointServer()
{
    stop();
}

HttpEndpointServer& HttpEndpointServer::instance()
{
    static HttpEndpointServer server;
    return server;
}

bool HttpEndpointServer::start(int port)
{
    if (m_running) {
        qWarning() << "[Client][HTTP] Server already running on port" << m_port;
        return true;
    }

    m_port = port;

    // 尝试绑定到指定端口
    if (!m_server->listen(QHostAddress::Any, static_cast<quint16>(port))) {
        // 如果端口被占用，尝试其他端口
        if (!m_server->listen(QHostAddress::Any, 0)) {
            QString errorMsg = QString("Failed to listen on port %1: %2")
                .arg(port)
                .arg(m_server->errorString());
            qCritical() << "[Client][HTTP] " << errorMsg;
            emit serverError(errorMsg);
            return false;
        }
        m_port = m_server->serverPort();
        qWarning() << "[Client][HTTP] Port" << port << "in use, falling back to" << m_port;
    }

    m_running = true;
    qInfo() << "[Client][HTTP] Server started on port" << m_port;

    // 打印可用地址
    for (const QHostAddress& addr : QNetworkInterface::allAddresses()) {
        if (addr.protocol() == QAbstractSocket::IPv4Protocol && addr != QHostAddress::LocalHost) {
            qInfo() << "[Client][HTTP] Listening on http://" << addr.toString() << ":" << m_port;
        }
    }

    emit serverStarted(m_port);
    emit runningChanged(true);
    return true;
}

void HttpEndpointServer::stop()
{
    if (!m_running) {
        return;
    }

    m_server->close();
    m_running = false;

    qInfo() << "[Client][HTTP] Server stopped";
    emit runningChanged(false);
}

void HttpEndpointServer::registerHandler(const QString& path, Handler handler)
{
    std::lock_guard<std::mutex> lock(m_handlersMutex);
    m_handlers[path] = handler;
    qDebug() << "[Client][HTTP] Registered handler for" << path;
}

void HttpEndpointServer::registerJsonHandler(const QString& path, JsonHandler handler)
{
    std::lock_guard<std::mutex> lock(m_handlersMutex);
    m_jsonHandlers[path] = handler;
    qDebug() << "[Client][HTTP] Registered JSON handler for" << path;
}

void HttpEndpointServer::unregisterHandler(const QString& path)
{
    std::lock_guard<std::mutex> lock(m_handlersMutex);
    m_handlers.remove(path);
    m_jsonHandlers.remove(path);
    qDebug() << "[Client][HTTP] Unregistered handler for" << path;
}

void HttpEndpointServer::setMetric(const QString& name, double value)
{
    std::lock_guard<std::mutex> lock(m_metricsMutex);
    m_numericMetrics[name] = value;
}

void HttpEndpointServer::setMetric(const QString& name, const QString& value)
{
    std::lock_guard<std::mutex> lock(m_metricsMutex);
    m_stringMetrics[name] = value;
}

void HttpEndpointServer::incrementMetric(const QString& name, double delta)
{
    std::lock_guard<std::mutex> lock(m_metricsMutex);
    m_numericMetrics[name] += delta;
}

void HttpEndpointServer::setHealthStatus(const QString& component, bool healthy, const QString& message)
{
    std::lock_guard<std::mutex> lock(m_healthMutex);
    m_healthStatus[component] = qMakePair(healthy, message);
}

void HttpEndpointServer::setReadyStatus(bool ready, const QString& reason)
{
    std::lock_guard<std::mutex> lock(m_readyMutex);
    m_ready = ready;
    m_readyReason = reason;
}

void HttpEndpointServer::handleRequest(QTcpSocket* socket)
{
    // 记录开始时间
    auto startTime = std::chrono::steady_clock::now();

    // 读取所有数据
    QByteArray requestData;
    while (socket->bytesAvailable() > 0) {
        requestData.append(socket->readAll());
    }

    // 解析请求行
    QStringList lines = QString::fromUtf8(requestData).split("\r\n");
    if (lines.isEmpty()) {
        return;
    }

    QString requestLine = lines.value(0);
    QStringList parts = requestLine.split(' ');
    if (parts.size() < 2) {
        socket->write(buildHttpResponse(400, "text/plain", "Bad Request").toUtf8());
        socket->disconnectFromHost();
        return;
    }

    QString method = parts.value(0);
    QString rawPath = parts.value(1);
    QString path = urlDecode(rawPath);

    // 记录请求
    m_requestCount.fetch_add(1, std::memory_order_relaxed);
    incrementMetric("http_requests_total");

    qDebug() << "[Client][HTTP] Request:" << method << path;

    // 处理路由
    QString responseBody;
    int statusCode = 404;

    if (path == "/metrics") {
        responseBody = handleMetrics();
        statusCode = 200;
    } else if (path == "/metrics/json") {
        responseBody = handleMetricsJson();
        statusCode = 200;
    } else if (path == "/health") {
        responseBody = handleHealth();
        statusCode = 200;
    } else if (path == "/ready") {
        responseBody = handleReady();
        statusCode = 200;
    } else {
        // 检查自定义处理器
        {
            std::lock_guard<std::mutex> lock(m_handlersMutex);
            if (m_handlers.contains(path)) {
                responseBody = m_handlers[path]();
                statusCode = 200;
            } else if (m_jsonHandlers.contains(path)) {
                QJsonObject json = m_jsonHandlers[path]();
                responseBody = QJsonDocument(json).toJson();
                statusCode = 200;
            }
        }

        if (statusCode == 404) {
            responseBody = "Not Found";
        }
    }

    // 发送响应
    QString contentType = (path.endsWith("/json")) ? "application/json" : "text/plain; version=0.0.4";
    socket->write(buildHttpResponse(statusCode, contentType, responseBody).toUtf8());
    socket->disconnectFromHost();

    // 计算请求持续时间
    auto endTime = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration<double>(endTime - startTime).count();
    setMetric("http_request_duration_seconds", duration);

    emit requestReceived(method, path, statusCode);
}

QString HttpEndpointServer::buildHttpResponse(int statusCode, const QString& contentType, const QString& body)
{
    QString statusText;
    switch (statusCode) {
        case 200: statusText = "OK"; break;
        case 400: statusText = "Bad Request"; break;
        case 404: statusText = "Not Found"; break;
        case 500: statusText = "Internal Server Error"; break;
        default: statusText = "Unknown"; break;
    }

    QString response = QString("HTTP/1.1 %1 %2\r\n")
        .arg(statusCode)
        .arg(statusText);

    response += "Server: RemoteDrivingClient/1.0\r\n";
    response += "Content-Type: " + contentType + "\r\n";
    response += "Content-Length: " + QString::number(body.toUtf8().size()) + "\r\n";
    response += "Connection: close\r\n";
    response += "\r\n";
    response += body;

    return response;
}

QString HttpEndpointServer::handleMetrics()
{
    std::lock_guard<std::mutex> lock(m_metricsMutex);

    QString output;
    QString timestamp = QString::number(getPrometheusTimestamp(), 'f', 3);

    // 输出数值指标（Prometheus 格式）
    for (auto it = m_numericMetrics.constBegin(); it != m_numericMetrics.constEnd(); ++it) {
        output += QString("%1 %2 %3\n")
            .arg(it.key())
            .arg(it.value(), 0, 'g', 15)
            .arg(timestamp);
    }

    // 输出字符串指标（作为 gauge）
    for (auto it = m_stringMetrics.constBegin(); it != m_stringMetrics.constEnd(); ++it) {
        output += QString("# HELP %1 String metric\n").arg(it.key());
        output += QString("# TYPE %1 gauge\n").arg(it.key());
        // 字符串值存储为辅助指标
        output += QString("%1_value 1 %2\n").arg(it.key(), timestamp);
    }

    // 添加标准指标
    output += QString("http_endpoint_up 1 %1\n").arg(timestamp);
    output += QString("http_endpoint_port %1 %2\n").arg(m_port).arg(timestamp);

    return output;
}

QString HttpEndpointServer::handleMetricsJson()
{
    std::lock_guard<std::mutex> lock(m_metricsMutex);

    QJsonObject root;
    root["timestamp"] = QDateTime::currentSecsSinceEpoch();
    root["up"] = true;
    root["port"] = m_port;

    QJsonObject metrics;
    for (auto it = m_numericMetrics.constBegin(); it != m_numericMetrics.constEnd(); ++it) {
        metrics[it.key()] = it.value();
    }
    root["metrics"] = metrics;

    QJsonObject strings;
    for (auto it = m_stringMetrics.constBegin(); it != m_stringMetrics.constEnd(); ++it) {
        strings[it.key()] = it.value();
    }
    root["stringMetrics"] = strings;

    return QJsonDocument(root).toJson();
}

QString HttpEndpointServer::handleHealth()
{
    std::lock_guard<std::mutex> lock(m_healthMutex);

    QJsonObject root;
    root["status"] = "ok";
    root["timestamp"] = QDateTime::currentSecsSinceEpoch();
    root["uptime_seconds"] = QDateTime::currentSecsSinceEpoch();

    QJsonArray components;
    bool allHealthy = true;

    for (auto it = m_healthStatus.constBegin(); it != m_healthStatus.constEnd(); ++it) {
        QJsonObject comp;
        comp["name"] = it.key();
        comp["healthy"] = it.value().first;
        comp["message"] = it.value().second;
        components.append(comp);

        if (!it.value().first) {
            allHealthy = false;
        }
    }

    root["components"] = components;
    root["all_healthy"] = allHealthy;

    if (!allHealthy) {
        root["status"] = "degraded";
    }

    return QJsonDocument(root).toJson();
}

QString HttpEndpointServer::handleReady()
{
    std::lock_guard<std::mutex> lock(m_readyMutex);

    QJsonObject root;
    root["ready"] = m_ready;
    root["timestamp"] = QDateTime::currentSecsSinceEpoch();

    if (!m_readyReason.isEmpty()) {
        root["reason"] = m_readyReason;
    }

    if (m_ready) {
        root["status"] = "ready";
    } else {
        root["status"] = "not_ready";
    }

    // Kubernetes 就绪探测响应
    root["kind"] = "Pod";
    root["apiVersion"] = "v1";

    return QJsonDocument(root).toJson();
}

QString HttpEndpointServer::urlDecode(const QString& encoded)
{
    QString decoded = encoded;
    decoded.replace("%20", " ");
    decoded.replace("%3F", "?");
    decoded.replace("%3D", "=");
    decoded.replace("%26", "&");
    decoded.replace("%2F", "/");
    decoded.replace("%25", "%");
    return decoded;
}

double HttpEndpointServer::getPrometheusTimestamp()
{
    // 返回毫秒级时间戳
    return QDateTime::currentMSecsSinceEpoch() / 1000.0;
}