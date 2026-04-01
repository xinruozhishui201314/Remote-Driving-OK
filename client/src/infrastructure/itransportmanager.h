#pragma once
#include <QObject>
#include <functional>
#include <future>
#include <cstdint>
#include <string>

/**
 * 网络传输管理器完整接口（《客户端架构设计》§3.1.1）。
 * 多通道分优先级传输：控制/视频/遥测/音频/信令/诊断。
 */

// ─── 传输通道枚举 ─────────────────────────────────────────────────────────────
enum class TransportChannel : uint8_t {
    CONTROL_CRITICAL = 0, // 控制指令 - 最高优先级，UDP+FEC
    VIDEO_PRIMARY    = 1, // 主视频通道 - 高优先级，RTP/WebRTC
    VIDEO_SECONDARY  = 2, // 辅助视频通道
    TELEMETRY        = 3, // 遥测数据通道 - 中优先级
    AUDIO            = 4, // 音频通道
    SIGNALING        = 5, // 信令通道 - TCP/WebSocket
    DIAGNOSTIC       = 6, // 诊断通道 - 低优先级
};

// ─── 连接状态 ─────────────────────────────────────────────────────────────────
enum class ConnectionState {
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    RECONNECTING,
    ERROR,
};

// ─── 网络质量 ─────────────────────────────────────────────────────────────────
struct NetworkQuality {
    double rttMs = 0;
    double packetLossRate = 0;
    double bandwidthKbps = 0;
    double jitterMs = 0;
    double score = 1.0; // 0~1 综合评分
    bool degraded = false;
};

// ─── 通道统计 ─────────────────────────────────────────────────────────────────
struct ChannelStats {
    TransportChannel channel;
    uint64_t bytesSent = 0;
    uint64_t bytesReceived = 0;
    uint64_t packetsSent = 0;
    uint64_t packetsReceived = 0;
    uint64_t packetsLost = 0;
    double latencyMs = 0;
};

// ─── 包元数据 ─────────────────────────────────────────────────────────────────
struct PacketMetadata {
    int64_t  receiveTimestampMs = 0;
    uint32_t sequenceNumber = 0;
    TransportChannel channel = TransportChannel::TELEMETRY;
};

// ─── 连接结果 ─────────────────────────────────────────────────────────────────
struct ConnectionResult {
    bool success = false;
    QString errorMessage;
    ConnectionState state = ConnectionState::DISCONNECTED;
};

// ─── 端点信息 ─────────────────────────────────────────────────────────────────
struct EndpointInfo {
    QString host;
    uint16_t port = 0;
    QString vin;
    QString sessionId;
    QString token;
};

// ─── 发送标志 ─────────────────────────────────────────────────────────────────
enum class SendFlags : uint8_t {
    Default    = 0,
    Unreliable = 1 << 0, // UDP 无重传
    NoCopy     = 1 << 1, // 零拷贝（调用方保证 data 生命周期）
    Encrypted  = 1 << 2,
};

// ─── 发送结果 ─────────────────────────────────────────────────────────────────
struct SendResult {
    bool success = false;
    QString errorMessage;
};

// ─── 传输配置 ─────────────────────────────────────────────────────────────────
struct TransportConfig {
    struct ControlChannelConfig {
        uint16_t port = 9000;
        uint32_t sendRateHz = 100;
        bool enableFEC = true;
        double fecRedundancy = 0.3;
        uint32_t maxLatencyMs = 20;
    } control;

    struct VideoChannelConfig {
        uint16_t port = 9001;
        QString codec = "H265";
        uint32_t maxBitrateKbps = 20000;
        uint32_t minBitrateKbps = 2000;
        bool enableAdaptiveBitrate = true;
        uint32_t jitterBufferMs = 30;
        bool enableFEC = true;
    } video;

    struct GlobalConfig {
        uint32_t heartbeatIntervalMs = 100;
        uint32_t connectionTimeoutMs = 3000;
        uint32_t reconnectDelayMs = 1000;
        int maxReconnectAttempts = 5;
    } global;
};

// ─── ITransportManager 接口 ──────────────────────────────────────────────────
class ITransportManager : public QObject {
    Q_OBJECT
public:
    explicit ITransportManager(QObject* parent = nullptr) : QObject(parent) {}
    ~ITransportManager() override = default;

    // 生命周期
    virtual bool initialize(const TransportConfig& config) = 0;
    virtual void shutdown() = 0;

    // 连接管理
    virtual void connectAsync(const EndpointInfo& endpoint) = 0;
    virtual void disconnect() = 0;

    // 发送（按通道优先级）
    virtual SendResult send(TransportChannel channel,
                            const uint8_t* data, size_t len,
                            SendFlags flags = SendFlags::Default) = 0;

    // 接收回调注册
    virtual void registerReceiver(
        TransportChannel channel,
        std::function<void(const uint8_t*, size_t, const PacketMetadata&)> callback) = 0;

    // 网络质量监控
    virtual NetworkQuality getNetworkQuality() const = 0;
    virtual ChannelStats getChannelStats(TransportChannel ch) const = 0;

    // 便利方法：发送 JSON 字符串（兼容旧接口）
    virtual SendResult sendControlJson(const QByteArray& json) {
        return send(TransportChannel::CONTROL_CRITICAL,
                    reinterpret_cast<const uint8_t*>(json.constData()),
                    static_cast<size_t>(json.size()));
    }

    virtual ConnectionState connectionState() const = 0;

signals:
    void networkQualityChanged(const NetworkQuality& quality);
    void connectionStateChanged(ConnectionState state);
    void channelError(TransportChannel channel, const QString& error);
    void connected();
    void disconnected();
};
