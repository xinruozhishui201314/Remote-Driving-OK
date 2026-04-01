/**
 * Common Utils 实现
 */

#include "common/utils.h"
#include <sstream>
#include <algorithm>
#include <cctype>
#include <random>
#include <iomanip>
#include <regex>

namespace teleop::common {

// ========== UuidGenerator 实现 ==========

std::string UuidGenerator::generate() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<uint8_t> dis(0, 255);
    
    std::ostringstream oss;
    oss << std::hex << std::setfill('0') << std::setw(2);
    
    // time_low (4 bytes)
    for (int i = 0; i < 4; ++i) oss << static_cast<int>(dis(gen));
    oss << '-';
    
    // time_mid (2 bytes)
    for (int i = 0; i < 2; ++i) oss << static_cast<int>(dis(gen));
    oss << '-';
    
    // time_hi_and_version (2 bytes, version 0x4 = random)
    oss << static_cast<int>(dis(gen)) << static_cast<int>(dis(gen)) << '4';
    oss << '-';
    
    // clock_seq_hi_and_reserved (1 byte, variant 0x8 = random)
    uint8_t variant = 0x8 | (dis(gen) & 0x3);
    oss << static_cast<int>(variant);
    oss << '-';
    
    // clock_seq_low (1 byte)
    oss << static_cast<int>(dis(gen));
    oss << '-';
    
    // node (6 bytes)
    for (int i = 0; i < 6; ++i) oss << static_cast<int>(dis(gen));
    
    return oss.str();
}

bool UuidGenerator::validate(const std::string& uuid) {
    // UUID 格式：xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx (8-4-4-4-12 个十六进制字符)
    std::regex uuid_regex(
        "^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-"
        "[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$"
    );
    return std::regex_match(uuid, uuid_regex);
}

std::string UuidGenerator::normalize(const std::string& uuid) {
    std::string result = uuid;
    
    // 移除花括号
    if (result.size() >= 2 && result.front() == '{') result.erase(0, 1);
    if (!result.empty() && result.back() == '}') result.pop_back();
    
    // 移除空格
    result.erase(std::remove_if(result.begin(), result.end(), ::isspace), result.end());
    
    // 统一为小写
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    
    return result;
}

// ========== Timestamp 实现 ==========

int64_t Timestamp::now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

int64_t Timestamp::now_sec() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

std::string Timestamp::to_iso8601(int64_t timestamp_ms) {
    std::chrono::milliseconds ms(timestamp_ms);
    std::chrono::system_clock::time_point tp(ms);
    std::time_t tt = std::chrono::system_clock::to_time_t(tp);
    
    std::tm tm;
    gmtime_r(&tt, &tm);
    
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S");
    
    // 添加毫秒部分
    int ms_part = timestamp_ms % 1000;
    oss << '.' << std::setfill('0') << std::setw(3) << ms_part << 'Z';
    
    return oss.str();
}

std::optional<int64_t> Timestamp::from_iso8601(const std::string& iso_str) {
    // 解析 ISO8601 格式：2024-02-27T12:34:56.789Z
    std::tm tm = {};
    char ms_buf[4] = {0};
    
    if (sscanf(iso_str.c_str(), "%d-%d-%dT%d:%d:%d.%3sZ",
        &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
        &tm.tm_hour, &tm.tm_min, &tm.tm_sec, ms_buf) != 7) {
        return std::nullopt;
    }
    
    tm.tm_year -= 1900;
    tm.tm_mon -= 1;
    
    std::time_t tt = timegm(&tm);
    int64_t timestamp = static_cast<int64_t>(tt) * 1000;
    
    // 添加毫秒
    if (ms_buf[0] != 0) {
        timestamp += (ms_buf[0] - '0') * 100;
    }
    if (ms_buf[1] != 0) {
        timestamp += (ms_buf[1] - '0') * 10;
    }
    if (ms_buf[2] != 0) {
        timestamp += (ms_buf[2] - '0');
    }
    
    return timestamp;
}

std::string Timestamp::format(int64_t timestamp_ms, const std::string& fmt) {
    std::chrono::milliseconds ms(timestamp_ms);
    std::chrono::system_clock::time_point tp(ms);
    std::time_t tt = std::chrono::system_clock::to_time_t(tp);
    
    std::tm tm;
    gmtime_r(&tt, &tm);
    
    std::ostringstream oss;
    oss << std::put_time(&tm, fmt.c_str());
    
    return oss.str();
}

// ========== JsonHelper 实现 ==========

std::string JsonHelper::get_string(
    const nlohmann::json& json,
    const std::string& key,
    const std::string& default_val
) {
    auto it = json.find(key);
    if (it != json.end() && it->is_string()) {
        return it->get<std::string>();
    }
    return default_val;
}

int JsonHelper::get_int(
    const nlohmann::json& json,
    const std::string& key,
    int default_val
) {
    auto it = json.find(key);
    if (it != json.end() && it->is_number()) {
        return it->get<int>();
    }
    return default_val;
}

double JsonHelper::get_double(
    const nlohmann::json& json,
    const std::string& key,
    double default_val
) {
    auto it = json.find(key);
    if (it != json.end() && it->is_number()) {
        return it->get<double>();
    }
    return default_val;
}

bool JsonHelper::get_bool(
    const nlohmann::json& json,
    const std::string& key,
    bool default_val
) {
    auto it = json.find(key);
    if (it != json.end() && it->is_boolean()) {
        return it->get<bool>();
    }
    return default_val;
}

bool JsonHelper::has_key(const nlohmann::json& json, const std::string& key) {
    return json.find(key) != json.end();
}

// ========== StringUtils 实现 ==========

