/**
 * VIN Authorization Handler 实现
 */

#include "api/vin_handler.h"
#include "db/repository.h"
#include "auth/jwt_validator.h"
#include "common/utils.h"
#include <nlohmann/json.hpp>
#include <iostream>

namespace teleop::api {

namespace {
    // 辅助函数：从 JWT sub 获取 user_id
    std::optional<std::string> get_user_id_from_sub(
        db::Repository& repo,
        const std::string& sub
    ) {
        std::string query = "SELECT id::text FROM users WHERE keycloak_sub = $1";
        const char* param = sub.c_str();
        int len = static_cast<int>(sub.size());
        
        auto result = repo.execute_query(query, {&param}, {&len});
        if (!result.is_valid() || !result.has_rows()) {
            return std::nullopt;
        }
        
        return result.get_value(0, 0);
    }
    
    // 辅助函数：从 username 获取 user_id
    std::optional<std::string> get_user_id_from_username(
        db::Repository& repo,
        const std::string& username
    ) {
        std::string query = "SELECT id::text FROM users WHERE username = $1";
        const char* param = username.c_str();
        int len = static_cast<int>(username.size());
        
        auto result = repo.execute_query(query, {&param}, {&len});
        if (!result.is_valid() || !result.has_rows()) {
            return std::nullopt;
        }
        
        return result.get_value(0, 0);
    }
}

VinHandler::VinHandler() {}

void VinHandler::register_routes(httplib::Server& server) {
    // 从环境变量获取 expected issuers 和 audiences
    std::string keycloak_url = teleop::common::EnvUtils::get("KEYCLOAK_URL", "http://keycloak:8080");
    std::string keycloak_realm = teleop::common::EnvUtils::get("KEYCLOAK_REALM", "teleop");
    std::string default_iss = keycloak_url + "/realms/" + keycloak_realm;
    
    std::vector<std::string> expected_issuers = {default_iss};
    std::string issuer_extra = teleop::common::EnvUtils::get("KEYCLOAK_ISSUER_EXTRA", "");
    if (!issuer_extra.empty()) {
        for (size_t pos = 0; pos < issuer_extra.size(); ) {
            size_t next = issuer_extra.find(',', pos);
            std::string s = (next == std::string::npos) 
                ? issuer_extra.substr(pos) 
                : issuer_extra.substr(pos, next - pos);
            pos = (next == std::string::npos) ? issuer_extra.size() : next + 1;
            s = teleop::common::StringUtils::trim(s);
            if (!s.empty()) expected_issuers.push_back(s);
        }
    }
    
    std::string keycloak_client_id = teleop::common::EnvUtils::get("KEYCLOAK_CLIENT_ID", "teleop-backend");
    std::vector<std::string> expected_aud = {keycloak_client_id, "account", "teleop-client"};
    
    // 获取 database_url
    std::string database_url = teleop::common::EnvUtils::get("DATABASE_URL", "");
    
    server.Post("/api/v1/vins/([^/]+)/grant", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            handle_grant(req, res, expected_issuers, expected_aud, database_url);
        } catch (const std::exception& e) {
            std::cerr << "[Backend][VinHandler] Exception in grant: " << e.what() << std::endl;
            res.status = 500;
            res.set_content("{\"error\":\"internal\",\"details\":\"" + std::string(e.what()) + "\"}", "application/json");
        }
    });
    
    server.Post("/api/v1/vins/([^/]+)/revoke", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            handle_revoke(req, res, expected_issuers, expected_aud, database_url);
        } catch (const std::exception& e) {
            std::cerr << "[Backend][VinHandler] Exception in revoke: " << e.what() << std::endl;
            res.status = 500;
            res.set_content("{\"error\":\"internal\",\"details\":\"" + std::string(e.what()) + "\"}", "application/json");
        }
    });
    
    server.Get("/api/v1/vins/([^/]+)/permissions", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            handle_get_permissions(req, res, expected_issuers, expected_aud, database_url);
        } catch (const std::exception& e) {
            std::cerr << "[Backend][VinHandler] Exception in get_permissions: " << e.what() << std::endl;
            res.status = 500;
            res.set_content("{\"error\":\"internal\",\"details\":\"" + std::string(e.what()) + "\"}", "application/json");
        }
    });
    
    server.Post("/api/v1/vins/([^/]+)/check-permission", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            handle_check_permission(req, res, expected_issuers, expected_aud, database_url);
        } catch (const std::exception& e) {
            std::cerr << "[Backend][VinHandler] Exception in check_permission: " << e.what() << std::endl;
            res.status = 500;
            res.set_content("{\"error\":\"internal\",\"details\":\"" + std::string(e.what()) + "\"}", "application/json");
        }
    });
    
    std::cout << "[Backend][VinHandler] Registered routes:" << std::endl;
    std::cout << "  POST /api/v1/vins/{vin}/grant" << std::endl;
    std::cout << "  POST /api/v1/vins/{vin}/revoke" << std::endl;
    std::cout << "  GET  /api/v1/vins/{vin}/permissions" << std::endl;
    std::cout << "  POST /api/v1/vins/{vin}/check-permission" << std::endl;
}

