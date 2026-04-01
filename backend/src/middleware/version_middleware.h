/**
 * Version Middleware - 版本协商中间件
 * 
 * 功能：
 * - 解析和验证API版本号（Semantic Versioning）
 * - 检查客户端版本与后端版本的兼容性
 * - 根据客户端版本选择响应字段
 * - 记录版本协商日志
 */

#ifndef TELEOP_MIDDLEWARE_VERSION_MIDDLWARE_H
#define TELEOP_MIDDLEWARE_VERSION_MIDDLWARE_H

#include <string>
#include <unordered_map>
#include <vector>
#include <set>
#include <optional>

namespace teleop::middleware {

/**
 * Semantic Version (semver) 结构
 * 遵循 MAJOR.MINOR.PATCH 格式
 */
struct Version {
    int major;
    int minor;
    int patch;
    
    /**
     * 从字符串解析版本号
     * @param version_str 版本字符串，如 "1.0.0"
     * @return Version 对象（解析失败返回 {0,0,0}）
     */
    static Version parse(const std::string& version_str);
    
    /**
     * 转换为字符串
     * @return 版本字符串
     */
    std::string to_string() const;
    
    /**
     * 比较版本大小
     * @param other 另一个版本
     * @return -1（this < other），0（相等），1（this > other）
     */
    int compare(const Version& other) const;
    
    /**
     * 检查是否与客户端版本兼容
     * 规则：
     * - 主版本必须一致
     * - 客户端次版本 ≤ 服务端次版本（向后兼容）
     * @param client_version 客户端版本
     * @return 是否兼容
     */
    bool is_compatible_with(const Version& client_version) const;
    
    /**
     * 重载比较运算符
     */
    bool operator<(const Version& other) const;
    bool operator<=(const Version& other) const;
    bool operator==(const Version& other) const;
    bool operator!=(const Version& other) const;
    bool operator>=(const Version& other) const;
    bool operator>(const Version& other) const;
};

/**
 * 版本协商中间件
 * 
 * 使用示例：
 * ```
 * VersionMiddleware middleware("1.1.0");
 * 
 * std::string error_msg;
 * if (!middleware.validate_client_version("1.0.0", error_msg)) {
 *     // 返回 400 错误
 *     res.status = 400;
 *     res.set_content(error_msg, "application/json");
 *     return;
 * }
 * 
 * // 获取响应版本
 * std::string response_version = middleware.get_response_version(client_version);
 * ```
 */
class VersionMiddleware {
public:
    /**
     * 构造函数
     * @param current_backend_version 当前后端版本
     */
    explicit VersionMiddleware(const std::string& current_backend_version = "1.1.0");
    
    /**
     * 析构函数
     */
    ~VersionMiddleware();
    
    /**
     * 验证客户端请求版本是否兼容
     * @param client_version 客户端版本字符串
     * @param error_msg 输出错误信息（验证失败时）
     * @return 是否兼容
     */
    bool validate_client_version(const std::string& client_version, std::string& error_msg);
    
    /**
     * 根据客户端版本选择响应版本
     * 规则：
     * - 如果客户端版本 < 后端版本，使用客户端版本（向后兼容）
     * - 否则使用后端版本
     * @param client_version 客户端版本
     * @return 响应应使用的版本
     */
    std::string get_response_version(const Version& client_version) const;
    
    /**
     * 获取当前后端版本
     * @return 版本字符串
     */
    std::string get_backend_version() const;
    
    /**
     * 获取当前后端版本（Version对象）
     * @return Version 对象
     */
    Version get_backend_version_obj() const;
    
    /**
     * 注册API的最小客户端版本
     * @param api_path API路径（如 "/api/v1/vins"）
     * @param min_version 最小客户端版本
     */
    void register_min_version(const std::string& api_path, const std::string& min_version);
    
    /**
     * 检查特定API的客户端版本要求
     * @param api_path API路径
     * @param client_version 客户端版本
     * @param error_msg 输出错误信息
     * @return 是否满足要求
     */
    bool validate_api_version(const std::string& api_path, 
                            const std::string& client_version, 
                            std::string& error_msg);
    
    /**
     * 获取支持的所有版本列表
     * @return 版本列表
     */
    std::vector<std::string> get_supported_versions() const;
    
    /**
     * 检查版本是否在支持列表中
     * @param version 版本字符串
     * @return 是否支持
     */
    bool is_version_supported(const std::string& version) const;
    
    /**
     * 设置版本协商日志级别
     * @param enabled 是否启用详细日志
     */
    void set_detailed_logging(bool enabled);

private:
    Version m_current_version;
    std::unordered_map<std::string, Version> m_min_client_versions;  // API路径 -> 最小客户端版本
    std::set<std::string> m_supported_versions;  // 支持的版本列表
    bool m_detailed_logging = false;
    
    /**
     * 初始化默认的API版本要求
     */
    void initialize_default_versions();
    
    /**
     * 记录版本协商日志
     * @param level 日志级别（INFO/WARN/ERROR）
     * @param message 日志消息
     */
    void log(const std::string& level, const std::string& message) const;
};

/**
 * 版本协商结果
 */
struct VersionNegotiationResult {
    bool success;
    std::string client_version;
    std::string negotiated_version;
    std::string error_message;
    
    /**
     * 创建成功结果
     */
    static VersionNegotiationResult success_result(
        const std::string& client_version,
        const std::string& negotiated_version
    );
    
    /**
     * 创建失败结果
     */
    static VersionNegotiationResult failure_result(
        const std::string& client_version,
        const std::string& error_message
    );
};

/**
 * 辅助函数：从HTTP请求头提取版本
 * @param header_value Header值（如 "1.0.0"）
 * @return 解析的版本（失败返回 nullopt）
 */
std::optional<Version> extract_version_from_header(const std::string& header_value);

/**
 * 辅助函数：构建版本响应头
 * @param version 版本字符串
 * @return Header值
 */
std::string build_version_header(const std::string& version);

/**
 * 辅助函数：构建版本错误响应
 * @param error_type 错误类型
 * @param client_version 客户端版本
 * @param server_version 服务端版本
 * @return JSON错误响应字符串
 */
std::string build_version_error_response(
    const std::string& error_type,
    const std::string& client_version,
    const std::string& server_version
);

} // namespace teleop::middleware

#endif // TELEOP_MIDDLEWARE_VERSION_MIDDLWARE_H
