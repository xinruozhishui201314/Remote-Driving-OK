/**
 * Version Middleware - 版本协商中间件实现
 */

#include "middleware/version_middleware.h"
#include "common/utils.h"
#include <nlohmann/json.hpp>
#include <chrono>
#include <sstream>
#include <algorithm>
#include <regex>
#include <iostream>
#include <iomanip>

namespace teleop::middleware {

// ============================================================================
// Version 实现
// ============================================================================

Version Version::parse(const std::string& version_str) {
    Version v{0, 0, 0};
    
    // 使用正则表达式匹配版本号：major.minor.patch
    std::regex version_regex(R"(^(\d+)\.(\d+)\.(\d+)(?:-([a-zA-Z0-9]+))?$)");
    std::smatch match;
    
    if (std::regex_match(version_str, match, version_regex) && match.size() >= 4) {
        try {
            v.major = std::stoi(match[1].str());
            v.minor = std::stoi(match[2].str());
            v.patch = std::stoi(match[3].str());
        } catch (const std::exception& e) {
            std::cerr << "[Version] Failed to parse version '" << version_str << "': " << e.what() << std::endl;
        }
    } else {
        std::cerr << "[Version] Invalid version format: " << version_str << std::endl;
    }
    
    return v;
}

std::string Version::to_string() const {
    std::ostringstream oss;
    oss << major << "." << minor << "." << patch;
    return oss.str();
}

int Version::compare(const Version& other) const {
    if (major != other.major) {
        return (major < other.major) ? -1 : 1;
    }
    if (minor != other.minor) {
        return (minor < other.minor) ? -1 : 1;
    }
    if (patch != other.patch) {
        return (patch < other.patch) ? -1 : 1;
    }
    return 0;
}

bool Version::is_compatible_with(const Version& client_version) const {
    // 主版本必须一致
    if (major != client_version.major) {
        return false;
    }
    
    // 客户端次版本不能超过服务端（向后兼容）
    // 客户端可以比服务端旧，但不能更新
    return client_version.minor <= minor;
}

// 运算符重载
bool Version::operator<(const Version& other) const {
    return compare(other) < 0;
}

bool Version::operator<=(const Version& other) const {
    return compare(other) <= 0;
}

bool Version::operator==(const Version& other) const {
    return compare(other) == 0;
}

bool Version::operator!=(const Version& other) const {
    return compare(other) != 0;
}

bool Version::operator>=(const Version& other) const {
    return compare(other) >= 0;
}

bool Version::operator>(const Version& other) const {
    return compare(other) > 0;
}

// ============================================================================
// VersionMiddleware 实现
// ============================================================================

VersionMiddleware::VersionMiddleware(const std::string& current_backend_version)
    : m_current_version(Version::parse(current_backend_version))
{
    initialize_default_versions();
    
    // 添加当前版本到支持列表
    m_supported_versions.insert(m_current_version.to_string());
    
    // 添加向后兼容的版本（1.0.0）
    if (m_current_version.major == 1 && m_current_version.minor >= 0) {
        m_supported_versions.insert("1.0.0");
    }
    
    log("INFO", "VersionMiddleware initialized with backend version: " + m_current_version.to_string());
}

VersionMiddleware::~VersionMiddleware() {
    log("INFO", "VersionMiddleware destroyed");
}

bool VersionMiddleware::validate_client_version(const std::string& client_version, std::string& error_msg) {
    Version client_ver = Version::parse(client_version);
    
    // 检查版本是否为 0.0.0（表示未提供）
    if (client_ver.major == 0 && client_ver.minor == 0 && client_ver.patch == 0) {
        // 未提供版本，使用默认兼容策略
        log("WARN", "Client did not provide version, using default compatibility");
        return true;
    }
    
    // 检查版本是否在支持列表中
    if (!is_version_supported(client_version)) {
        error_msg = "Client version '" + client_version + "' is not supported. "
                   "Supported versions: " + teleop::common::StringUtils::join(
                       std::vector<std::string>(m_supported_versions.begin(), m_supported_versions.end()),
                       ", "
                   );
        log("WARN", error_msg);
        return false;
    }
    
    // 检查兼容性
    if (!m_current_version.is_compatible_with(client_ver)) {
        error_msg = "Version mismatch: client=" + client_version + 
                   " is not compatible with server=" + m_current_version.to_string();
        log("WARN", error_msg);
        return false;
    }
    
    if (m_detailed_logging) {
        log("INFO", "Client version " + client_version + " validated successfully");
    }
    
    return true;
}

std::string VersionMiddleware::get_response_version(const Version& client_version) const {
    // 如果客户端版本 < 服务端版本，使用客户端版本（向后兼容）
    // 否则使用服务端版本
    if (client_version < m_current_version) {
        return client_version.to_string();
    } else {
        return m_current_version.to_string();
    }
}

std::string VersionMiddleware::get_backend_version() const {
    return m_current_version.to_string();
}

Version VersionMiddleware::get_backend_version_obj() const {
    return m_current_version;
}

void VersionMiddleware::register_min_version(const std::string& api_path, const std::string& min_version) {
    m_min_client_versions[api_path] = Version::parse(min_version);
    log("INFO", "Registered minimum version for " + api_path + ": " + min_version);
}

bool VersionMiddleware::validate_api_version(const std::string& api_path, 
                                           const std::string& client_version, 
                                           std::string& error_msg) {
    // 查找API的最小版本要求
    auto it = m_min_client_versions.find(api_path);
    if (it != m_min_client_versions.end()) {
        Version client_ver = Version::parse(client_version);
        Version min_ver = it->second;
        
        if (client_ver < min_ver) {
            error_msg = "API '" + api_path + "' requires at least version " + min_ver.to_string() + 
                       ", but client is using " + client_version;
            log("WARN", error_msg);
            return false;
        }
    }
    
    return true;
}

std::vector<std::string> VersionMiddleware::get_supported_versions() const {
    return std::vector<std::string>(m_supported_versions.begin(), m_supported_versions.end());
}

bool VersionMiddleware::is_version_supported(const std::string& version) const {
    return m_supported_versions.find(version) != m_supported_versions.end();
}

void VersionMiddleware::set_detailed_logging(bool enabled) {
    m_detailed_logging = enabled;
    log("INFO", std::string("Detailed logging ") + (enabled ? "enabled" : "disabled"));
}

void VersionMiddleware::initialize_default_versions() {
    // 注册各API的最小客户端版本要求
    // /api/v1/me: 1.0.0
    register_min_version("/api/v1/me", "1.0.0");
    
    // /api/v1/vins: 1.0.0
    register_min_version("/api/v1/vins", "1.0.0");
    
    // /api/v1/vins/{vin}/sessions: 1.0.0
    register_min_version("/api/v1/vins/*/sessions", "1.0.0");
    
    // /api/v1/sessions/*: 1.0.0
    register_min_version("/api/v1/sessions/*", "1.0.0");
    
    // /api/v1/vins/*: 1.0.0
    register_min_version("/api/v1/vins/*", "1.0.0");
}

void VersionMiddleware::log(const std::string& level, const std::string& message) const {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
    oss << " [Backend][VersionMiddleware] [" << level << "] " << message;
    
    if (level == "ERROR") {
        std::cerr << oss.str() << std::endl;
    } else {
        std::cout << oss.str() << std::endl;
    }
}

// ============================================================================
// VersionNegotiationResult 实现
// ============================================================================

VersionNegotiationResult VersionNegotiationResult::success_result(
    const std::string& client_version,
    const std::string& negotiated_version
) {
    VersionNegotiationResult result;
    result.success = true;
    result.client_version = client_version;
    result.negotiated_version = negotiated_version;
    result.error_message.clear();
    return result;
}

VersionNegotiationResult VersionNegotiationResult::failure_result(
    const std::string& client_version,
    const std::string& error_message
) {
    VersionNegotiationResult result;
    result.success = false;
    result.client_version = client_version;
    result.negotiated_version.clear();
    result.error_message = error_message;
    return result;
}

// ============================================================================
// 辅助函数实现
// ============================================================================

std::optional<Version> extract_version_from_header(const std::string& header_value) {
    if (header_value.empty()) {
        return std::nullopt;
    }
    
    Version ver = Version::parse(header_value);
    
    // 检查是否为有效版本（非 0.0.0）
    if (ver.major == 0 && ver.minor == 0 && ver.patch == 0) {
        return std::nullopt;
    }
    
    return ver;
}

std::string build_version_header(const std::string& version) {
    return version;
}

std::string build_version_error_response(
    const std::string& error_type,
    const std::string& client_version,
    const std::string& server_version
) {
    nlohmann::json response;
    response["error"] = error_type;
    response["clientVersion"] = client_version;
    response["serverVersion"] = server_version;
    response["timestamp"] = teleop::common::Timestamp::now_ms();
    
    if (error_type == "version_mismatch") {
        response["details"] = "Client version is incompatible with server version. "
                           "Please update your client or server.";
    } else if (error_type == "unsupported_version") {
        response["details"] = "Client version is not supported. "
                           "Supported versions: 1.0.0, 1.1.0";
    } else if (error_type == "invalid_version") {
        response["details"] = "Invalid version format. Expected format: X.Y.Z";
    }
    
    return response.dump();
}

} // namespace teleop::middleware