void VinHandler::handle_grant(
    const httplib::Request& req,
    httplib::Response& res,
    const std::vector<std::string>& expected_issuers,
    const std::vector<std::string>& expected_aud,
    const std::string& database_url
) {
    std::cout << "[Backend][POST /api/v1/vins/*/grant] " << req.path << std::endl;
    
    // 1. 验证 JWT
    std::string auth = req.get_header_value("Authorization");
    if (auth.empty() || auth.find("Bearer ") != 0) {
        res.status = 401;
        res.set_content("{\"error\":\"unauthorized\",\"details\":\"Missing Bearer token\"}", "application/json");
        return;
    }
    
    std::string token = auth.substr(7);
    auto payload_opt = teleop::auth::validate_jwt_claims(token, expected_issuers, expected_aud);
    if (!payload_opt) {
        res.status = 401;
        res.set_content("{\"error\":\"unauthorized\",\"details\":\"Invalid JWT\"}", "application/json");
        return;
    }
    
    nlohmann::json payload;
    try {
        payload = nlohmann::json::parse(*payload_opt);
    } catch (...) {
        res.status = 401;
        res.set_content("{\"error\":\"unauthorized\",\"details\":\"Invalid JWT payload\"}", "application/json");
        return;
    }
    
    std::string actor_sub = teleop::common::JsonHelper::get_string(payload, "sub");
    std::string actor_username = teleop::common::JsonHelper::get_string(payload, "preferred_username");
    
    // 2. 提取路径中的 VIN
    std::string vin;
    {
        std::string path = req.path;
        size_t pos = path.find("/vins/");
        if (pos != std::string::npos) {
            pos += 6; // "/vins/" 的长度
            size_t end = path.find("/grant", pos);
            if (end != std::string::npos) {
                vin = path.substr(pos, end - pos);
            }
        }
    }
    
    if (vin.empty() || vin.size() > 17) {
        res.status = 400;
        res.set_content("{\"error\":\"bad_request\",\"details\":\"Invalid or missing VIN\"}", "application/json");
        return;
    }
    
    // 3. 解析请求体
    nlohmann::json body;
    try {
        body = nlohmann::json::parse(req.body);
    } catch (...) {
        res.status = 400;
        res.set_content("{\"error\":\"bad_request\",\"details\":\"Invalid JSON body\"}", "application/json");
        return;
    }
    
    std::string grantee_username = teleop::common::JsonHelper::get_string(body, "grantee_username");
    std::vector<std::string> permissions = body.value("permissions", std::vector<std::string>{"vin.view", "vin.control", "vin.maintain"});
    std::string expires_at_str = teleop::common::JsonHelper::get_string(body, "expires_at");
    
    if (grantee_username.empty()) {
        res.status = 400;
        res.set_content("{\"error\":\"bad_request\",\"details\":\"grantee_username is required\"}", "application/json");
        return;
    }
    
    if (!teleop::common::ValidationUtils::is_valid_username(grantee_username)) {
        res.status = 400;
        res.set_content("{\"error\":\"bad_request\",\"details\":\"Invalid username format\"}", "application/json");
        return;
    }
    
    // 4. 验证 VIN 存在
    if (!teleop::common::ValidationUtils::is_valid_vin(vin)) {
        res.status = 400;
        res.set_content("{\"error\":\"bad_request\",\"details\":\"Invalid VIN format\"}", "application/json");
        return;
    }
    
    // 5. 获取 actor 和 grantee 的 user_id
    db::Repository::Config config{database_url, true, 10, 10};
    db::Repository repo(config);
    
    auto actor_user_id_opt = get_user_id_from_sub(repo, actor_sub);
    if (!actor_user_id_opt) {
        res.status = 403;
        res.set_content("{\"error\":\"forbidden\",\"details\":\"Actor user not found\"}", "application/json");
        return;
    }
    
    auto grantee_user_id_opt = get_user_id_from_username(repo, grantee_username);
    if (!grantee_user_id_opt) {
        res.status = 404;
        res.set_content("{\"error\":\"not_found\",\"details\":\"Grantee user not found\"}", "application/json");
        return;
    }
    
    // 6. 在事务中插入 vin_grants 记录
    db::Connection conn = repo.get_connection();
    db::Transaction tx(conn, db::Transaction::RollbackPolicy::ON_EXCEPTION);
    
    std::string query = R"(
        INSERT INTO vin_grants (vin, grantee_user_id, permissions, created_by, expires_at)
        VALUES ($1, $2::uuid, $3::jsonb, $4::uuid, $5::timestamp)
        ON CONFLICT (vin, grantee_user_id) 
        DO UPDATE SET permissions = $3::jsonb, updated_at = now()
    )";
    
    std::string expires_at;
    if (expires_at_str.empty()) {
        expires_at = "NULL";
    } else {
        expires_at = "$5::timestamp";
    }
    
    const char* params[5] = {
        vin.c_str(),
        grantee_user_id_opt->c_str(),
        "[\"vin.view\",\"vin.control\",\"vin.maintain\"]",
        actor_user_id_opt->c_str(),
        expires_at_str.c_str()
    };
    
    int lens[5] = {
        static_cast<int>(vin.size()),
        static_cast<int>(grantee_user_id_opt->size()),
        static_cast<int>(strlen("[\"vin.view\",\"vin.control\",\"vin.maintain\"]")),
        static_cast<int>(actor_user_id_opt->size()),
        static_cast<int>(expires_at_str.size())
    };
    
    std::string error_msg;
    bool success = repo.execute_command(query, {params, 5}, {lens, 5}, &error_msg);
    if (!success) {
        res.status = 500;
        res.set_content("{\"error\":\"internal\",\"details\":\"Failed to insert vin_grant: " + error_msg + "\"}", "application/json");
        return;
    }
    
    tx.commit();
    
    // 7. 记录审计日志
    nlohmann::json detail;
    detail["permissions"] = permissions;
    if (!expires_at_str.empty()) {
        detail["expires_at"] = expires_at_str;
    }
    
    repo.audit_log(
        *actor_user_id_opt,
        "vin_grant",
        vin,
        std::nullopt,
        req.get_header_value("X-Forwarded-For"),
        req.get_header_value("User-Agent"),
        detail.dump()
    );
    
    // 8. 返回响应
    nlohmann::json response;
    response["vin"] = vin;
    response["grantee_user_id"] = *grantee_user_id_opt;
    response["grantee_username"] = grantee_username;
    response["permissions"] = permissions;
    
    res.status = 201;
    res.set_content(response.dump(), "application/json");
    
    std::cout << "[Backend][Grant] Success vin=" << vin << " grantee=" << grantee_username << std::endl;
}

