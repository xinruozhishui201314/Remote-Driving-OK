#include "mqttcontroller.h"
#include "webrtcclient.h"
#include "core/tracing.h"
#include <QJsonDocument>
#include <QDebug>
#include <QUrl>
#include <QDateTime>
#include <QElapsedTimer>
#include <QProcess>
#include <QTimer>
#include <QRegularExpression>

// 生产构建：定义 ENABLE_MQTT_PAHO 时使用 Paho async_client（见 CMakeLists / connectToBroker）。

MqttController::MqttController(QObject *parent)
    : QObject(parent)
{
#ifdef ENABLE_MQTT_PAHO
    // Paho MQTT 客户端将在 connectToBroker 时初始化
#endif
    // 初始化重连定时器（指数退避）
    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setSingleShot(true);
    connect(m_reconnectTimer, &QTimer::timeout, this, &MqttController::onReconnectTimer);
}

bool MqttController::isConnected() const
{
    return m_isConnected || (m_webrtcClient && m_webrtcClient->isConnected());
}

MqttController::~MqttController()
{
    disconnectFromBroker();
#ifndef ENABLE_MQTT_PAHO
    if (m_mosquittoSubProcess) {
        if (m_mosquittoSubProcess->state() != QProcess::NotRunning) {
            m_mosquittoSubProcess->terminate();
            m_mosquittoSubProcess->waitForFinished(3000);
            if (m_mosquittoSubProcess->state() != QProcess::NotRunning) {
                m_mosquittoSubProcess->kill();
            }
        }
        m_mosquittoSubProcess->deleteLater();
        m_mosquittoSubProcess = nullptr;
    }
#endif
}

void MqttController::setBrokerUrl(const QString &url)
{
    if (m_brokerUrl != url) {
        m_brokerUrl = url;
        emit brokerUrlChanged(m_brokerUrl);
    }
}

void MqttController::setClientId(const QString &id)
{
    if (m_clientId != id) {
        m_clientId = id;
        emit clientIdChanged(m_clientId);
    }
}

void MqttController::setControlTopic(const QString &topic)
{
    if (m_controlTopic != topic) {
        m_controlTopic = topic;
        emit controlTopicChanged(m_controlTopic);
    }
}

void MqttController::setStatusTopic(const QString &topic)
{
    if (m_statusTopic != topic) {
        m_statusTopic = topic;
        emit statusTopicChanged(m_statusTopic);
    }
}

void MqttController::setCurrentVin(const QString &vin)
{
    if (m_currentVin != vin) {
        m_currentVin = vin;
        qDebug() << "MQTT Controller: Current VIN set to" << vin;
        
        // 如果已连接，重新订阅车辆特定主题
        if (m_isConnected) {
            onConnected();
        }
    }
}

void MqttController::connectToBroker()
{
    if (m_isConnected) {
        qDebug() << "[CLIENT][MQTT] 已连接，跳过 connectToBroker";
        return;
    }

    qDebug() << "[CLIENT][MQTT] 连接 broker vin=" << m_currentVin << " brokerUrl=" << m_brokerUrl;
    
#ifdef ENABLE_MQTT_PAHO
    try {
        // 解析 broker URL
        QUrl url(m_brokerUrl);
        QString host = url.host();
        int port = url.port(1883);
        QString address = QString("%1:%2").arg(host).arg(port);
        
        // 创建 MQTT 客户端
        m_client = std::make_unique<mqtt::async_client>(
            address.toStdString(),
            m_clientId.toStdString()
        );
        
        // 设置回调
        m_client->set_connected_handler([this](const std::string&) {
            qDebug() << "[CLIENT][MQTT] 连接成功 vin=" << m_currentVin;
            QMetaObject::invokeMethod(this, "onConnected", Qt::QueuedConnection);
        });
        
        m_client->set_connection_lost_handler([this](const std::string&) {
            qWarning() << "[CLIENT][MQTT] 连接丢失 vin=" << m_currentVin;
            QMetaObject::invokeMethod(this, "onDisconnected", Qt::QueuedConnection);
        });
        
        m_client->set_message_callback([this](mqtt::const_message_ptr msg) {
            QString topic = QString::fromStdString(msg->get_topic());
            QByteArray payload(msg->to_string().data(), msg->to_string().length());
            qDebug() << "[MQTT] 消息回调触发，主题:" << topic << "大小:" << payload.size() << "bytes";
            QMetaObject::invokeMethod(this, "onMessageReceived", Qt::QueuedConnection,
                Q_ARG(QByteArray, topic.toUtf8()),
                Q_ARG(QByteArray, payload));
        });
        
        // 连接选项
        mqtt::connect_options connOpts;
        connOpts.set_clean_session(true);
        connOpts.set_automatic_reconnect(true);
        // 设置 Keep-Alive 间隔（60秒），防止 NAT 超时断开连接
        connOpts.set_keep_alive_interval(60);
        // 设置连接超时（10秒）
        connOpts.set_connect_timeout(10);
        
        // 纯异步连接：connected_handler 回调已注册，不阻塞主线程
        m_client->connect(connOpts);
        
    } catch (const mqtt::exception& exc) {
        QString error = QString("MQTT connection error: %1").arg(exc.what());
        qWarning() << error;
        emit errorOccurred(error);
    }
#else
    // 无 Paho 时使用 mosquitto_sub 接收消息
    qDebug() << "[MQTT] 使用 mosquitto_sub 接收消息（ENABLE_MQTT_PAHO 未定义）";
    QUrl url(m_brokerUrl);
    QString host = url.host();
    int port = url.port(1883);
    if (host.isEmpty()) {
        qWarning() << "[MQTT] brokerUrl 无效，无法连接";
        emit errorOccurred("MQTT broker URL 无效");
        return;
    }
    
    // 延迟连接，确保状态更新
    QTimer::singleShot(500, this, [this, host, port]() {
        updateConnectionStatus(true);
        onConnected();
    });
#endif
}

