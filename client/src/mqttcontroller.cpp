#include "mqttcontroller.h"

#include "core/eventbus.h"
#include "core/tracing.h"
#include "utils/MqttControlEnvelope.h"
#include "webrtcclient.h"

#include <QDateTime>
#include <QDebug>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QProcess>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>

// 生产构建：定义 ENABLE_MQTT_PAHO 时使用 Paho async_client（见 CMakeLists / connectToBroker）。

namespace {
// 入站 MQTT 原始日志（Paho 回调 / mosquitto_sub）默认每 N 条记 1 条，避免 10~50Hz 遥测刷屏。
// CLIENT_MQTT_TRACE_MESSAGES=1 全量；CLIENT_MQTT_MESSAGE_LOG_SAMPLE_RATE 覆盖 N（≤0 视为默认）。
constexpr int kDefaultMqttInboundLogEveryN = 200;

bool shouldLogMqttInboundMessage(quint64 &seq) {
  ++seq;
  if (qEnvironmentVariableIntValue("CLIENT_MQTT_TRACE_MESSAGES") != 0)
    return true;
  if (seq <= 3u)
    return true;
  bool ok = false;
  const int rate = qEnvironmentVariableIntValue("CLIENT_MQTT_MESSAGE_LOG_SAMPLE_RATE", &ok);
  const int mod = (ok && rate > 0) ? rate : kDefaultMqttInboundLogEveryN;
  if (mod <= 1)
    return true;
  return (seq % static_cast<quint64>(mod)) == 0u;
}

quint64 s_mqttPahoInboundSeq = 0;
quint64 s_mqttSubInboundSeq = 0;

bool mqttChainVerbose() {
  static const int v = qEnvironmentVariableIntValue("CLIENT_MQTT_CHAIN_DIAG");
  return v != 0;
}
}  // namespace

MqttController::MqttController(QObject *parent) : QObject(parent) {
#ifdef ENABLE_MQTT_PAHO
  // Paho MQTT 客户端将在 connectToBroker 时初始化
#endif
  // 初始化重连定时器（指数退避）
  m_reconnectTimer = new QTimer(this);
  m_reconnectTimer->setSingleShot(true);
  connect(m_reconnectTimer, &QTimer::timeout, this, &MqttController::onReconnectTimer);
}

bool MqttController::controlChannelReady() const {
  return m_isConnected;
}

bool MqttController::isConnected() const {
  return controlChannelReady();
}

void MqttController::bumpControlChannelState() {
  const bool ready = m_isConnected;
  if (ready == m_controlChannelReadyCache)
    return;
  m_controlChannelReadyCache = ready;
  emit controlChannelReadyChanged();
  emit connectionStatusChanged(ready);
}

MqttController::~MqttController() {
  disconnectFromBroker();
#ifndef ENABLE_MQTT_PAHO
  if (m_mosquittoSubProcess) {
    if (m_mosquittoSubProcess->state() != QProcess::NotRunning) {
      m_mosquittoSubProcess->terminate();
      // 生产环境不应在此 wait 以免阻塞 UI 析构；QProcess 随 deleteLater 会由 Qt 进行后台清理或在 destruction 时杀掉
    }
    m_mosquittoSubProcess->deleteLater();
    m_mosquittoSubProcess = nullptr;
  }
#endif
}

void MqttController::setBrokerUrl(const QString &url) {
  if (m_brokerUrl != url) {
    m_brokerUrl = url;
    emit brokerUrlChanged(m_brokerUrl);
  }
}

void MqttController::setClientId(const QString &id) {
  if (m_clientId != id) {
    m_clientId = id;
    emit clientIdChanged(m_clientId);
  }
}

void MqttController::setControlTopic(const QString &topic) {
  if (m_controlTopic != topic) {
    m_controlTopic = topic;
    emit controlTopicChanged(m_controlTopic);
  }
}

void MqttController::setStatusTopic(const QString &topic) {
  if (m_statusTopic != topic) {
    m_statusTopic = topic;
    emit statusTopicChanged(m_statusTopic);
  }
}