void VinHandler::handle_revoke(
    const httplib::Request& req,
    httplib::Response& res,
    const std::vector<std::string>& expected_issuers,
    const std::vector<std::string>& expected_aud,
    const std::string& database_url
) {
    std::cout << "[Backend][POST /api/v1/vins/*/revoke] " << req.path << std::endl;
    
    // 1. 验证 JWT
    std::string auth = req.get_header_value("Authorization");
    if (auth.empty() || auth.find("Bearer ") != 0) {
        res.status = 401;
        res.set_content("{\"error\":\"unauthorized\",\"details\":\"Missing Bearer token\"}", "application/json");
        return;
    }
    
    std::string token = auth.substr(7);
    auto payload_opt = teleop::auth::validate_jwt_claims(token, expected_issuers, expected_aud);
    if (!payload_opt) {
        res.status = 401;
        res.set_content("{\"error\":\"unauthorized\",\"details\":\"Invalid JWT\"}", "application/json");
        return;
    }
    
    nlohmann::json payload;
    try {
        payload = nlohmann::json::parse(*payload_opt);
    } catch (...) {
        res.status = 401;
        res.set_content("{\"error\":\"unauthorized\",\"details\":\"Invalid JWT payload\"}", "application/json");
        return;
    }
    
    std::string actor_sub = teleop::common::JsonHelper::get_string(payload, "sub");
    
    // 2. 提取路径中的 VIN
    std::string vin;
    {
        std::string path = req.path;
        size_t pos = path.find("/vins/");
        if (pos != std::string::npos) {
            pos += 6;
            size_t end = path.find("/revoke", pos);
            if (end != std::string::npos) {
                vin = path.substr(pos, end - pos);
            }
        }
    }
    
    if (vin.empty() || vin.size() > 17) {
        res.status = 400;
        res.set_content("{\"error\":\"bad_request\",\"details\":\"Invalid or missing VIN\"}", "application/json");
        return;
    }
    
    // 3. 解析请求体
    nlohmann::json body;
    try {
        body = nlohmann::json::parse(req.body);
    } catch (...) {
        res.status = 400;
        res.set_content("{\"error\":\"bad_request\",\"details\":\"Invalid JSON body\"}", "application/json");
        return;
    }
    
    std::string grantee_username = teleop::common::JsonHelper::get_string(body, "grantee_username");
    std::vector<std::string> permissions = body.value("permissions", std::vector<std::string>{});
    
    if (grantee_username.empty()) {
        res.status = 400;
        res.set_content("{\"error\":\"bad_request\",\"details\":\"grantee_username is required\"}", "application/json");
        return;
    }
    
    // 4. 获取 actor 和 grantee 的 user_id
    db::Repository::Config config{database_url, true, 10, 10};
    db::Repository repo(config);
    
    auto actor_user_id_opt = get_user_id_from_sub(repo, actor_sub);
    if (!actor_user_id_opt) {
        res.status = 403;
        res.set_content("{\"error\":\"forbidden\",\"details\":\"Actor user not found\"}", "application/json");
        return;
    }
    
    auto grantee_user_id_opt = get_user_id_from_username(repo, grantee_username);
    if (!grantee_user_id_opt) {
        res.status = 404;
        res.set_content("{\"error\":\"not_found\",\"details\":\"Grantee user not found\"}", "application/json");
        return;
    }
    
    // 5. 在事务中删除 vin_grants 记录
    db::Connection conn = repo.get_connection();
    db::Transaction tx(conn, db::Transaction::RollbackPolicy::ON_EXCEPTION);
    
    std::string query = R"(
        DELETE FROM vin_grants 
        WHERE vin = $1 
          AND grantee_user_id = $2::uuid
    )";
    
    const char* params[2] = {
        vin.c_str(),
        grantee_user_id_opt->c_str()
    };
    
    int lens[2] = {
        static_cast<int>(vin.size()),
        static_cast<int>(grantee_user_id_opt->size())
    };
    
    std::string error_msg;
    bool success = repo.execute_command(query, {params, 2}, {lens, 2}, &error_msg);
    if (!success) {
        res.status = 500;
        res.set_content("{\"error\":\"internal\",\"details\":\"Failed to delete vin_grant: " + error_msg + "\"}", "application/json");
        return;
    }
    
    tx.commit();
    
    // 6. 记录审计日志
    nlohmann::json detail;
    if (!permissions.empty()) {
        detail["permissions"] = permissions;
    }
    detail["revoked_count"] = 1;
    
    repo.audit_log(
        *actor_user_id_opt,
        "vin_revoke",
        vin,
        std::nullopt,
        req.get_header_value("X-Forwarded-For"),
        req.get_header_value("User-Agent"),
        detail.dump()
    );
    
    // 7. 返回响应
    res.status = 204;
    res.set_content("", "text/plain");
    
    std::cout << "[Backend][Revoke] Success vin=" << vin << " grantee=" << grantee_username << std::endl;
}

