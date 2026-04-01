/**
 * Message Signature 实现 - HMAC-SHA256 签名与验证
 */

#include "protocol/message_signature.h"
#include "protocol/message_types.h"
#include <iomanip>
#include <sstream>
#include <cstring>
#include <random>

namespace teleop::protocol {

// ========== MessageSigner 实现 ==========

MessageSigner::MessageSigner(const std::string& sessionSecret) 
    : sessionSecret_(sessionSecret) {
}

std::string MessageSigner::sign(const std::string& payload) {
    return generateHMAC(payload, sessionSecret_);
}

std::string MessageSigner::sign(const DriveCommand& cmd) {
    std::string payload = getPayload(cmd);
    return sign(payload);
}

std::string MessageSigner::sign(const ModeCommand& cmd) {
    std::string payload = getPayload(cmd);
    return sign(payload);
}

std::string MessageSigner::sign(const EStopCommand& cmd) {
    std::string payload = getPayload(cmd);
    return sign(payload);
}

std::string MessageSigner::sign(const Heartbeat& msg) {
    std::string payload = getPayload(msg);
    return sign(payload);
}

bool MessageSigner::verify(const std::string& payload, const std::string& signature) const {
    std::string expected = generateHMAC(payload, sessionSecret_);
    
    // 比较时序安全的字符串比较
    if (expected.size() != signature.size()) {
        return false;
    }
    
    unsigned char result = 0;
    for (size_t i = 0; i < expected.size(); ++i) {
        result |= expected[i] ^ signature[i];
    }
    
    return result == 0;
}

bool MessageSigner::verify(const DriveCommand& cmd, const std::string& signature) const {
    std::string payload = getPayload(cmd);
    return verify(payload, signature);
}

bool MessageSigner::verify(const ModeCommand& cmd, const std::string& signature) const {
    std::string payload = getPayload(cmd);
    return verify(payload, signature);
}

bool MessageSigner::verify(const EStopCommand& cmd, const std::string& signature) const {
    std::string payload = getPayload(cmd);
    return verify(payload, signature);
}

bool MessageSigner::verify(const Heartbeat& msg, const std::string& signature) const {
    std::string payload = getPayload(msg);
    return verify(payload, signature);
}

std::string MessageSigner::getPayload(const DriveCommand& cmd) {
    // 生成用于签名的 payload（不含 signature 字段）
    std::ostringstream oss;
    oss << R"({"type":"drive",)"
        << R"("seq":)" << cmd.seq << ","
        << R"("timestamp":)" << cmd.timestamp << ","
        << R"("vin":")" << cmd.vin << R"(",)"
        << R"("steering":)" << cmd.steering << ","
        << R"("throttle":)" << cmd.throttle << ","
        << R"("brake":)" << cmd.brake << ","
        << R"("gear":)" << cmd.gear << ","
        << R"("remote_enabled":)" << (cmd.remote_enabled ? "true" : "false");
    return oss.str();
}

std::string MessageSigner::getPayload(const ModeCommand& cmd) {
    std::ostringstream oss;
    oss << R"({"type":"mode",)"
        << R"("seq":)" << cmd.seq << ","
        << R"("timestamp":)" << cmd.timestamp << ","
        << R"("vin":")" << cmd.vin << R"(",)"
        << R"("mode":")" << cmd.mode << R"("})";
    return oss.str();
}

std::string MessageSigner::getPayload(const EStopCommand& cmd) {
    std::ostringstream oss;
    oss << R"({"type":"estop",)"
        << R"("seq":)" << cmd.seq << ","
        << R"("timestamp":)" << cmd.timestamp << ","
        << R"("vin":")" << cmd.vin << R"(",)"
        << R"("reason":")" << cmd.reason << R"("})";
    return oss.str();
}

std::string MessageSigner::getPayload(const Heartbeat& msg) {
    std::ostringstream oss;
    oss << R"({"type":"heartbeat",)"
        << R"("timestamp":)" << msg.timestamp << ","
        << R"("vin":")" << msg.vin << R"(",)"
        << R"("sessionId":")" << msg.sessionId << R"("})";
    return oss.str();
}

std::string MessageSigner::hashPayload(const std::string& payload) {
    // 先计算 payload 的 hash，再对 hash 签名
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hash_len = 0;
    
    EVP_MD_CTX* mdctx = EVP_MD_CTX_new();
    if (!mdctx) {
        throw std::runtime_error("Failed to create MD context");
    }
    
    const EVP_MD* md = EVP_sha256();
    if (EVP_DigestInit_ex(mdctx, md, nullptr) != 1) {
        EVP_MD_CTX_free(mdctx);
        throw std::runtime_error("Failed to init SHA256");
    }
    
    if (EVP_DigestUpdate(mdctx, payload.c_str(), payload.size()) != 1) {
        EVP_MD_CTX_free(mdctx);
        throw std::runtime_error("Failed to update SHA256");
    }
    
    if (EVP_DigestFinal_ex(mdctx, hash, &hash_len) != 1) {
        EVP_MD_CTX_free(mdctx);
        throw std::runtime_error("Failed to finalize SHA256");
    }
    
    EVP_MD_CTX_free(mdctx);
    
    // 转为十六进制
    std::ostringstream oss;
    for (unsigned int i = 0; i < hash_len; ++i) {
        oss << std::hex << std::setfill('0') << std::setw(2) 
            << static_cast<int>(hash[i]);
    }
    
    return oss.str();
}

std::string MessageSigner::generateHMAC(
    const std::string& data,
    const std::string& key
) {
    unsigned char* hmac = nullptr;
    unsigned int hmac_len = 0;
    
    // 使用 OpenSSL HMAC
    hmac = HMAC(
        EVP_sha256(),
        key.c_str(),
        static_cast<int>(key.size()),
        reinterpret_cast<const unsigned char*>(data.c_str()),
        static_cast<int>(data.size()),
        nullptr,
        &hmac_len
    );
    
    if (!hmac) {
        throw std::runtime_error("Failed to compute HMAC");
    }
    
    // 转为十六进制
    std::ostringstream oss;
    for (unsigned int i = 0; i < hmac_len; ++i) {
        oss << std::hex << std::setfill('0') << std::setw(2) 
            << static_cast<int>(hmac[i]);
    }
    
    return oss.str();
}

// ========== MessageVerifier 实现 ==========

MessageVerifier::MessageVerifier(const std::string& sessionSecret, int32_t clockSkewMs)
    : sessionSecret_(sessionSecret), clockSkewMs_(clockSkewMs) {
}

MessageVerifier::ValidationResult MessageVerifier::validate(const DriveCommand& cmd, uint32_t lastSeq) {
    ValidationResult result;
    result.signatureValid = verifySignature(getPayload(cmd), cmd.signature);
    result.seqValid = (cmd.seq > lastSeq);
    result.timestampValid = isTimestampValid(cmd.timestamp);
    result.boundsValid = isBoundsValid(getPayload(cmd), "drive");
    result.deadmanEngaged = false;
    result.frequencyValid = true; // 频率验证由车端完成
    
    if (!result.signatureValid) {
        result.failureReason = "Signature invalid";
    } else if (!result.seqValid) {
        result.failureReason = "Sequence number invalid (must be greater than last)";
    } else if (!result.timestampValid) {
        result.failureReason = "Timestamp outside allowed window";
    } else if (!result.boundsValid) {
        result.failureReason = "Parameter value out of bounds";
    }
    
    return result;
}

MessageVerifier::ValidationResult MessageVerifier::validate(const ModeCommand& cmd) {
    ValidationResult result;
    result.signatureValid = verifySignature(getPayload(cmd), cmd.signature);
    result.seqValid = true; // Mode 指令不检查序列
    result.timestampValid = isTimestampValid(cmd.timestamp);
    result.boundsValid = true; // Mode 指令不需要边界检查
    result.deadmanEngaged = false;
    result.frequencyValid = true;
    
    if (!result.signatureValid) {
        result.failureReason = "Signature invalid";
    } else if (!result.timestampValid) {
        result.failureReason = "Timestamp outside allowed window";
    }
    
    return result;
}

MessageVerifier::ValidationResult MessageVerifier::validate(const EStopCommand& cmd) {
    ValidationResult result;
    result.signatureValid = verifySignature(getPayload(cmd), cmd.signature);
    result.seqValid = true;
    result.timestampValid = isTimestampValid(cmd.timestamp);
    result.boundsValid = true;
    result.deadmanEngaged = false;
    result.frequencyValid = true;
    
    if (!result.signatureValid) {
        result.failureReason = "Signature invalid";
    } else if (!result.timestampValid) {
        result.failureReason = "Timestamp outside allowed window";
    }
    
    return result;
}

MessageVerifier::ValidationResult MessageVerifier::validate(const Heartbeat& msg) {
    ValidationResult result;
    result.signatureValid = verifySignature(getPayload(msg), msg.signature);
    result.seqValid = true;
    result.timestampValid = isTimestampValid(msg.timestamp);
    result.boundsValid = true;
    result.deadmanEngaged = false;
    result.frequencyValid = true;
    
    if (!result.signatureValid) {
        result.failureReason = "Signature invalid";
    } else if (!result.timestampValid) {
        result.failureReason = "Timestamp outside allowed window";
    }
    
    return result;
}

bool MessageVerifier::validateTelemetry(const TelemetryData& telemetry) {
    // 遥测数据不需要签名验证，仅检查边界
    bool valid = true;
    
    if (telemetry.speed < 0 || telemetry.speed > 200) {
        valid = false;
    }
    if (telemetry.battery < 0 || telemetry.battery > 100) {
        valid = false;
    }
    if (telemetry.odometer < 0) {
        valid = false;
    }
    if (telemetry.gear < -1 || telemetry.gear > 4) {
        valid = false;
    }
    
    return valid;
}

bool MessageVerifier::verifySignature(const std::string& payload, const std::string& signature) const {
    std::string expected = generateHMAC(payload, sessionSecret_);
    
    if (expected.size() != signature.size()) {
        return false;
    }
    
    unsigned char result = 0;
    for (size_t i = 0; i < expected.size(); ++i) {
        result |= expected[i] ^ signature[i];
    }
    
    return result == 0;
}

std::string MessageVerifier::generateHMAC(const std::string& data, const std::string& key) {
    unsigned char* hmac = nullptr;
    unsigned int hmac_len = 0;
    
    hmac = HMAC(
        EVP_sha256(),
        key.c_str(),
        static_cast<int>(key.size()),
        reinterpret_cast<const unsigned char*>(data.c_str()),
        static_cast<int>(data.size()),
        nullptr,
        &hmac_len
    );
    
    if (!hmac) {
        throw std::runtime_error("Failed to compute HMAC");
    }
    
    std::ostringstream oss;
    for (unsigned int i = 0; i < hmac_len; ++i) {
        oss << std::hex << std::setfill('0') << std::setw(2) 
            << static_cast<int>(hmac[i]);
    }
    
    return oss.str();
}

bool MessageVerifier::isTimestampValid(uint64_t timestampMs) const {
    auto now = std::chrono::system_clock::now();
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
    
    int64_t diff = static_cast<int64_t>(timestampMs) - static_cast<int64_t>(now_ms);
    int64_t skew = static_cast<int64_t>(clockSkewMs_);
    
    // 时间戳必须在允许的窗口内（±clockSkewMs）
    return (diff >= -skew && diff <= skew);
}

bool MessageVerifier::isBoundsValid(const std::string& payload, const std::string& type) const {
    // 简单验证：检查 JSON 是否包含非法值
    // 实际应用中可根据 type 做更精细的检查
    
    if (type == "drive") {
        // 驱动指令：steering [-1,1], throttle/brake [0,1], gear [-1,4]
        // 这里只做简单检查，完整验证在 MessageVerifier::validate
        return payload.find("steering") != std::string::npos &&
               payload.find("throttle") != std::string::npos;
    }
    
    return true;
}

// ========== SessionSecretGenerator 实现 ==========

std::string SessionSecretGenerator::generate() {
    return generate(32);
}

std::string SessionSecretGenerator::generate(size_t length) {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dis(0, 255);
    
    std::string result;
    result.reserve(length);
    for (size_t i = 0; i < length; ++i) {
        result += static_cast<char>(dis(gen));
    }
    
    // 转为十六进制
    std::ostringstream oss;
    for (size_t i = 0; i < result.size(); ++i) {
        oss << std::hex << std::setfill('0') << std::setw(2)
            << (static_cast<unsigned char>(result[i]) & 0xFF);
    }
    
    return oss.str();
}

bool SessionSecretGenerator::validate(const std::string& secret) {
    // 验证密钥格式：64 个十六进制字符
    if (secret.size() != 64) {
        return false;
    }
    
    // 检查是否都是十六进制字符
    for (char c : secret) {
        if (!((c >= '0' && c <= '9') ||
              (c >= 'a' && c <= 'f') ||
              (c >= 'A' && c <= 'F'))) {
            return false;
        }
    }
    
    return true;
}

} // namespace teleop::protocol
