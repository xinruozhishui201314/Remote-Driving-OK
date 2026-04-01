/**
 * Common Utils - 通用工具函数
 * 
 * 功能：
 * - UUID 生成/解析
 * - 时间戳处理
 * - JSON 辅助函数
 * - 字符串处理
 */

#ifndef TELEOP_COMMON_UTILS_H
#define TELEOP_COMMON_UTILS_H

#include <string>
#include <vector>
#include <chrono>
#include <random>
#include <optional>
#include <nlohmann/json.hpp>

namespace teleop::common {

/**
 * UUID v4 生成器
 */
class UuidGenerator {
public:
    /**
     * 生成新的 UUID v4
     * @return UUID 字符串（无花括号，无连字符）
     */
    static std::string generate();
    
    /**
     * 验证 UUID 格式
     * @param uuid UUID 字符串
     * @return 是否有效
     */
    static bool validate(const std::string& uuid);
    
    /**
     * 规范化 UUID（移除花括号和连字符）
     * @param uuid 原始 UUID
     * @return 规范化后的 UUID
     */
    static std::string normalize(const std::string& uuid);
};

/**
 * 时间戳工具
 */
class Timestamp {
public:
    /**
     * 获取当前时间戳（毫秒）
     * @return 毫秒时间戳
     */
    static int64_t now_ms();
    
    /**
     * 获取当前时间戳（秒）
     * @return 秒时间戳
     */
    static int64_t now_sec();
    
    /**
     * 获取 ISO8601 格式的时间字符串
     * @param timestamp_ms 毫秒时间戳
     * @return ISO8601 字符串
     */
    static std::string to_iso8601(int64_t timestamp_ms);
    
    /**
     * 从 ISO8601 字符串解析时间戳
     * @param iso_str ISO8601 字符串
     * @return 毫秒时间戳（失败返回 nullopt）
     */
    static std::optional<int64_t> from_iso8601(const std::string& iso_str);
    
    /**
     * 格式化时间戳为可读字符串
     * @param timestamp_ms 毫秒时间戳
     * @return 格式化字符串
     */
    static std::string format(int64_t timestamp_ms, const std::string& format = "%Y-%m-%d %H:%M:%S");
};

/**
 * JSON 辅助函数
 */
class JsonHelper {
public:
    /**
     * 安全获取字符串值
     * @param json JSON 对象
     * @param key 键
     * @param default_val 默认值
     * @return 字符串值
     */
    static std::string get_string(
        const nlohmann::json& json,
        const std::string& key,
        const std::string& default_val = ""
    );
    
    /**
     * 安全获取整数值
     * @param json JSON 对象
     * @param key 键
     * @param default_val 默认值
     * @return 整数值
     */
    static int get_int(
        const nlohmann::json& json,
        const std::string& key,
        int default_val = 0
    );
    
    /**
     * 安全获取浮点数值
     * @param json JSON 对象
     * @param key 键
     * @param default_val 默认值
     * @return 浮点数值
     */
    static double get_double(
        const nlohmann::json& json,
        const std::string& key,
        double default_val = 0.0
    );
    
    /**
     * 安全获取布尔值
     * @param json JSON 对象
     * @param key 键
     * @param default_val 默认值
     * @return 布尔值
     */
    static bool get_bool(
        const nlohmann::json& json,
        const std::string& key,
        bool default_val = false
    );
    
    /**
     * 检查 JSON 对象是否包含键
     * @param json JSON 对象
     * @param key 键
     * @return 是否包含
     */
    static bool has_key(
        const nlohmann::json& json,
        const std::string& key
    );
};

/**
 * 字符串工具
 */
class StringUtils {
public:
    /**
     * 去除字符串两端空白
     * @param str 原始字符串
     * @return 去除空白后的字符串
     */
    static std::string trim(const std::string& str);
    
    /**
     * 分割字符串
     * @param str 原始字符串
     * @param delimiter 分隔符
     * @return 分割后的字符串数组
     */
    static std::vector<std::string> split(
        const std::string& str,
        char delimiter
    );
    
    /**
     * 连接字符串数组
     * @param parts 字符串数组
     * @param delimiter 分隔符
     * @return 连接后的字符串
     */
    static std::string join(
        const std::vector<std::string>& parts,
        const std::string& delimiter = ","
    );
    
    /**
     * 转换为小写
     * @param str 原始字符串
     * @return 小写字符串
     */
    static std::string to_lower(const std::string& str);
    
    /**
     * 转换为大写
     * @param str 原始字符串
     * @return 大写字符串
     */
    static std::string to_upper(const std::string& str);
    
    /**
     * 检查字符串是否为空或仅空白
     * @param str 字符串
     * @return 是否为空
     */
    static bool is_empty(const std::string& str);
    
    /**
     * Base64 编码
     * @param data 原始数据
     * @return Base64 字符串
     */
    static std::string base64_encode(const std::vector<uint8_t>& data);
    
    /**
     * Base64 解码
     * @param encoded Base64 字符串
     * @return 解码后的数据（失败返回空）
     */
    static std::vector<uint8_t> base64_decode(const std::string& encoded);
};

/**
 * 验证工具
 */
class ValidationUtils {
public:
    /**
     * 验证 VIN 格式
     * @param vin VIN 码
     * @return 是否有效
     */
    static bool is_valid_vin(const std::string& vin);
    
    /**
     * 验证邮箱格式
     * @param email 邮箱地址
     * @return 是否有效
     */
    static bool is_valid_email(const std::string& email);
    
    /**
     * 验证用户名格式
     * @param username 用户名
     * @return 是否有效（非空、长度 3-50）
     */
    static bool is_valid_username(const std::string& username);
    
    /**
     * 验证 JSON Web Token 格式
     * @param token JWT 字符串
     * @return 是否有效（三个部分，用点分隔）
     */
    static bool is_valid_jwt_format(const std::string& token);
};

/**
 * 环境变量工具
 */
class EnvUtils {
public:
    /**
     * 获取环境变量（带默认值）
     * @param key 环境变量名
     * @param default_val 默认值
     * @return 环境变量值
     */
    static std::string get(
        const std::string& key,
        const std::string& default_val = ""
    );
    
    /**
     * 获取整型环境变量
     * @param key 环境变量名
     * @param default_val 默认值
     * @return 整型值
     */
    static int get_int(
        const std::string& key,
        int default_val = 0
    );
    
    /**
     * 获取布尔型环境变量
     * @param key 环境变量名
     * @param default_val 默认值
     * @return 布尔值（true/false/yes/no/1/0）
     */
    static bool get_bool(
        const std::string& key,
        bool default_val = false
    );
    
    /**
     * 检查环境变量是否设置
     * @param key 环境变量名
     * @return 是否已设置
     */
    static bool is_set(const std::string& key);
};

} // namespace teleop::common

#endif // TELEOP_COMMON_UTILS_H
