#pragma once
#ifndef TELEOP_VINI_REPOSITORY_H
#define TELEOP_VINI_REPOSITORY_H

#include <string>
#include <vector>
#include <memory>
#include <optional>

#include "telemetry_data.h"

namespace teleopdb {

// VIN 信息
struct VinInfo {
    std::string vin;
    std::string model;
    std::string capabilities;  // JSON 格式的字符串
    std::string safety_profile;  // JSON 格式的字符串
    std::string createdBy;     // 创建者
};

// VIN 授权Grant 信息
struct VinGrant {
    std::string granteeUserId;
    std::vector<std::string> permissions; // ["vin.view", "vin.control", "vin.maintain"]
    std::string created_by;
    std::string expireAt;
};

// 会话信息
struct SessionInfo {
    std::string sessionId;
    std::string vin;
    std::string controllerUserId;
    std::string state;  // REQUESTED, ACTIVE, ENDING, ENDED, FAILED
    uint64_t startedAt;
    uint64_t endedAt;
    uint64_t lastHeartbeatAt;
    std::string sessionSecret;
};

} // namespace teleopdb

/**
 * VIN 仓库接口
 */
class VinRepository {
public:
    /**
     * 绑定 VIN 到账户
     * @param accountId 账户 ID
     * @param bindCode 绑定码
     * @return 是否成功
     */
    virtual bool bindVin(const std::string& accountId, const std::string& vin, const std::string& bindCode) = 0;

    /**
     * 解绑 VIN
     * @param accountId 账户 ID
     * @param vin VIN 码
     * @return 是否成功
     */
    virtual bool unbindVin(const std::string& accountId, const std::string& vin) = 0;

    /**
     * 获取绑定的 VIN 列表
     * @param accountId 账户 ID
     * @return VIN 列表
     */
    virtual std::vector<VinInfo> getBoundVins(const std::string& accountId) = 0;

    /**
     * 授权 VIN 给用户
     * @param vin VIN 码
     * @param granteeUserIds 授权用户ID列表
     * @param permissions 权限列表
     * @param createdBy 创建者ID
     * @return 是否成功
     */
    virtual bool grantVinPermission(const std::string& vin, 
                                           const std::vector<std::string>& granteeUserIds,
                                           const std::vector<std::string>& permissions,
                                           const std::string& createdBy) = 0;

    /**
     * 撤销 VIN 权限
     * @param vin VIN 码
     * @param granteeUserIds 要撤权用户ID列表
     * @return 是否成功
     */
    virtual bool revokeVinPermission(const std::string& vin,
                                          const std::vector<std::string>& granteeUserIds) = 0;

    /**
     * 检查用户对 VIN 是否有指定权限
     * @param userId 用户ID
     * @param vin VIN 码
     @param permission 权限字符串 ("vin.view", "vin.control", "vin.maintain")
     * @return 是否有权限
     */
    virtual bool hasPermission(const std::string& userId,
                                  const std::string& vin,
                                  const std::string& permission) const = 0;

    /**
     * 获取 VIN 信息
     * @param vin VIN 码
     * @return VIN 信息
     */
    virtual std::optional<VinInfo> getVinInfo(const std::string& vin) const = 0;

    /**
     * 获取 VIN 的所有授权
     * @param vin VIN 码
     * @return 授权列表
     */
    virtual std::vector<VinGrant> getVinGrants(const std::string& vin) const = 0;

    /**
     * 获取激活的会话
     * @param vin VIN 码
     * @return 会话信息
     */
    virtual std::optional<SessionInfo> getActiveSession(const std::string& vin) const = 0;

    /**
     * 设置激活会话
     * @param sessionInfo 会话信息
     */
    virtual void setSession(const SessionInfo& sessionInfo) = 0;

    /**
     * 清除激活会话
     * @param vin VIN 码
     * @param endedAt 时间戳
     */
    virtual void clearActiveSession(const std::string& vin, uint64_t endedAt) = 0;
};

// 内存实现示例
class InMemoryVinRepository : public VinRepository {
    std::map<std::string, VinInfo> vinInfoMap_;
    std::map<std::string, std::vector<std::string>>> vinBindings_;  // accountId -> [vin1, vin2, ...]
    std::map<std::string, std::vector<VinGrant>> vinGrants_;  // vin -> [grant1, grant2, ...]
    std::map<std::string, SessionInfo> activeSessions_;
};

} // namespace teleopdb

#endif // TELEOP_VINI_REPOSITORY_H