void MqttController::setCurrentVin(const QString &vin) {
  {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    if (m_currentVin == vin) return;
    m_currentVin = vin;
  }
  qDebug() << "MQTT Controller: Current VIN set to" << vin;
  emit currentVinChanged(vin);
  bumpControlChannelState();
}

void MqttController::connectToBroker() {
  if (m_isConnected) {
    qDebug() << "[CLIENT][MQTT][CHAIN] phase=SKIP already_connected url=" << m_brokerUrl;
    return;
  }
  if (m_brokerConnectInFlight) {
    qInfo().noquote() << "[CLIENT][MQTT][CHAIN] phase=SKIP in_flight (duplicate connectToBroker) url="
                      << m_brokerUrl;
    return;
  }

  QUrl url(m_brokerUrl);
  QString host = url.host();
  int port = url.port(1883);
  if (host.isEmpty()) {
    qCritical().noquote()
        << "[CLIENT][MQTT][CHAIN] phase=URL_INVALID raw_brokerUrl=" << m_brokerUrl
        << " | 环节: 解析后 host 为空；请使用 mqtt://HOST:1883 或 tcp://HOST:1883（Docker 内服务名在宿主机客户端上不可解析）";
    emit mqttConnectResolved(false, QStringLiteral("invalid_broker_url_empty_host|%1").arg(m_brokerUrl));
    emit errorOccurred(QStringLiteral("MQTT 地址无效：解析后主机名为空。请检查 mqtt://IP:1883"));
    return;
  }

  m_brokerConnectInFlight = true;
  ++m_connectWatchdogGeneration;
  const quint64 watchdogGen = m_connectWatchdogGeneration;

  qInfo().noquote() << "[CLIENT][MQTT][CHAIN] phase=ATTEMPT gen=" << watchdogGen << " vin=" << m_currentVin
                    << " host=" << host << " port=" << port << " controlTopic=" << m_controlTopic
                    << " statusTopic=" << m_statusTopic
#ifdef ENABLE_MQTT_PAHO
                    << " backend=Paho"
#else
                    << " backend=mosquitto_sub+pub"
#endif
                    << " | grep [CLIENT][MQTT][CHAIN] 跟踪全链路";

  QTimer::singleShot(15000, this, [this, watchdogGen]() {
    if (watchdogGen != m_connectWatchdogGeneration)
      return;
    if (m_isConnected) {
      m_brokerConnectInFlight = false;
      return;
    }
    m_brokerConnectInFlight = false;
    qCritical().noquote()
        << "[CLIENT][MQTT][CHAIN] phase=TIMEOUT gen=" << watchdogGen << " url=" << m_brokerUrl
        << " | 15s 内未进入已连接：检查 broker 是否监听、端口映射、防火墙、以及宿主机能否访问该 host";
    
    SystemErrorEvent sysErr;
    sysErr.domain = QStringLiteral("MQTT");
    sysErr.severity = SystemErrorEvent::Severity::CRITICAL;
    sysErr.code = QStringLiteral("MQTT-TIMEOUT");
    sysErr.message = QStringLiteral("MQTT 连接超时(15s): %1").arg(m_brokerUrl);
    EventBus::instance().publish(sysErr);

    emit mqttConnectResolved(
        false, QStringLiteral("connect_timeout_15s|url=%1|see_logs_CLIENT_MQTT_CHAIN").arg(m_brokerUrl));
    emit errorOccurred(
        QStringLiteral("MQTT 连接超时(15s)。请确认 Mosquitto 已启动且地址/端口可从本机访问（docker 内需映射 1883）"));
  });

#ifdef ENABLE_MQTT_PAHO
  try {
    QString address = QString("%1:%2").arg(host).arg(port);
    if (mqttChainVerbose()) {
      qInfo() << "[CLIENT][MQTT][CHAIN] phase=PAHO_CREATE clientId=" << m_clientId << " server=" << address;
    }

    m_client =
        std::make_unique<mqtt::async_client>(address.toStdString(), m_clientId.toStdString());

    m_client->set_connected_handler([this](const std::string &) {
      qInfo().noquote() << "[CLIENT][MQTT][CHAIN] phase=PAHO_TCP_OK vin=" << m_currentVin
                        << " → invoking onConnected (subscribe + m_isConnected)";
      QMetaObject::invokeMethod(this, "onConnected", Qt::QueuedConnection);
    });

    m_client->set_connection_lost_handler([this](const std::string &cause) {
      const QString qcause = QString::fromStdString(cause);
      qWarning().noquote() << "[CLIENT][MQTT][CHAIN] phase=CONNECTION_LOST vin=" << m_currentVin
                           << " cause=" << (qcause.isEmpty() ? QStringLiteral("(empty)") : qcause);
      QMetaObject::invokeMethod(this, "onDisconnected", Qt::QueuedConnection);
    });

    m_client->set_message_callback([this](mqtt::const_message_ptr msg) {
      QString topic = QString::fromStdString(msg->get_topic());
      QByteArray payload(msg->to_string().data(), msg->to_string().length());
      if (shouldLogMqttInboundMessage(s_mqttPahoInboundSeq)) {
        qDebug() << "[MQTT] 消息回调触发，主题:" << topic << "大小:" << payload.size() << "bytes";
      }
      QMetaObject::invokeMethod(this, "onMessageReceived", Qt::QueuedConnection,
                                Q_ARG(QByteArray, topic.toUtf8()), Q_ARG(QByteArray, payload));
    });

    mqtt::connect_options connOpts;
    connOpts.set_clean_session(true);
    connOpts.set_automatic_reconnect(true);
    connOpts.set_keep_alive_interval(60);
    connOpts.set_connect_timeout(10);

    if (mqttChainVerbose()) {
      qInfo() << "[CLIENT][MQTT][CHAIN] phase=PAHO_CONNECT_ASYNC connect_timeout_s=10 keepalive_s=60";
    }
    m_client->connect(connOpts);

  } catch (const mqtt::exception &exc) {
    ++m_connectWatchdogGeneration;
    m_brokerConnectInFlight = false;
    const QString err = QStringLiteral("MQTT Paho 异常: %1").arg(exc.what());
    qCritical().noquote() << "[CLIENT][MQTT][CHAIN] phase=PAHO_EXCEPTION " << err;
    emit mqttConnectResolved(false, QStringLiteral("paho_exception|%1").arg(exc.what()));
    emit errorOccurred(err);
  }
#else
  qInfo() << "[CLIENT][MQTT][CHAIN] phase=MOSQUITTO_SUB_SCHEDULE delay_ms=500 (无 Paho：订阅靠子进程)";
  QTimer::singleShot(500, this, [this, watchdogGen]() {
    if (watchdogGen != m_connectWatchdogGeneration)
      return;
    onConnected();
  });
#endif
}