void VinHandler::handle_get_permissions(
    const httplib::Request& req,
    httplib::Response& res,
    const std::vector<std::string>& expected_issuers,
    const std::vector<std::string>& expected_aud,
    const std::string& database_url
) {
    std::cout << "[Backend][GET /api/v1/vins/*/permissions] " << req.path << std::endl;
    
    // 1. 验证 JWT
    std::string auth = req.get_header_value("Authorization");
    if (auth.empty() || auth.find("Bearer ") != 0) {
        res.status = 401;
        res.set_content("{\"error\":\"unauthorized\",\"details\":\"Missing Bearer token\"}", "application/json");
        return;
    }
    
    std::string token = auth.substr(7);
    auto payload_opt = teleop::auth::validate_jwt_claims(token, expected_issuers, expected_aud);
    if (!payload_opt) {
        res.status = 401;
        res.set_content("{\"error\":\"unauthorized\",\"details\":\"Invalid JWT\"}", "application/json");
        return;
    }
    
    // 2. 提取路径中的 VIN
    std::string vin;
    {
        std::string path = req.path;
        size_t pos = path.find("/vins/");
        if (pos != std::string::npos) {
            pos += 6;
            size_t end = path.find("/permissions", pos);
            if (end != std::string::npos) {
                vin = path.substr(pos, end - pos);
            }
        }
    }
    
    if (vin.empty() || vin.size() > 17) {
        res.status = 400;
        res.set_content("{\"error\":\"bad_request\",\"details\":\"Invalid or missing VIN\"}", "application/json");
        return;
    }
    
    // 3. 查询数据库
    db::Repository::Config config{database_url, true, 10, 10};
    db::Repository repo(config);
    
    std::string query = R"(
        SELECT 
            grantee_user_id::text,
            u.username as grantee_username,
            permissions::text,
            expires_at,
            created_by::text,
            u2.username as created_by_username,
            created_at
        FROM vin_grants vg
        JOIN users u ON vg.grantee_user_id = u.id
        JOIN users u2 ON vg.created_by = u2.id
        WHERE vg.vin = $1
        ORDER BY vg.created_at DESC
    )";
    
    const char* param = vin.c_str();
    int len = static_cast<int>(vin.size());
    
    auto result = repo.execute_query(query, {&param}, {&len});
    if (!result.is_valid()) {
        res.status = 500;
        res.set_content("{\"error\":\"internal\",\"details\":\"Database query failed\"}", "application/json");
        return;
    }
    
    // 4. 构建响应
    nlohmann::json permissions_array = nlohmann::json::array();
    
    int num_rows = result.num_rows();
    for (int i = 0; i < num_rows; ++i) {
        auto grantee_user_id = result.get_value(i, 0);
        auto grantee_username = result.get_value(i, 1);
        auto permissions_json = result.get_value(i, 2);
        auto expires_at = result.get_value(i, 3);
        auto created_by = result.get_value(i, 4);
        auto created_by_username = result.get_value(i, 5);
        auto created_at = result.get_value(i, 6);
        
        nlohmann::json perm;
        perm["grantee_user_id"] = grantee_user_id ? *grantee_user_id : "";
        perm["grantee_username"] = grantee_username ? *grantee_username : "";
        
        // 解析 permissions JSON
        try {
            perm["permissions"] = permissions_json ? nlohmann::json::parse(*permissions_json) : nlohmann::json::array();
        } catch (...) {
            perm["permissions"] = nlohmann::json::array();
        }
        
        if (expires_at && !expires_at->empty()) {
            perm["expires_at"] = *expires_at;
        }
        
        perm["created_by"] = created_by ? *created_by : "";
        perm["created_by_username"] = created_by_username ? *created_by_username : "";
        
        if (created_at && !created_at->empty()) {
            perm["created_at"] = *created_at;
        }
        
        permissions_array.push_back(perm);
    }
    
    nlohmann::json response;
    response["vin"] = vin;
    response["permissions"] = permissions_array;
    response["count"] = num_rows;
    
    res.status = 200;
    res.set_content(response.dump(), "application/json");
    
    std::cout << "[Backend][GetPermissions] Success vin=" << vin << " count=" << num_rows << std::endl;
}