void MqttController::disconnectFromBroker()
{
    if (!m_isConnected) {
        return;
    }

    qDebug() << "Disconnecting from MQTT broker";
    
#ifdef ENABLE_MQTT_PAHO
    try {
        if (m_client) {
            // 异步断开：不阻塞主线程；用 QTimer 延迟销毁客户端给 paho 内部清理时间
            auto *rawClient = m_client.release();
            try { rawClient->disconnect(); } catch (...) {}
            QTimer::singleShot(2000, [rawClient]() {
                delete rawClient;
            });
        }
    } catch (const mqtt::exception& exc) {
        qWarning() << "[CLIENT][MQTT] disconnect error:" << exc.what();
    }
#else
    // 停止 mosquitto_sub 进程（异步终止，不阻塞主线程）
    if (m_mosquittoSubProcess) {
        QProcess *proc = m_mosquittoSubProcess;
        m_mosquittoSubProcess = nullptr;
        if (proc->state() != QProcess::NotRunning) {
            proc->terminate();
            // 给 1s 优雅退出，超时后 kill，全程不阻塞主线程
            QTimer::singleShot(1000, proc, [proc]() {
                if (proc->state() != QProcess::NotRunning) {
                    proc->kill();
                }
                proc->deleteLater();
            });
        } else {
            proc->deleteLater();
        }
    }
#endif
    
    updateConnectionStatus(false);
    onDisconnected();
}

void MqttController::sendSteeringCommand(double angle)
{
    QJsonObject cmd;
    cmd["type"] = "steering";
    cmd["value"] = qBound(-1.0, angle, 1.0);
    cmd["timestampMs"] = QDateTime::currentMSecsSinceEpoch();
    sendControlCommand(cmd);
}

void MqttController::sendThrottleCommand(double throttle)
{
    QJsonObject cmd;
    cmd["type"] = "throttle";
    cmd["value"] = qBound(0.0, throttle, 1.0);
    cmd["timestampMs"] = QDateTime::currentMSecsSinceEpoch();
    sendControlCommand(cmd);
}

void MqttController::sendBrakeCommand(double brake)
{
    QJsonObject cmd;
    cmd["type"] = "brake";
    cmd["value"] = qBound(0.0, brake, 1.0);
    cmd["timestampMs"] = QDateTime::currentMSecsSinceEpoch();
    cmd["seq"] = static_cast<int>(++m_seq);
    sendControlCommand(cmd);
}

void MqttController::sendSpeedCommand(double speed)
{
    QJsonObject cmd;
    cmd["type"] = "target_speed";
    cmd["value"] = qBound(0.0, speed, 100.0);
    cmd["timestampMs"] = QDateTime::currentMSecsSinceEpoch();
    sendControlCommand(cmd);
}

