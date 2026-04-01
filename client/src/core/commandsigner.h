#pragma once
#include <QByteArray>
#include <QJsonObject>
#include <QString>

/**
 * 控制命令 HMAC-SHA256 签名与验证（《客户端架构设计》§3 安全要求）。
 *
 * 签名覆盖字段：steering, throttle, brake, gear, emergency_stop,
 *               timestamp, seq, session_id, vin
 * 签名密钥 = HMAC-SHA256(sessionId | vin | token)，在 setCredentials 时派生。
 *
 * 发送方（客户端）：
 *   1) 构建 JSON（不含 hmac 字段）
 *   2) sign(json) → 添加 "hmac" 字段
 *   3) 发送
 *
 * 接收方（服务端 / 验证者）：
 *   1) 取出 "hmac" 字段
 *   2) 对剩余字段重新计算 HMAC
 *   3) 时间安全比较
 *
 * 线程安全：setCredentials 与 sign/verify 之间需外部同步（正常使用中
 * credentials 在会话建立时设置一次，之后只读）。
 */
class CommandSigner {
public:
    CommandSigner() = default;

    /**
     * 设置会话凭证，派生签名密钥。
     * @param vin       车辆 VIN
     * @param sessionId 会话 ID
     * @param token     认证令牌
     */
    void setCredentials(const QString& vin, const QString& sessionId, const QString& token);

    /**
     * 清除凭证（会话结束时调用）。
     */
    void clearCredentials();

    /**
     * 是否已初始化（有有效凭证）。
     */
    bool isReady() const { return !m_signingKey.isEmpty(); }

    /**
     * 对 JSON 对象签名，原地添加 "hmac" 字段。
     * @param json  输入/输出 JSON
     * @return true = 成功，false = 未初始化
     */
    bool sign(QJsonObject& json) const;

    /**
     * 验证 JSON 对象中的 "hmac" 字段。
     * @param json      待验证的 JSON（含 "hmac" 字段）
     * @param reason    失败原因（out）
     * @return true = 签名有效
     */
    bool verify(const QJsonObject& json, QString* reason = nullptr) const;

    /**
     * 计算规范化载荷的 HMAC（可用于测试）。
     */
    QByteArray computeHmac(const QByteArray& canonicalPayload) const;

private:
    static QByteArray canonicalize(const QJsonObject& json);
    static QByteArray deriveKey(const QString& vin, const QString& sessionId, const QString& token);

    QByteArray m_signingKey;
};