void VinHandler::handle_check_permission(
    const httplib::Request& req,
    httplib::Response& res,
    const std::vector<std::string>& expected_issuers,
    const std::vector<std::string>& expected_aud,
    const std::string& database_url
) {
    std::cout << "[Backend][POST /api/v1/vins/*/check-permission] " << req.path << std::endl;
    
    // 1. 验证 JWT
    std::string auth = req.get_header_value("Authorization");
    if (auth.empty() || auth.find("Bearer ") != 0) {
        res.status = 401;
        res.set_content("{\"error\":\"unauthorized\",\"details\":\"Missing Bearer token\"}", "application/json");
        return;
    }
    
    std::string token = auth.substr(7);
    auto payload_opt = teleop::auth::validate_jwt_claims(token, expected_issuers, expected_aud);
    if (!payload_opt) {
        res.status = 401;
        res.set_content("{\"error\":\"unauthorized\",\"details\":\"Invalid JWT\"}", "application/json");
        return;
    }
    
    nlohmann::json payload;
    try {
        payload = nlohmann::json::parse(*payload_opt);
    } catch (...) {
        res.status = 401;
        res.set_content("{\"error\":\"unauthorized\",\"details\":\"Invalid JWT payload\"}", "application/json");
        return;
    }
    
    std::string sub = teleop::common::JsonHelper::get_string(payload, "sub");
    
    // 2. 提取路径中的 VIN
    std::string vin;
    {
        std::string path = req.path;
        size_t pos = path.find("/vins/");
        if (pos != std::string::npos) {
            pos += 6;
            size_t end = path.find("/check-permission", pos);
            if (end != std::string::npos) {
                vin = path.substr(pos, end - pos);
            }
        }
    }
    
    if (vin.empty() || vin.size() > 17) {
        res.status = 400;
        res.set_content("{\"error\":\"bad_request\",\"details\":\"Invalid or missing VIN\"}", "application/json");
        return;
    }
    
    // 3. 解析请求体
    nlohmann::json body;
    try {
        body = nlohmann::json::parse(req.body);
    } catch (...) {
        res.status = 400;
        res.set_content("{\"error\":\"bad_request\",\"details\":\"Invalid JSON body\"}", "application/json");
        return;
    }
    
    std::string permission = teleop::common::JsonHelper::get_string(body, "permission");
    if (permission.empty()) {
        res.status = 400;
        res.set_content("{\"error\":\"bad_request\",\"details\":\"permission is required\"}", "application/json");
        return;
    }
    
    // 4. 查询数据库（检查当前用户是否有权限）
    db::Repository::Config config{database_url, true, 10, 10};
    db::Repository repo(config);
    
    auto user_id_opt = get_user_id_from_sub(repo, sub);
    if (!user_id_opt) {
        res.status = 403;
        res.set_content("{\"error\":\"forbidden\",\"details\":\"User not found\"}", "application/json");
        return;
    }
    
    std::string query = R"(
        SELECT EXISTS (
            SELECT 1 FROM vin_grants 
            WHERE vin = $1 
              AND grantee_user_id = $2::uuid
              AND permissions @> ARRAY[$3::text]
              AND (expires_at IS NULL OR expires_at > now())
        ) as has_permission
    )";
    
    const char* params[3] = {
        vin.c_str(),
        user_id_opt->c_str(),
        ("\"" + permission + "\"").c_str()
    };
    
    int lens[3] = {
        static_cast<int>(vin.size()),
        static_cast<int>(user_id_opt->size()),
        static_cast<int>(permission.size() + 2)  // 包括引号
    };
    
    auto result = repo.execute_query(query, {params, 3}, {lens, 3});
    if (!result.is_valid()) {
        res.status = 500;
        res.set_content("{\"error\":\"internal\",\"details\":\"Database query failed\"}", "application/json");
        return;
    }
    
    auto has_permission = result.get_bool(0, 0);
    
    // 5. 返回响应
    nlohmann::json response;
    response["vin"] = vin;
    response["permission"] = permission;
    response["has_permission"] = has_permission ? true : false;
    
    res.status = 200;
    res.set_content(response.dump(), "application/json");
    
    std::cout << "[Backend][CheckPermission] vin=" << vin << " permission=" << permission << " result=" << (has_permission ? "true" : "false") << std::endl;
}

} // namespace teleop::api