void MqttController::sendEmergencyStopCommand(bool enable)
{
    qWarning() << "[CLIENT][EMERGENCY_STOP] enable=" << enable;
    QJsonObject cmd;
    cmd["type"] = "emergency_stop";
    cmd["enable"] = enable;
    cmd["timestampMs"] = QDateTime::currentMSecsSinceEpoch();
    sendControlCommand(cmd);
}

void MqttController::sendGearCommand(int gear)
{
    qDebug() << "[CLIENT][GEAR] gear=" << gear;
    QJsonObject cmd;
    cmd["type"] = "gear";
    cmd["value"] = gear;
    cmd["timestampMs"] = QDateTime::currentMSecsSinceEpoch();
    sendControlCommand(cmd);
}

void MqttController::sendSweepCommand(const QString &sweepType, bool active)
{
    qDebug() << "[CLIENT][SWEEP] type=" << sweepType << "active=" << active;
    QJsonObject cmd;
    cmd["type"] = "sweep";
    cmd["sweepType"] = sweepType;
    cmd["active"] = active;
    cmd["timestampMs"] = QDateTime::currentMSecsSinceEpoch();
    sendControlCommand(cmd);
}

void MqttController::sendLightCommand(const QString &lightType, bool active)
{
    qDebug() << "[LIGHT] 发送灯光命令: type=" << lightType << "active=" << active;
    
    if (!m_isConnected) {
        qWarning() << "[LIGHT] MQTT 未连接";
        return;
    }
    
    QJsonObject cmd;
    cmd["type"] = "mode";
    cmd["subType"] = "light";
    cmd["lightType"] = lightType;
    cmd["active"] = active;
    sendControlCommand(cmd);
}

void MqttController::sendUiEnvelopeJson(const QString &type, const QVariantMap &payload)
{
    if (Tracing::instance().currentTraceId().isEmpty()) {
        Tracing::instance().setCurrentTraceId(Tracing::generateTraceId());
    }
    QJsonObject obj;
    obj[QStringLiteral("schemaVersion")] = QStringLiteral("1.0");
    obj[QStringLiteral("vin")] = m_currentVin;
    obj[QStringLiteral("sessionId")] = QString();
    obj[QStringLiteral("type")] = type;
    obj[QStringLiteral("payload")] = QJsonObject::fromVariantMap(payload);
    obj[QStringLiteral("timestampMs")] = QDateTime::currentMSecsSinceEpoch();
    obj[QStringLiteral("trace_id")] = Tracing::instance().currentTraceId();
    sendControlCommand(obj);
}

void MqttController::sendControlCommand(const QJsonObject &command)
{
    const bool dataChannelOk = m_webrtcClient && m_webrtcClient->isConnected();
    if (!m_isConnected && !dataChannelOk) {
        qWarning() << "[CLIENT][Control] ✗ 无法发送控制指令: MQTT 与 DataChannel 均未连接";
        emit errorOccurred("控制通道未连接（请连接车端或视频流）");
        return;
    }

    // 添加 VIN 到命令中
    QJsonObject cmdWithVin = command;
    if (!m_currentVin.isEmpty() && !cmdWithVin.contains("vin")) {
        cmdWithVin["vin"] = m_currentVin;
    }
    
    // 添加时间戳和序列号（防重放）
    if (!cmdWithVin.contains("timestampMs")) {
        cmdWithVin["timestampMs"] = QDateTime::currentMSecsSinceEpoch();
    }
    // 添加序列号
    if (!cmdWithVin.contains("seq")) {
        cmdWithVin["seq"] = static_cast<int>(++m_seq);
    }

    QString vinInPayload = cmdWithVin.value("vin").toString();
    if (vinInPayload.isEmpty()) {
        qWarning() << "[CLIENT][Control] ⚠ 控制指令 VIN 为空 type=" << cmdWithVin.value("type").toString();
    }
    // 自动选择最佳通道发送
    sendControlCommandAuto(cmdWithVin);
}