std::string StringUtils::trim(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    
    size_t end = str.find_last_not_of(" \t\n\r");
    if (end == std::string::npos) return str.substr(start);
    
    return str.substr(start, end - start + 1);
}

std::vector<std::string> StringUtils::split(const std::string& str, char delimiter) {
    std::vector<std::string> result;
    std::istringstream iss(str);
    std::string token;
    
    while (std::getline(iss, token, delimiter)) {
        result.push_back(token);
    }
    
    return result;
}

std::string StringUtils::join(const std::vector<std::string>& parts, const std::string& delimiter) {
    if (parts.empty()) return "";
    
    std::ostringstream oss;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) oss << delimiter;
        oss << parts[i];
    }
    
    return oss.str();
}

std::string StringUtils::to_lower(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
}

std::string StringUtils::to_upper(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), ::toupper);
    return result;
}

bool StringUtils::is_empty(const std::string& str) {
    for (char c : str) {
        if (!std::isspace(static_cast<unsigned char>(c))) {
            return false;
        }
    }
    return true;
}

// Base64 编码/解码
static const char base64_chars[] = 
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string StringUtils::base64_encode(const std::vector<uint8_t>& data) {
    std::string result;
    result.reserve(((data.size() + 2) / 3) * 4);
    
    size_t i = 0;
    for (size_t n = data.size(); i < n; i += 3) {
        uint32_t triple = (data[i] << 16);
        if (i + 1 < n) triple |= (data[i + 1] << 8);
        if (i + 2 < n) triple |= data[i + 2];
        
        result.push_back(base64_chars[(triple >> 18) & 0x3F]);
        result.push_back(base64_chars[(triple >> 12) & 0x3F]);
        result.push_back(base64_chars[(triple >> 6) & 0x3F]);
        
        if (i + 2 < n) {
            result.push_back(base64_chars[triple & 0x3F]);
        } else {
            result.push_back('=');
        }
    }
    
    return result;
}

static inline bool is_base64_char(char c) {
    return (isalnum(static_cast<unsigned char>(c)) || c == '+' || c == '/');
}

std::vector<uint8_t> StringUtils::base64_decode(const std::string& encoded) {
    std::vector<uint8_t> result;
    
    size_t len = encoded.size();
    if (len == 0 || len % 4 != 0) return result;
    
    result.reserve((len / 4) * 3);
    
    for (size_t i = 0; i < len; ) {
        uint32_t quadruple = 0;
        
        for (int j = 0; j < 4 && i < len; ++i, ++j) {
            char c = encoded[i];
            if (c == '=') {
                // Padding
                quadruple <<= 6;
            } else if (!is_base64_char(c)) {
                // 无效字符
                return std::vector<uint8_t>();
            } else {
                // 查找 base64 字符的值
                const char* pos = std::strchr(base64_chars, c);
                if (!pos) return std::vector<uint8_t>();
                quadruple = (quadruple << 6) | (pos - base64_chars);
            }
        }
        
        // 提取 3 个字节
        if (result.size() + 2 < result.capacity()) {
            result.push_back(static_cast<uint8_t>((quadruple >> 16) & 0xFF));
            result.push_back(static_cast<uint8_t>((quadruple >> 8) & 0xFF));
            result.push_back(static_cast<uint8_t>(quadruple & 0xFF));
        }
    }
    
    return result;
}

// ========== ValidationUtils 实现 ==========

bool ValidationUtils::is_valid_vin(const std::string& vin) {
    // VIN 格式：17 位字母和数字，最后四位是数字
    if (vin.size() != 17) return false;
    
    // 检查字符（排除 I, O, Q）
    for (char c : vin) {
        if (!(std::isalnum(static_cast<unsigned char>(c)))) return false;
        if (c == 'I' || c == 'O' || c == 'Q') return false;
    }
    
    return true;
}

bool ValidationUtils::is_valid_email(const std::string& email) {
    // 简单的邮箱格式验证
    std::regex email_regex(
        R"(^[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\.[a-zA-Z]{2,}$)"
    );
    return std::regex_match(email, email_regex);
}

bool ValidationUtils::is_valid_username(const std::string& username) {
    // 用户名：3-50 字符，可包含字母、数字、下划线、点
    if (username.size() < 3 || username.size() > 50) return false;
    
    for (char c : username) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || 
              c == '_' || c == '.')) {
            return false;
        }
    }
    
    // 不能以点开头或结尾
    if (username.front() == '.' || username.back() == '.') return false;
    
    return true;
}

bool ValidationUtils::is_valid_jwt_format(const std::string& token) {
    // JWT 格式：header.payload.signature（三个部分，用点分隔）
    int dot_count = 0;
    for (char c : token) {
        if (c == '.') dot_count++;
        if (dot_count > 2) return false;
    }
    
    return (dot_count == 2);
}

// ========== EnvUtils 实现 ==========

std::string EnvUtils::get(const std::string& key, const std::string& default_val) {
    const char* env = std::getenv(key.c_str());
    return (env != nullptr) ? std::string(env) : default_val;
}

int EnvUtils::get_int(const std::string& key, int default_val) {
    const char* env = std::getenv(key.c_str());
    if (env != nullptr) {
        try {
            return std::stoi(env);
        } catch (...) {
            return default_val;
        }
    }
    return default_val;
}

bool EnvUtils::get_bool(const std::string& key, bool default_val) {
    const char* env = std::getenv(key.c_str());
    if (env != nullptr) {
        std::string val(env);
        std::transform(val.begin(), val.end(), val.begin(), ::tolower);
        
        return (val == "1" || val == "true" || val == "yes" || val == "on");
    }
    return default_val;
}

bool EnvUtils::is_set(const std::string& key) {
    return (std::getenv(key.c_str()) != nullptr);
}

} // namespace teleop::common
