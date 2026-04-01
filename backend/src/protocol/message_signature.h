#pragma once
#ifndef TELEOP_PROTOCOL_MESSAGE_SIGNATURE_H
#define TELEOP_PROTOCOL_MESSAGE_SIGNATURE_H

#include <string>
#include <vector>
#include <openssl/hmac.h>
#include "message_types.h"

namespace teleop::protocol {

/**
 * 消息签名器
 * 
 * 使用 HMAC-SHA256 签名，实现消息完整性和防重放
 */
class MessageSigner {
public:
    /**
     * 构造签名器
     * @param sessionSecret 会话密钥（短期）
     */
    explicit MessageSigner(const std::string& sessionSecret);

    /**
     * 计算消息签名
     * @param payload 原始消息 JSON
     * @return HMAC-SHA256 签名（hex 格式）
     */
    std::string sign(const std::string& payload);

    /**
     * 计算关键消息的签名
     * @return HMAC-SHA256 签名（hex 格式）
     */
    std::string sign(const DriveCommand& cmd);
    std::string sign(const ModeCommand& cmd);
    std::string sign(const EStopCommand& cmd);
    std::string sign(const Heartbeat& msg);

    /**
     * 验证消息签名
     * @param payload 原始消息 JSON
     * @param signature 签名（hex 格式）
     * @return 是否验证通过
     */
    bool verify(const std::string& payload, const std::string& signature) const;

    /**
     * 验证关键消息签名
     */
    bool verify(const DriveCommand& cmd, const std::string& signature) const;
    bool verify(const ModeCommand& cmd, const std::string& signature) const;
    bool verify(const Heartbeat& msg, const std::string& signature) const;

    /**
     * 获取原始消息内容（不含签名）
     */
    static std::string getPayload(const DriveCommand& cmd);
    static std::string getPayload(const ModeCommand& cmd);
    static std::string getPayload(const EStopCommand& cmd);
    static std::string getPayload(const Heartbeat& msg);

    /**
     * 生成 payloadHash（用于签名计算）
     */
    static std::string hashPayload(const std::string& payload);

private:
    std::string sessionSecret_;

    // 生成签名
    std::string generateHMAC(
        const std::string& data,
        const std::string& key
    );
};

/**
 * 消息验证器
 * 
 * 验证消息签名、序列号、时间戳等
 */
class MessageVerifier {
public:
    /**
     * 构造验证器
     * @param sessionSecret 会话密钥
     * @param clockSkewMs 允许的时间戳偏差（毫秒，默认±2秒）
     */
    explicit MessageVerifier(const std::string& sessionSecret, int32_t clockSkewMs = 2000);

    /**
     * 验证驱动指令
     * @param cmd 驱动指令
     * @return 验证结果
     */
    struct ValidationResult {
        bool signatureValid;
        bool seqValid;
        bool timestampValid;
        bool boundsValid;
        bool deadmanEngaged;
        bool frequencyValid;
        std::string failureReason;
    };

    ValidationResult validate(const DriveCommand& cmd, uint32_t lastSeq);

    /**
     * 验证控制指令
     */
    ValidationResult validate(const ModeCommand& cmd);

    /**
     * 验证急停指令
     */
    ValidationResult validate(const EStopCommand& cmd);

    /**
     * 验证心跳消息
     */
    ValidationResult validate(const Heartbeat& msg);

    /**
     * 验证遥测数据
     */
    bool validateTelemetry(const TelemetryData& telemetry);

private:
    std::string sessionSecret_;
    int32_t clockSkewMs_;

    // 验证辅助函数
    bool verifySignature(const std::string& payload, const std::string& signature) const;
    std::string generateHMAC(const std::string& data, const std::string& key);
    bool isTimestampValid(uint64_t timestampMs) const;
    bool isBoundsValid(const std::string& payload, const std::string& type) const;
};

/**
 * 会话密钥工具
 * 
 * 生成和验证短期会话密钥
 */
class SessionSecretGenerator {
public:
    /**
     * 生成随机会话密钥
     * @return 随机密钥（32字节十六进制）
     */
    static std::string generate();

    /**
     * 生成指定长度的密钥
     * @param length 字节数（默认32）
     * @return 随机密钥（十六进制）
     */
    static std::string generate(size_t length = 32);

    /**
     * 验证密钥格式
     * @param secret 密钥字符串
     * @return 是否有效
     */
    static bool validate(const std::string& secret);
};

} // namespace teleop::protocol

#endif // TELEOP_PROTOCOL_MESSAGE_SIGNATURE_H