void MqttController::sendControlCommandAuto(const QJsonObject &command)
{
    const bool dataChannelOk = m_webrtcClient && m_webrtcClient->isConnected();
    ChannelType useChannel = m_preferredChannel;

    if (useChannel == ChannelType::AUTO) {
        useChannel = dataChannelOk ? ChannelType::DATA_CHANNEL : ChannelType::MQTT;
    }

    if (useChannel == ChannelType::DATA_CHANNEL && dataChannelOk) {
        sendControlCommandViaDataChannel(command);
        m_activeChannel = ChannelType::DATA_CHANNEL;
        m_dataChannelCount++;
        return;
    }
    if (useChannel == ChannelType::WEBSOCKET) {
        // WebSocket 暂未实现，回退到 MQTT
        qDebug() << "[CLIENT][Control] WebSocket 通道暂未实现，回退到 MQTT";
        useChannel = ChannelType::MQTT;
    }
    if (useChannel == ChannelType::MQTT && m_isConnected) {
        sendControlCommandViaMqtt(command);
        m_activeChannel = ChannelType::MQTT;
        m_mqttCount++;
        return;
    }
    // 回退：首选 DataChannel 不可用时用 MQTT
    if (m_isConnected) {
        qDebug() << "[CLIENT][Control] 使用 MQTT 发送（DataChannel 不可用或未选）";
        sendControlCommandViaMqtt(command);
        m_activeChannel = ChannelType::MQTT;
        m_mqttCount++;
    } else {
        qWarning() << "[CLIENT][Control] 无可用通道，未发送";
    }
}

void MqttController::sendControlCommandViaDataChannel(const QJsonObject &command)
{
    if (!m_webrtcClient || !m_webrtcClient->isConnected()) {
        qWarning() << "[CLIENT][Control] DataChannel 未连接，无法发送";
        return;
    }
    QByteArray payload = QJsonDocument(command).toJson(QJsonDocument::Compact);
    m_webrtcClient->sendDataChannelMessage(payload);
}

void MqttController::sendControlCommandViaMqtt(const QJsonObject &command)
{
    if (!m_isConnected) {
        qWarning() << "[CLIENT][Control] MQTT 未连接，无法发送";
        return;
    }
    publishMessage(m_controlTopic, command);
}

void MqttController::setWebRtcClient(WebRtcClient *client)
{
    if (m_webrtcClient == client) {
        return;
    }
    // 断开旧连接，防止快速重连时信号槽累积导致槽被多次触发
    if (m_webrtcClient) {
        disconnect(m_webrtcClient, &WebRtcClient::connectionStatusChanged, this, nullptr);
    }
    m_webrtcClient = client;
    if (m_webrtcClient) {
        connect(m_webrtcClient, &WebRtcClient::connectionStatusChanged, this, [this]() {
            emit connectionStatusChanged(isConnected());
        });
    }
    qDebug() << "[CLIENT][Control] WebRTC 客户端已设置，DataChannel 可用时将优先使用";
}

void MqttController::setPreferredChannel(const QString &channelType)
{
    QString lower = channelType.toLower().trimmed();
    ChannelType next = ChannelType::AUTO;
    if (lower == "data_channel" || lower == "datachannel" || lower == "webrtc") {
        next = ChannelType::DATA_CHANNEL;
    } else if (lower == "mqtt") {
        next = ChannelType::MQTT;
    } else if (lower == "websocket" || lower == "ws") {
        next = ChannelType::WEBSOCKET;
    }
    if (m_preferredChannel != next) {
        m_preferredChannel = next;
        qDebug() << "[CLIENT][Control] 首选通道设置为:" << channelType;
    }
}

QString MqttController::getActiveChannelType() const
{
    switch (m_activeChannel) {
    case ChannelType::AUTO:        return QStringLiteral("AUTO");
    case ChannelType::DATA_CHANNEL: return QStringLiteral("DATA_CHANNEL");
    case ChannelType::MQTT:         return QStringLiteral("MQTT");
    case ChannelType::WEBSOCKET:   return QStringLiteral("WEBSOCKET");
    default:                       return QStringLiteral("UNKNOWN");
    }
}

void MqttController::requestStreamStart()
{
    const bool anyChannel = m_isConnected || (m_webrtcClient && m_webrtcClient->isConnected());
    qDebug() << "[CLIENT][Control] requestStreamStart m_isConnected=" << m_isConnected
             << " dataChannelOk=" << (m_webrtcClient && m_webrtcClient->isConnected())
             << " m_currentVin=" << m_currentVin;
    if (!anyChannel) {
        qWarning() << "[CLIENT][Control] ✗ 未连接，无法发送 start_stream（请先连接车端或视频流）";
        return;
    }
    QJsonObject cmd;
    cmd["type"] = "start_stream";
    cmd["timestampMs"] = QDateTime::currentMSecsSinceEpoch();
    sendControlCommand(cmd);
    qDebug() << "[CLIENT][MQTT] ✓ 已调用 sendControlCommand(start_stream)，等待车端推流（约 5~15s）";
}