void MqttController::disconnectFromBroker() {
  ++m_connectWatchdogGeneration;
  m_brokerConnectInFlight = false;
  if (!m_isConnected) {
    return;
  }

  qInfo() << "[CLIENT][MQTT][CHAIN] phase=USER_DISCONNECT url=" << m_brokerUrl;

#ifdef ENABLE_MQTT_PAHO
  try {
    if (m_client) {
      // 异步断开：不阻塞主线程；用 QTimer 延迟销毁客户端给 paho 内部清理时间
      auto *rawClient = m_client.release();
      try {
        rawClient->disconnect();
      } catch (...) {
      }
      QTimer::singleShot(2000, [rawClient]() { delete rawClient; });
    }
  } catch (const mqtt::exception &exc) {
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

void MqttController::sendSteeringCommand(double angle) {
  sendControlCommand(
      MqttControlEnvelope::buildSteering(angle, QDateTime::currentMSecsSinceEpoch()));
}

void MqttController::sendThrottleCommand(double throttle) {
  sendControlCommand(
      MqttControlEnvelope::buildThrottle(throttle, QDateTime::currentMSecsSinceEpoch()));
}

void MqttController::sendBrakeCommand(double brake) {
  sendControlCommand(
      MqttControlEnvelope::buildBrake(brake, QDateTime::currentMSecsSinceEpoch(), ++m_seq));
}

void MqttController::sendSpeedCommand(double speed) {
  sendControlCommand(
      MqttControlEnvelope::buildTargetSpeed(speed, QDateTime::currentMSecsSinceEpoch()));
}

void MqttController::sendEmergencyStopCommand(bool enable) {
  qWarning() << "[CLIENT][EMERGENCY_STOP] enable=" << enable;
  sendControlCommand(
      MqttControlEnvelope::buildEmergencyStop(enable, QDateTime::currentMSecsSinceEpoch()));
}

void MqttController::sendGearCommand(int gear) {
  qDebug() << "[CLIENT][GEAR] gear=" << gear;
  sendControlCommand(MqttControlEnvelope::buildGear(gear, QDateTime::currentMSecsSinceEpoch()));
}

void MqttController::sendSweepCommand(const QString &sweepType, bool active) {
  qDebug() << "[CLIENT][SWEEP] type=" << sweepType << "active=" << active;
  sendControlCommand(
      MqttControlEnvelope::buildSweep(sweepType, active, QDateTime::currentMSecsSinceEpoch()));
}

void MqttController::sendLightCommand(const QString &lightType, bool active) {
  qDebug() << "[LIGHT] 发送灯光命令: type=" << lightType << "active=" << active;

  if (!m_isConnected) {
    qWarning() << "[LIGHT] MQTT 未连接";
    return;
  }

  sendControlCommand(MqttControlEnvelope::buildLight(lightType, active));
}

void MqttController::sendControlCommand(const QJsonObject &command) {
  QString vin;
  bool connected;
  {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    vin = m_currentVin;
    connected = m_isConnected;
  }

  if (!connected) {
    static int s_mqttOffWarn = 0;
    if (++s_mqttOffWarn == 1 || s_mqttOffWarn % 200 == 0) {
      qWarning() << "[CLIENT][Control] ✗ 无法发送：MQTT 未连接"
                 << " throttled=" << s_mqttOffWarn;
    }
    return;
  }

  uint32_t seq = m_seq.fetch_add(1);
  const auto prep = MqttControlEnvelope::prepareForSend(command, vin,
                                                        QDateTime::currentMSecsSinceEpoch(), seq);
  if (!prep.ok) {
    return;
  }
  sendControlCommandAuto(prep.cmd);
}

void MqttController::sendControlCommandAuto(const QJsonObject &command) {
  if (!m_isConnected) {
    static int s_logCount = 0;
    if (++s_logCount % 400 == 1) {
      qWarning() << "[CLIENT][Control] MQTT 未连接，车控未发送 (throttled 1/400)";
    }
    return;
  }
  (void)m_preferredChannel;
  sendControlCommandViaMqtt(command);
}

void MqttController::sendControlCommandViaDataChannel(const QJsonObject &command) {
  if (!m_webrtcClient || !m_webrtcClient->isDataChannelOpen()) {
    qWarning() << "[CLIENT][Control] DataChannel 未就绪，无法发送";
    return;
  }
  const QByteArray payload = QJsonDocument(command).toJson(QJsonDocument::Compact);
  if (!m_webrtcClient->trySendDataChannelMessage(payload))
    qWarning() << "[CLIENT][Control] DataChannel trySend 失败";
}

void MqttController::sendControlCommandViaMqtt(const QJsonObject &command) {
  if (!m_isConnected) {
    qWarning() << "[CLIENT][Control] MQTT 未连接，无法发送";
    return;
  }
  const QString typ = command.value(QStringLiteral("type")).toString();
  const int qos = (typ == QStringLiteral("start_stream") || typ == QStringLiteral("stop_stream")) ? 1
                                                                                                    : 0;
  if (publishMessage(m_controlTopic, command, qos)) {
    m_activeChannel = ChannelType::MQTT;
    m_mqttCount++;
  }
}

void MqttController::setWebRtcClient(WebRtcClient *client) {
  if (m_webrtcClient == client) {
    return;
  }
  if (m_webrtcClient) {
    disconnect(m_webrtcClient, &WebRtcClient::connectionStatusChanged, this, nullptr);
    disconnect(m_webrtcClient, &WebRtcClient::dataChannelOpenChanged, this, nullptr);
  }
  m_webrtcClient = client;
  if (m_webrtcClient) {
    connect(m_webrtcClient, &WebRtcClient::connectionStatusChanged, this, [this]() {
      qDebug() << "[CLIENT][Control] WebRTC 媒体状态变化（车控仍仅走 MQTT）";
    });
    connect(m_webrtcClient, &WebRtcClient::dataChannelOpenChanged, this, [this]() {
      qDebug() << "[CLIENT][Control] DataChannel 状态变化（车控仍仅走 MQTT；hint 等仍可用）";
    });
  }
  qDebug() << "[CLIENT][Control] WebRTC 客户端已设置，DataChannel 可用时将优先使用";
}

void MqttController::setPreferredChannel(const QString &channelType) {
  using ME = MqttControlEnvelope::PreferredChannel;
  const ME p = MqttControlEnvelope::parsePreferredChannel(channelType);
  ChannelType next = ChannelType::AUTO;
  switch (p) {
    case ME::DataChannel:
      next = ChannelType::DATA_CHANNEL;
      break;
    case ME::Mqtt:
      next = ChannelType::MQTT;
      break;
    case ME::WebSocket:
      next = ChannelType::WEBSOCKET;
      break;
    case ME::Auto:
      next = ChannelType::AUTO;
      break;
  }
  if (m_preferredChannel != next) {
    m_preferredChannel = next;
    qDebug() << "[CLIENT][Control] 首选通道设置为:" << channelType;
  }
}

QString MqttController::getActiveChannelType() const {
  switch (m_activeChannel) {
    case ChannelType::AUTO:
      return QStringLiteral("AUTO");
    case ChannelType::DATA_CHANNEL:
      return QStringLiteral("DATA_CHANNEL");
    case ChannelType::MQTT:
      return QStringLiteral("MQTT");
    case ChannelType::WEBSOCKET:
      return QStringLiteral("WEBSOCKET");
    default:
      return QStringLiteral("UNKNOWN");
  }
}

void MqttController::requestStreamStart() {
  qDebug() << "[CLIENT][Control] requestStreamStart mqtt=" << m_isConnected
           << " m_currentVin=" << m_currentVin;
  if (!m_isConnected) {
    qWarning() << "[CLIENT][Control] ✗ 无法发送 start_stream：MQTT 未连接";
    emit errorOccurred(
        QStringLiteral("请先连接 MQTT 再请求推流（车端/carla-bridge 仅订阅 vehicle/control）"));
    return;
  }
  if (m_currentVin.isEmpty()) {
    qWarning() << "[CLIENT][Control] ✗ 无法发送 start_stream：VIN 为空";
    emit errorOccurred(QStringLiteral("请先选择车辆后再连接视频（start_stream 需带 VIN）"));
    return;
  }
  sendControlCommand(MqttControlEnvelope::buildStartStream(QDateTime::currentMSecsSinceEpoch()));
  qInfo() << "[CLIENT][MQTT] start_stream 已提交（MQTT QoS1）；等待车端推流注册到 ZLM（通常数秒至数十秒）";
}

void MqttController::requestStreamStop() {
  if (!m_isConnected) {
    qDebug() << "[CLIENT][MQTT] MQTT 未连接，跳过 stop_stream";
    return;
  }
  if (m_currentVin.isEmpty()) {
    qWarning() << "[CLIENT][MQTT] VIN 为空，跳过 stop_stream";
    return;
  }
  sendControlCommand(MqttControlEnvelope::buildStopStream(QDateTime::currentMSecsSinceEpoch()));
  qDebug() << "[CLIENT][MQTT] 已请求车端/仿真停止推流 stop_stream vin=" << m_currentVin;
}

void MqttController::publishClientEncoderHint(const QJsonObject &hintPayload) {
  if (!m_isConnected) {
    qDebug() << "[CLIENT][MQTT][EncoderHint] skip publish (broker not connected)";
    return;
  }
  QJsonObject o(hintPayload);
  if (!m_currentVin.isEmpty() && !o.contains(QStringLiteral("vin")))
    o.insert(QStringLiteral("vin"), m_currentVin);
  o.insert(QStringLiteral("mqttRelayAtMs"), QDateTime::currentMSecsSinceEpoch());
  publishMessage(QStringLiteral("teleop/client_encoder_hint"), o);
  const QString compact = QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
  qInfo().noquote() << "[Client][MQTT][EncoderHint] published teleop/client_encoder_hint vin="
                    << m_currentVin << " stream=" << o.value(QStringLiteral("stream")).toString()
                    << " preferSingleSlice="
                    << o.value(QStringLiteral("preferH264SingleSlice")).toBool()
                    << " reasonCode=" << o.value(QStringLiteral("reasonCode")).toString()
                    << " payload=" << compact
                    << " ★车端/桥接应回 encoder_hint_ack；grep [Client][MQTT][EncoderHint] ack";
}

void MqttController::requestRemoteControl(bool enable) {
  // ★ 精确定位：便于 grep [REMOTE_CONTROL][SEND]
  qDebug() << "[REMOTE_CONTROL][SEND] >>> 即将发送 remote_control <<< enable="
           << (enable ? "true" : "false") << " topic=" << m_controlTopic << " vin=" << m_currentVin;
  qDebug() << "[REMOTE_CONTROL] ========== [MQTT] 请求远驾接管 ==========";
  qDebug() << "[REMOTE_CONTROL] enable=" << (enable ? "true" : "false")
           << " m_isConnected=" << (m_isConnected ? "true" : "false")
           << " m_currentVin=" << m_currentVin << " m_controlTopic=" << m_controlTopic;

  if (!m_isConnected) {
    qWarning() << "[REMOTE_CONTROL][SEND] ✗ 未发送：MQTT 未连接（远驾指令仅走 vehicle/control）";
    emit errorOccurred(
        QStringLiteral("远驾接管未发送：请先连接 MQTT（视频/DataChannel 不能替代 MQTT）"));
    return;
  }
  try {
    sendControlCommand(
        MqttControlEnvelope::buildRemoteControl(enable, QDateTime::currentMSecsSinceEpoch()));
  } catch (const std::exception &e) {
    qCritical() << "[REMOTE_CONTROL][SEND] ✗ 异常:" << e.what();
    emit errorOccurred(QStringLiteral("remote_control 发送异常: %1").arg(e.what()));
    return;
  } catch (...) {
    qCritical() << "[REMOTE_CONTROL][SEND] ✗ 未知异常";
    emit errorOccurred(QStringLiteral("remote_control 发送未知异常"));
    return;
  }
  qDebug() << "[REMOTE_CONTROL][SEND] <<< 已调用 sendControlCommand(remote_control) topic="
           << m_controlTopic << " 请确认车端收到";
}

void MqttController::sendDriveCommand(double steering, double throttle, double brake, int gear) {
  sendControlCommand(MqttControlEnvelope::buildDrive(steering, throttle, brake, gear,
                                                     QDateTime::currentMSecsSinceEpoch()));
}

bool MqttController::publishMessage(const QString &topic, const QJsonObject &payload, int qos) {
  QJsonDocument doc(payload);
  QByteArray data = doc.toJson(QJsonDocument::Compact);
  const int q = (qos >= 1) ? 1 : 0;

#ifdef ENABLE_MQTT_PAHO
  try {
    if (m_client && m_client->is_connected()) {
      mqtt::message_ptr msg =
          mqtt::make_message(topic.toStdString(), std::string(data.data(), data.length()));
      msg->set_qos(q);
      m_client->publish(msg);
      return true;
    }
    qWarning() << "[CLIENT][MQTT] ✗ Paho 未连接，未发送 topic=" << topic;
    return false;
  } catch (const mqtt::exception &exc) {
    qWarning() << "[CLIENT][MQTT] ✗ Paho 发布失败 topic=" << topic << " error=" << exc.what();
    emit errorOccurred(QString("MQTT publish error: %1").arg(exc.what()));
    return false;
  }
#else
  QUrl url(m_brokerUrl);
  QString host = url.host();
  int port = url.port(1883);
  if (host.isEmpty()) {
    qWarning() << "[CLIENT][MQTT] ✗ brokerUrl 无效，无法发送 topic=" << topic;
    return false;
  }
  QStringList args;
  args << "-h" << host << "-p" << QString::number(port) << "-t" << topic << "-m"
       << QString::fromUtf8(data);
  if (q >= 1)
    args << QStringLiteral("-q") << QStringLiteral("1");
  const bool ok = QProcess::startDetached(QStringLiteral("mosquitto_pub"), args);
  if (!ok) {
    qWarning() << "[CLIENT][MQTT] ✗ mosquitto_pub startDetached 失败 topic=" << topic
               << "(请安装 mosquitto-clients 或编译 ENABLE_MQTT_PAHO)";
  }
  return ok;
#endif
}

void MqttController::onConnected() {
  ++m_connectWatchdogGeneration;
  m_brokerConnectInFlight = false;

#ifndef ENABLE_MQTT_PAHO
  {
    QUrl u(m_brokerUrl);
    if (u.host().isEmpty()) {
      qCritical().noquote()
          << "[CLIENT][MQTT][CHAIN] phase=ON_CONNECTED_ABORT brokerUrl_invalid raw=" << m_brokerUrl;
      updateConnectionStatus(false);
      emit mqttConnectResolved(false, QStringLiteral("on_connected_empty_host|%1").arg(m_brokerUrl));
      return;
    }
  }
#endif

  // ★ 必须通过 updateConnectionStatus 触发 mqttBrokerConnectionChanged / controlChannelReadyChanged
  // （旧代码仅写 m_isConnected 会导致 QML 收不到「已连接」信号，表现为视频有而车控全失败）
  updateConnectionStatus(true);

  m_reconnectAttempts = 0;
  m_reconnectScheduled = false;
  qInfo().noquote() << "[CLIENT][MQTT][CHAIN] phase=SESSION_UP m_currentVin=" << m_currentVin
                    << " m_controlTopic=" << m_controlTopic
                    << " m_statusTopic=" << m_statusTopic
                    << " | 下一环节: 订阅 status；车控发布 topic=" << m_controlTopic
                    << " | QML 请监听 mqttBrokerConnectionChanged(true)";
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
  } catch (const mqtt::exception &exc) {
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
    QProcess *proc = qobject_cast<QProcess *>(sender());
    if (!proc)
      return;

    m_mosquittoLineBuffer.append(QString::fromUtf8(proc->readAllStandardOutput()));
    // 按行分割，保留最后可能不完整的行在 buffer 中
    int lastNewline = m_mosquittoLineBuffer.lastIndexOf('\n');
    if (lastNewline < 0)
      return;  // 无完整行，等待更多数据

    QString complete = m_mosquittoLineBuffer.left(lastNewline + 1);
    m_mosquittoLineBuffer = m_mosquittoLineBuffer.mid(lastNewline + 1);

    const QStringList lines = complete.split('\n', Qt::SkipEmptyParts);
    for (const QString &line : lines) {
      QString trimmedLine = line.trimmed();
      if (trimmedLine.isEmpty())
        continue;
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

      if (shouldLogMqttInboundMessage(s_mqttSubInboundSeq)) {
        qDebug() << "[MQTT] [mosquitto_sub] 收到消息，主题:" << topic << "大小:" << payload.size()
                 << "bytes";
        qDebug() << "[MQTT] [mosquitto_sub] 消息内容（前100字符）:" << payload.left(100);
        if (payload.contains(QStringLiteral("\"gear\""))) {
          qDebug() << "[GEAR] [MQTT] [mosquitto_sub] 消息包含档位信息";
        }
      }

      // 调用消息处理函数
      onMessageReceived(topic.toUtf8(), payload.toUtf8());
    }
  });

  // 连接错误信号
  QObject::connect(m_mosquittoSubProcess,
                   QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
                   [this](int exitCode, QProcess::ExitStatus exitStatus) {
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
  QObject::connect(
      m_mosquittoSubProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError err) {
        if (err == QProcess::FailedToStart) {
          qWarning() << "[CLIENT][MQTT] mosquitto_sub 启动失败（FailedToStart）";
          emit errorOccurred(
              "mosquitto_sub 启动失败，请安装 mosquitto-clients 或使用 ENABLE_MQTT_PAHO");
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

void MqttController::onDisconnected() {
  qWarning().noquote() << "[CLIENT][MQTT][CHAIN] phase=BROKER_SESSION_DOWN vin=" << m_currentVin
                     << " url=" << m_brokerUrl << " → updateConnectionStatus(false)+scheduleReconnect";
  m_brokerConnectInFlight = false;
  updateConnectionStatus(false);
  scheduleReconnect();
}

void MqttController::scheduleReconnect() {
  if (m_reconnectScheduled) {
    return;
  }

  if (m_reconnectAttempts >= m_maxReconnectAttempts) {
    qCritical().noquote()
        << "[CLIENT][MQTT][CHAIN] phase=RECONNECT_EXHAUSTED attempts=" << m_maxReconnectAttempts
        << " url=" << m_brokerUrl;
    emit errorOccurred("MQTT 连接失败，已达到最大重连次数");
    emit mqttConnectResolved(
        false, QStringLiteral("max_reconnect_exhausted|n=%1|url=%2")
                   .arg(m_maxReconnectAttempts)
                   .arg(m_brokerUrl));
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

void MqttController::onReconnectTimer() {
  m_reconnectScheduled = false;
  qDebug() << "[CLIENT][MQTT] 执行重连 (attempt" << m_reconnectAttempts << "/"
           << m_maxReconnectAttempts << ")";
  connectToBroker();
}

void MqttController::onMessageReceived(const QByteArray &topic, const QByteArray &payload) {
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

    bool isAckMessage =
        status.contains("type") && status["type"].toString() == "remote_control_ack";
    if (isAckMessage) {
      bool ackEnabled = status.value("remote_control_enabled").toBool();
      qDebug() << "[CLIENT][REMOTE_CONTROL] ack received: enabled=" << ackEnabled
               << "mode=" << status.value("driving_mode").toString();
    }

    const bool isEncoderHintAck =
        status.contains(QStringLiteral("type")) &&
        status.value(QStringLiteral("type")).toString() == QLatin1String("encoder_hint_ack");
    if (isEncoderHintAck) {
      const QString ackCompact =
          QString::fromUtf8(QJsonDocument(status).toJson(QJsonDocument::Compact));
      qInfo().noquote() << "[Client][MQTT][EncoderHint] vehicle ack vin="
                        << status.value(QStringLiteral("vin")).toString()
                        << " stream=" << status.value(QStringLiteral("stream")).toString()
                        << " actionTaken=" << status.value(QStringLiteral("actionTaken")).toString()
                        << " schemaVersion="
                        << status.value(QStringLiteral("schemaVersion")).toVariant().toString()
                        << " message=" << status.value(QStringLiteral("message")).toString()
                        << " full=" << ackCompact
                        << " ★编码侧已消费 hint（日志/MQTT 闭环）";
    }

    // 日志记录（降频：每 300 条或每 30s；重要 ack 仍每次都记）
    bool shouldLog = (messageCount % 300 == 0) || (logTimer.elapsed() >= 30000) || isAckMessage ||
                     isEncoderHintAck;

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

      qDebug() << "[CHASSIS_DATA] 接收 #" << messageCount << " | 主题:" << topic
               << " | 速度:" << QString::number(speed, 'f', 1) << "km/h"
               << " | 电池:" << QString::number(battery, 'f', 1) << "%"
               << " | 里程:" << QString::number(odometer, 'f', 2) << "km"
               << " | 档位:" << gear << " | 实际频率:" << QString::number(actualFreq, 'f', 1)
               << "Hz"
               << " | 数据大小:" << payload.size() << "bytes";

      logTimer.restart();
    }

    emit statusReceived(status);
  } else {
    qWarning() << "[CHASSIS_DATA] 解析MQTT消息失败:" << error.errorString() << "| 主题:" << topic
               << "| 消息大小:" << payload.size() << "bytes"
               << "| 错误位置:" << error.offset;
  }
}

void MqttController::onError(const QString &error) {
  qWarning() << "MQTT error:" << error;
  
  SystemErrorEvent evt;
  evt.domain = QStringLiteral("MQTT");
  evt.message = error;
  evt.severity = SystemErrorEvent::Severity::ERROR;
  evt.code = QStringLiteral("MQTT-1001");
  EventBus::instance().publish(evt);

  emit errorOccurred(error);
}

void MqttController::updateConnectionStatus(bool connected) {
  {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    if (m_isConnected == connected)
      return;
    m_isConnected = connected;
  }
  emit mqttBrokerConnectionChanged(connected);
  bumpControlChannelState();
}
