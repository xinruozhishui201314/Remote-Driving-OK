#ifndef WEBRTCSTREAMMANAGER_H
#define WEBRTCSTREAMMANAGER_H

#include <QObject>
#include <QString>
#include <QTimer>
#include "webrtcclient.h"

/**
 * @brief 四路相机 WebRTC 流管理器
 * 管理前/后/左/右四路 WHEP 拉流，供 DrivingInterface 四宫格显示。
 * 多车隔离：流 ID 带 VIN 前缀，格式 {vin}_cam_front 等；VIN 为空时退化为 cam_front（兼容单车测试）。
 * VIN 通过 setCurrentVin() 设置，需在 connectFourStreams() 前调用。
 *
 * 诊断增强：
 * - 每 15s 轮询 ZLM getMediaList，验证 carla-bridge 推流是否仍在 ZLM 注册
 * - 若推流在册但客户端断流 → 诊断 RTCP/ICE/配置问题
 * - 若推流失册 → 诊断 carla-bridge / MQTT / CARLA 问题
 */
class WebRtcStreamManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(WebRtcClient* frontClient READ frontClient CONSTANT)
    Q_PROPERTY(WebRtcClient* rearClient READ rearClient CONSTANT)
    Q_PROPERTY(WebRtcClient* leftClient READ leftClient CONSTANT)
    Q_PROPERTY(WebRtcClient* rightClient READ rightClient CONSTANT)
    Q_PROPERTY(bool anyConnected READ anyConnected NOTIFY anyConnectedChanged)

public:
    explicit WebRtcStreamManager(QObject *parent = nullptr);
    ~WebRtcStreamManager();

    WebRtcClient* frontClient() const { return m_front; }
    WebRtcClient* rearClient() const { return m_rear; }
    WebRtcClient* leftClient() const { return m_left; }
    WebRtcClient* rightClient() const { return m_right; }

    bool anyConnected() const;

    /** 设置当前 VIN，供 connectFourStreams 构造 VIN-prefixed 流名 */
    void setCurrentVin(const QString &vin) { m_vin = vin; }
    QString currentVin() const { return m_vin; }

    /**
     * 从 WHEP URL 解析 base URL 与 app，再连接四路（流名加 VIN 前缀）；
     * 若 whepUrl 为空则用 ZLM_VIDEO_URL 环境变量。
     */
    Q_INVOKABLE void connectFourStreams(const QString &whepUrl = QString());
    /** 断开四路 */
    Q_INVOKABLE void disconnectAll();

signals:
    void anyConnectedChanged();

private:
    /** 诊断：每 15s 查询一次 ZLM getMediaList，验证推流是否在册 */
    void checkZlmStreamRegistration();
    QString baseUrlFromWhep(const QString &whepUrl) const;
    QString appFromWhep(const QString &whepUrl) const;
    /** 解析并规范化 base URL；空时返回空字符串（由调用方决定 fallback） */
    QString resolveBaseUrl(const QString &whepUrl) const;

    Q_INVOKABLE QString getStreamDebugInfo() const;
    Q_INVOKABLE int getQmlSignalReceiverCount() const;
    /** 诊断：各路独立信号接收者计数（对比前端/QML 诊断 rc=0 问题）*/
    Q_INVOKABLE int getFrontSignalReceiverCount() const;
    Q_INVOKABLE int getRearSignalReceiverCount() const;
    Q_INVOKABLE int getLeftSignalReceiverCount() const;
    Q_INVOKABLE int getRightSignalReceiverCount() const;
    /** 诊断：返回每个 WebRtcClient 的 videoFrameReady 信号元数据（索引、参数数量、签名），
     *  用于确认 QML 看到的信号签名是否与 C++ 声明一致。
     *  返回格式："front: index=N params=N sig=xxx | rear: ..." */
    Q_INVOKABLE QString getStreamSignalMetaInfo() const;
    Q_INVOKABLE void dumpStreamInfo() const;
    Q_INVOKABLE void forceRefreshAllRenderers();
    WebRtcClient *m_front = nullptr;
    WebRtcClient *m_rear = nullptr;
    WebRtcClient *m_left = nullptr;
    WebRtcClient *m_right = nullptr;
    QString m_app = QStringLiteral("teleop");
    QString m_vin;  // 当前 VIN，用于构造 {vin}_cam_front 等流名
    /** 防止同一 base URL 下的无意义断连：比较新旧 base，只有实际变更才断连重建 */
    QString m_currentBase;
    QTimer *m_zlmPollTimer = nullptr;  // ZLM 流状态轮询定时器
    /** 防止 ZLM 流状态检查 log 刷屏：同状态 diff 时才打日志 */
    QString m_lastZlmStreamsSeen;
};

#endif // WEBRTCSTREAMMANAGER_H