void MqttController::requestStreamStop()
{
    if (!m_isConnected) {
        qDebug() << "[CLIENT][MQTT] 未连接，跳过 stop_stream";
        return;
    }
    QJsonObject cmd;
    cmd["type"] = "stop_stream";
    cmd["timestampMs"] = QDateTime::currentMSecsSinceEpoch();
    sendControlCommand(cmd);
    qDebug() << "[CLIENT][MQTT] 已请求车端/仿真停止推流 stop_stream vin=" << m_currentVin;
}

void MqttController::requestRemoteControl(bool enable)
{
    // ★ 精确定位：便于 grep [REMOTE_CONTROL][SEND]
    qDebug() << "[REMOTE_CONTROL][SEND] >>> 即将发送 remote_control <<< enable=" << (enable ? "true" : "false")
             << " topic=" << m_controlTopic << " vin=" << m_currentVin;
    qDebug() << "[REMOTE_CONTROL] ========== [MQTT] 请求远驾接管 ==========";
    qDebug() << "[REMOTE_CONTROL] enable=" << (enable ? "true" : "false")
             << " m_isConnected=" << (m_isConnected ? "true" : "false")
             << " m_currentVin=" << m_currentVin
             << " m_controlTopic=" << m_controlTopic;

    const bool anyChannel = m_isConnected || (m_webrtcClient && m_webrtcClient->isConnected());
    if (!anyChannel) {
        qWarning() << "[REMOTE_CONTROL][SEND] ✗ 未发送：控制通道未连接";
        return;
    }
    QJsonObject cmd;
    cmd["type"] = "remote_control";
    cmd["enable"] = enable;
    cmd["timestampMs"] = QDateTime::currentMSecsSinceEpoch();
    sendControlCommand(cmd);
    qDebug() << "[REMOTE_CONTROL][SEND] <<< 已调用 publishMessage(topic=" << m_controlTopic << ") 请查看上方是否有 ✓ 发布成功";
}

void MqttController::sendDriveCommand(double steering, double throttle, double brake, int gear)
{
    QJsonObject cmd;
    cmd["type"] = "drive";
    cmd["steering"] = qBound(-1.0, steering, 1.0);
    cmd["throttle"] = qBound(0.0, throttle, 1.0);
    cmd["brake"] = qBound(0.0, brake, 1.0);
    cmd["gear"] = gear;
    cmd["timestampMs"] = QDateTime::currentMSecsSinceEpoch();
    sendControlCommand(cmd);
}

void MqttController::publishMessage(const QString &topic, const QJsonObject &payload)
{
    QJsonDocument doc(payload);
    QByteArray data = doc.toJson(QJsonDocument::Compact);
    
#ifdef ENABLE_MQTT_PAHO
    try {
        if (m_client && m_client->is_connected()) {
            mqtt::message_ptr msg = mqtt::make_message(
                topic.toStdString(),
                std::string(data.data(), data.length())
            );
            // QoS 0：fire-and-forget，不阻塞主线程；控制指令容忍偶发丢包
            msg->set_qos(0);
            m_client->publish(msg);  // 纯异步，不调用 ->wait()
        } else {
            qWarning() << "[CLIENT][MQTT] ✗ Paho 未连接，未发送 topic=" << topic;
        }
    } catch (const mqtt::exception& exc) {
        qWarning() << "[CLIENT][MQTT] ✗ Paho 发布失败 topic=" << topic << " error=" << exc.what();
        emit errorOccurred(QString("MQTT publish error: %1").arg(exc.what()));
    }
#else
    // 无 Paho fallback：startDetached 不阻塞主线程（fire-and-forget）
    QUrl url(m_brokerUrl);
    QString host = url.host();
    int port = url.port(1883);
    if (host.isEmpty()) {
        qWarning() << "[CLIENT][MQTT] ✗ brokerUrl 无效，无法发送 topic=" << topic;
        return;
    }
    QStringList args;
    args << "-h" << host << "-p" << QString::number(port)
         << "-t" << topic << "-m" << QString::fromUtf8(data);
    bool ok = QProcess::startDetached(QStringLiteral("mosquitto_pub"), args);
    if (!ok) {
        qWarning() << "[CLIENT][MQTT] ✗ mosquitto_pub startDetached 失败 topic=" << topic
                   << "(请安装 mosquitto-clients 或编译 ENABLE_MQTT_PAHO)";
    }
#endif
}

