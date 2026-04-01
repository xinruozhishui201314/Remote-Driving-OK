/**
 * VIN Authorization Handler
 * 
 * 功能：
 * - VIN 授权管理（grant/revoke）
 * - 查询 VIN 权限
 * - 权限检查（vin.view, vin.control, vin.maintain）
 */

#ifndef TELEOP_API_VIN_HANDLER_H
#define TELEOP_API_VIN_HANDLER_H

#include <string>
#include <vector>
#include <memory>
#include <httplib.h>

namespace teleop::api {

/**
 * VIN 授权处理器
 */
class VinHandler {
public:
    VinHandler();
    ~VinHandler() = default;
    
    /**
     * 注册所有 VIN 相关的路由
     * @param server HTTP 服务器
     */
    void register_routes(httplib::Server& server);

private:
    /**
     * POST /api/v1/vins/{vin}/grant
     * 授权 VIN 给指定用户
     * 
     * 请求体：
     * {
     *   "grantee_username": "user1",
     *   "permissions": ["vin.view", "vin.control"],
     *   "expires_at": "2026-03-31T23:59:59Z" // 可选
     * }
     * 
     * 响应：
     * 201 Created
     * {
     *   "vin": "LSGBF53M8DS123456",
     *   "grantee_user_id": "uuid",
     *   "permissions": ["vin.view", "vin.control"]
     * }
     */
    void handle_grant(const httplib::Request& req, httplib::Response& res);
    
    /**
     * POST /api/v1/vins/{vin}/revoke
     * 撤销用户的 VIN 权限
     * 
     * 请求体：
     * {
     *   "grantee_username": "user1",
     *   "permissions": ["vin.view", "vin.control"] // 可选，默认撤销所有权限
     * }
     * 
     * 响应：
     * 204 No Content
     */
    void handle_revoke(const httplib::Request& req, httplib::Response& res);
    
    /**
     * GET /api/v1/vins/{vin}/permissions
     * 查询 VIN 的所有权限记录
     * 
     * 响应：
     * 200 OK
     * {
     *   "vin": "LSGBF53M8DS123456",
     *   "permissions": [
     *     {
     *       "grantee_user_id": "uuid",
     *       "grantee_username": "user1",
     *       "permissions": ["vin.view", "vin.control"],
     *       "expires_at": "2026-03-31T23:59:59Z",
     *       "created_by": "uuid",
     *       "created_at": "2026-02-27T12:00:00Z"
     *     }
     *   ]
     * }
     */
    void handle_get_permissions(const httplib::Request& req, httplib::Response& res);
    
    /**
     * POST /api/v1/vins/{vin}/check-permission
     * 检查当前用户是否对 VIN 有指定权限
     * 
     * 请求体：
     * {
     *   "permission": "vin.control"
     * }
     * 
     * 响应：
     * 200 OK
     * {
     *   "has_permission": true
     * }
     */
    void handle_check_permission(const httplib::Request& req, httplib::Response& res);
};

} // namespace teleop::api

#endif // TELEOP_API_VIN_HANDLER_H