void MqttController::onConnected()
{
    m_isConnected = true;
    // 重置重连计数器
    m_reconnectAttempts = 0;
    m_reconnectScheduled = false;
    qDebug() << "[CLIENT][MQTT] 已连接 m_currentVin=" << m_currentVin
             << " m_controlTopic=" << m_controlTopic
             << "（发送 start_stream/remote_control 时将带此 VIN，若为空请先选车）";
#ifdef ENABLE_MQTT_PAHO
    try {
        if (m_client) {
            // 异步订阅，不调用 ->wait()，不阻塞主线程
            qDebug() << "[CLIENT][MQTT] 订阅状态主题:" << m_statusTopic;
            m_client->subscribe(m_statusTopic.toStdString(), 0);  // QoS 0，低延迟
            
            if (!m_currentVin.isEmpty()) {
                QString vehicleStatusTopic = QString("vehicle/%1/status").arg(m_currentVin);
                qDebug() << "[CLIENT][MQTT] 订阅车辆主题:" << vehicleStatusTopic;
                m_client->subscribe(vehicleStatusTopic.toStdString(), 0);
            }
        } else {
            qWarning() << "[CLIENT][MQTT] 客户端为空，无法订阅";
        }
    } catch (const mqtt::exception& exc) {
        qWarning() << "[CLIENT][MQTT] 订阅错误:" << exc.what();
    }
#else
    // 使用 mosquitto_sub 订阅状态主题
    qDebug() << "[MQTT] 使用 mosquitto_sub 订阅状态主题（ENABLE_MQTT_PAHO 未定义）";
    
    // 如果已有进程在运行，先异步停止它（不阻塞主线程）
    if (m_mosquittoSubProcess) {
        QProcess *oldProc = m_mosquittoSubProcess;
        m_mosquittoSubProcess = nullptr;
        if (oldProc->state() != QProcess::NotRunning) {
            oldProc->terminate();
            QTimer::singleShot(1000, oldProc, [oldProc]() {
                if (oldProc->state() != QProcess::NotRunning) {
                    oldProc->kill();
                }
                oldProc->deleteLater();
            });
        } else {
            oldProc->deleteLater();
        }
    }
    
    QUrl url(m_brokerUrl);
    QString host = url.host();
    int port = url.port(1883);
    
    if (host.isEmpty()) {
        qWarning() << "[MQTT] brokerUrl 无效，无法订阅";
        return;
    }
    
    m_mosquittoLineBuffer.clear();
    // 创建新的 mosquitto_sub 进程
    m_mosquittoSubProcess = new QProcess(this);
    
    // 连接信号：接收标准输出（消息）
    QObject::connect(m_mosquittoSubProcess, &QProcess::readyReadStandardOutput, this, [this]() {
        QProcess* proc = qobject_cast<QProcess*>(sender());
        if (!proc) return;
        
        m_mosquittoLineBuffer.append(QString::fromUtf8(proc->readAllStandardOutput()));
        // 按行分割，保留最后可能不完整的行在 buffer 中
        int lastNewline = m_mosquittoLineBuffer.lastIndexOf('\n');
        if (lastNewline < 0) return;  // 无完整行，等待更多数据
        
        QString complete = m_mosquittoLineBuffer.left(lastNewline + 1);
        m_mosquittoLineBuffer = m_mosquittoLineBuffer.mid(lastNewline + 1);
        
        const QStringList lines = complete.split('\n', Qt::SkipEmptyParts);
        for (const QString& line : lines) {
            QString trimmedLine = line.trimmed();
            if (trimmedLine.isEmpty()) continue;
            // 去除 BOM（UTF-8: EF BB BF）
            if (trimmedLine.startsWith(QChar(0xFEFF))) {
                trimmedLine = trimmedLine.mid(1);
            }
            
            QString topic = m_statusTopic;
            QString payload = trimmedLine;
            // 策略：整行能解析为 JSON → 纯 payload；否则按 "topic payload" 拆分
            QJsonParseError err;
            QJsonDocument::fromJson(trimmedLine.toUtf8(), &err);
            if (err.error == QJsonParseError::NoError) {
                payload = trimmedLine;
                topic = m_statusTopic;
            } else {
                int idx = trimmedLine.indexOf(' ');
                if (idx > 0) {
                    QString maybeTopic = trimmedLine.left(idx);
                    QString maybePayload = trimmedLine.mid(idx + 1).trimmed();
                    // topic 形如 vehicle/xxx/status，payload 为 JSON
                    if (maybeTopic.startsWith("vehicle/") && maybeTopic.contains("/")) {
                        QJsonParseError err2;
                        QJsonDocument::fromJson(maybePayload.toUtf8(), &err2);
                        if (err2.error == QJsonParseError::NoError) {
                            topic = maybeTopic;
                            payload = maybePayload;
                        }
                    }
                }
            }
            
            qDebug() << "[MQTT] [mosquitto_sub] 收到消息，主题:" << topic << "大小:" << payload.size() << "bytes";
            qDebug() << "[MQTT] [mosquitto_sub] 消息内容（前100字符）:" << payload.left(100);
            
            // ★ 检查是否包含档位信息
            if (payload.contains("\"gear\"")) {
                qDebug() << "[GEAR] [MQTT] [mosquitto_sub] 消息包含档位信息";
            }
            
            // 调用消息处理函数
            onMessageReceived(topic.toUtf8(), payload.toUtf8());
        }
    });
    
    // 连接错误信号
    QObject::connect(m_mosquittoSubProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                     this, [this](int exitCode, QProcess::ExitStatus exitStatus) {
        if (exitStatus == QProcess::CrashExit) {
            qWarning() << "[MQTT] mosquitto_sub 进程崩溃，退出码:" << exitCode;
            emit errorOccurred(QString("mosquitto_sub 进程崩溃: %1").arg(exitCode));
        } else if (exitCode != 0) {
            qWarning() << "[MQTT] mosquitto_sub 进程异常退出，退出码:" << exitCode;
            QString errorOutput = m_mosquittoSubProcess->readAllStandardError();
            if (!errorOutput.isEmpty()) {
                qWarning() << "[MQTT] mosquitto_sub 错误输出:" << errorOutput;
            }
        } else {
            qDebug() << "[MQTT] mosquitto_sub 进程正常退出";
        }
    });
    
    // 启动 mosquitto_sub
    // 必须使用 -v，否则默认只输出 payload，导致 JSON 被错误按空格拆分解析失败
    QStringList args;
    args << "-h" << host;
    args << "-p" << QString::number(port);
    args << "-v";  // 输出 "topic payload" 格式，便于按第一个空格拆分
    args << "-t" << m_statusTopic;
    // 如果设置了 VIN，也订阅车辆特定主题
    if (!m_currentVin.isEmpty()) {
        QString vehicleStatusTopic = QString("vehicle/%1/status").arg(m_currentVin);
        args << "-t" << vehicleStatusTopic;
        qDebug() << "[MQTT] 订阅车辆特定主题:" << vehicleStatusTopic;
    }
    qDebug() << "[MQTT] 启动 mosquitto_sub，参数:" << args.join(" ");
    // 连接错误启动信号，异步检测启动失败（不阻塞主线程）
    QObject::connect(m_mosquittoSubProcess, &QProcess::errorOccurred,
                     this, [this](QProcess::ProcessError err) {
        if (err == QProcess::FailedToStart) {
            qWarning() << "[CLIENT][MQTT] mosquitto_sub 启动失败（FailedToStart）";
            emit errorOccurred("mosquitto_sub 启动失败，请安装 mosquitto-clients 或使用 ENABLE_MQTT_PAHO");
            if (m_mosquittoSubProcess) {
                m_mosquittoSubProcess->deleteLater();
                m_mosquittoSubProcess = nullptr;
            }
        }
    });
    
    m_mosquittoSubProcess->start("mosquitto_sub", args);
    qDebug() << "[CLIENT][MQTT] mosquitto_sub 已启动（异步），订阅主题:" << m_statusTopic;
#endif
}

void MqttController::onDisconnected()
{
    qDebug() << "MQTT disconnected, scheduling reconnect...";
    updateConnectionStatus(false);
    scheduleReconnect();
}

void MqttController::scheduleReconnect()
{
    if (m_reconnectScheduled) {
        return;
    }

    if (m_reconnectAttempts >= m_maxReconnectAttempts) {
        qWarning() << "[CLIENT][MQTT] 达到最大重连次数 (" << m_maxReconnectAttempts
                   << ")，停止自动重连，请检查网络和 broker";
        emit errorOccurred("MQTT 连接失败，已达到最大重连次数");
        return;
    }

    m_reconnectScheduled = true;
    // 指数退避：base * 2^attempts，最大 30 秒
    int delay = std::min(m_baseReconnectDelayMs * (1 << m_reconnectAttempts), m_maxReconnectDelayMs);
    m_reconnectAttempts++;

    qDebug() << "[CLIENT][MQTT] 计划" << delay << "ms 后重连 (attempt" << m_reconnectAttempts << "/"
             << m_maxReconnectAttempts << ")";
    m_reconnectTimer->start(delay);
}

void MqttController::onReconnectTimer()
{
    m_reconnectScheduled = false;
    qDebug() << "[CLIENT][MQTT] 执行重连 (attempt" << m_reconnectAttempts << "/"
             << m_maxReconnectAttempts << ")";
    connectToBroker();
}

void MqttController::onMessageReceived(const QByteArray &topic, const QByteArray &payload)
{
    static int messageCount = 0;
    static QElapsedTimer logTimer;
    static QElapsedTimer firstMessageTimer;
    
    messageCount++;
    
    // 初始化计时器
    if (messageCount == 1) {
        logTimer.start();
        firstMessageTimer.start();
        qDebug() << "[CHASSIS_DATA] ========== 开始接收 MQTT 消息 ==========";
        qDebug() << "[CHASSIS_DATA] 主题:" << topic;
        qDebug() << "[CHASSIS_DATA] 消息大小:" << payload.size() << "bytes";
    }
    
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(payload, &error);
    
    if (error.error == QJsonParseError::NoError) {
        QJsonObject status = doc.object();
        
        bool isAckMessage = status.contains("type") && status["type"].toString() == "remote_control_ack";
        if (isAckMessage) {
            bool ackEnabled = status.value("remote_control_enabled").toBool();
            qDebug() << "[CLIENT][REMOTE_CONTROL] ack received: enabled=" << ackEnabled
                     << "mode=" << status.value("driving_mode").toString();
        }
        
        // 日志记录（每50条或每5秒记录一次）
        bool shouldLog = (messageCount % 50 == 0) || (logTimer.elapsed() >= 5000) || isAckMessage;
        
        if (shouldLog) {
            // 提取关键字段用于日志
            double speed = status.value("speed").toDouble(0.0);
            double battery = status.value("battery").toDouble(100.0);
            double odometer = status.value("odometer").toDouble(0.0);
            QString gear = "N";
            QJsonValue gearVal = status.value("gear");
            if (gearVal.isString()) {
                gear = gearVal.toString();
            } else if (gearVal.isDouble()) {
                int g = gearVal.toInt();
                gear = (g == -1) ? "R" : ((g == 0) ? "N" : "D");
            }
            
            // 计算实际接收频率
            qint64 totalElapsed = firstMessageTimer.elapsed();
            double actualFreq = (totalElapsed > 0) ? (messageCount * 1000.0 / totalElapsed) : 0.0;
            
            qDebug() << "[CHASSIS_DATA] 接收 #" << messageCount
                     << " | 主题:" << topic
                     << " | 速度:" << QString::number(speed, 'f', 1) << "km/h"
                     << " | 电池:" << QString::number(battery, 'f', 1) << "%"
                     << " | 里程:" << QString::number(odometer, 'f', 2) << "km"
                     << " | 档位:" << gear
                     << " | 实际频率:" << QString::number(actualFreq, 'f', 1) << "Hz"
                     << " | 数据大小:" << payload.size() << "bytes";
            
            logTimer.restart();
        }
        
        emit statusReceived(status);
    } else {
        qWarning() << "[CHASSIS_DATA] 解析MQTT消息失败:" << error.errorString()
                   << "| 主题:" << topic
                   << "| 消息大小:" << payload.size() << "bytes"
                   << "| 错误位置:" << error.offset;
    }
}

void MqttController::onError(const QString &error)
{
    qWarning() << "MQTT error:" << error;
    emit errorOccurred(error);
}

void MqttController::updateConnectionStatus(bool connected)
{
    if (m_isConnected != connected) {
        m_isConnected = connected;
        emit connectionStatusChanged(isConnected());
    }
}
