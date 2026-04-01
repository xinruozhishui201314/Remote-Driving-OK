/**
 * Session Handler 实现
 */

#include "api/session_handler.h"
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
    
    // 辅助函数：检查用户是否是 session 的 controller
    bool is_session_controller(
        db::Repository& repo,
        const std::string& session_id,
        const std::string& user_id
    ) {
        std::string query = "SELECT controller_user_id::text FROM sessions WHERE session_id = $1::uuid";
        const char* param = session_id.c_str();
        int len = static_cast<int>(session_id.size());
        
        auto result = repo.execute_query(query, {&param}, {&len});
        if (!result.is_valid() || !result.has_rows()) {
            return false;
        }
        
        auto controller_user_id = result.get_value(0, 0);
        return controller_user_id && *controller_user_id == user_id;
    }
    
    // 辅助函数：检查会话状态
    bool is_session_active(db::Repository& repo, const std::string& session_id) {
        std::string query = "SELECT state FROM sessions WHERE session_id = $1::uuid";
        const char* param = session_id.c_str();
        int len = static_cast<int>(session_id.size());
        
        auto result = repo.execute_query(query, {&param}, {&len});
        if (!result.is_valid() || !result.has_rows()) {
            return false;
        }
        
        auto state = result.get_value(0, 0);
        return state && (*state == "REQUESTED" || *state == "ACTIVE");
    }
}

SessionHandler::SessionHandler() {}

void SessionHandler::register_routes(httplib::Server& server) {
    std::cout << "[Backend][SessionHandler] Registering routes:" << std::endl;
    
    std::string database_url = teleop::common::EnvUtils::get("DATABASE_URL", "");
    std::string keycloak_url = teleop::common::EnvUtils::get("KEYCLOAK_URL", "http://keycloak:8080");
    std::string keycloak_realm = teleop::common::EnvUtils::get("KEYCLOAK_REALM", "teleop");
    std::string default_iss = keycloak_url + "/realms/" + keycloak_realm;
    
    std::vector<std::string> expected_issuers = {default_iss};
    std::string issuer_extra = teleop::common::EnvUtils::get("KEYCLOAK_ISSUER_EXTRA", "");
    if (!issuer_extra.empty()) {
        auto parts = teleop::common::StringUtils::split(issuer_extra, ',');
        for (auto& s : parts) {
            s = teleop::common::StringUtils::trim(s);
            if (!s.empty()) expected_issuers.push_back(s);
        }
    }
    
    std::string keycloak_client_id = teleop::common::EnvUtils::get("KEYCLOAK_CLIENT_ID", "teleop-backend");
    std::vector<std::string> expected_aud = {keycloak_client_id, "account", "teleop-client"};
    
    server.Post("/api/v1/sessions/([^/]+)/end", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            handle_end_session(req, res, expected_issuers, expected_aud, database_url);
        } catch (const std::exception& e) {
            std::cerr << "[Backend][SessionHandler] Exception in end_session: " << e.what() << std::endl;
            res.status = 500;
            res.set_content("{\"error\":\"internal\",\"details\":\"" + std::string(e.what()) + "\"}", "application/json");
        }
    });
    
    server.Post("/api/v1/sessions/([^/]+)/unlock", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            handle_unlock_session(req, res, expected_issuers, expected_aud, database_url);
        } catch (const std::exception& e) {
            std::cerr << "[Backend][SessionHandler] Exception in unlock_session: " << e.what() << std::endl;
            res.status = 500;
            res.set_content("{\"error\":\"internal\",\"details\":\"" + std::string(e.what()) + "\"}", "application/json");
        }
    });
    
    std::cout << "  POST /api/v1/sessions/{sessionId}/end" << std::endl;
    std::cout << "  POST /api/v1/sessions/{sessionId}/unlock" << std::endl;
}

void SessionHandler::handle_end_session(
    const httplib::Request& req,
    httplib::Response& res,
    const std::vector<std::string>& expected_issuers,
    const std::vector<std::string>& expected_aud,
    const std::string& database_url
) {
    std::cout << "[Backend][POST /api/v1/sessions/*/end] " << req.path << std::endl;
    
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
    
    // 2. 提取路径中的 sessionId
    std::string session_id;
    {
        std::string path = req.path;
        size_t pos = path.find("/sessions/");
        if (pos != std::string::npos) {
            pos += 10; // "/sessions/" 的长度
            size_t end = path.find("/end", pos);
            if (end != std::string::npos) {
                session_id = path.substr(pos, end - pos);
            }
        }
    }
    
    if (session_id.empty()) {
        res.status = 400;
        res.set_content("{\"error\":\"bad_request\",\"details\":\"Missing sessionId\"}", "application/json");
        return;
    }
    
    // 3. 验证用户是该会话的 controller
    db::Repository::Config config{database_url, true, 10, 10};
    db::Repository repo(config);
    
    auto user_id_opt = get_user_id_from_sub(repo, sub);
    if (!user_id_opt) {
        res.status = 403;
        res.set_content("{\"error\":\"forbidden\",\"details\":\"User not found\"}", "application/json");
        return;
    }
    
    if (!is_session_controller(repo, session_id, *user_id_opt)) {
        res.status = 403;
        res.set_content("{\"error\":\"forbidden\",\"details\":\"Only controller can end session\"}", "application/json");
        return;
    }
    
    // 4. 检查会话状态（必须为 REQUESTED 或 ACTIVE）
    if (!is_session_active(repo, session_id)) {
        res.status = 409;
        res.set_content("{\"error\":\"session_invalid_state\",\"details\":\"Session not active\"}", "application/json");
        return;
    }
    
    // 5. 解析请求体（可选 reason 字段）
    std::string reason = "";
    try {
        auto body = nlohmann::json::parse(req.body);
        reason = teleop::common::JsonHelper::get_string(body, "reason");
    } catch (...) {
        // 空请求体或无效 JSON，使用默认 reason
        reason = "User requested";
    }
    
    // 6. 在事务中更新 session
    db::Connection conn = repo.get_connection();
    db::Transaction tx(conn, db::Transaction::RollbackPolicy::ON_EXCEPTION);
    
    std::string query = R"(
        UPDATE sessions
        SET state = 'ENDED',
            ended_at = now(),
            last_heartbeat_at = now()
        WHERE session_id = $1::uuid
          AND (state = 'REQUESTED' OR state = 'ACTIVE')
        RETURNING vin::text
    )";
    
    const char* param = session_id.c_str();
    int len = static_cast<int>(session_id.size());
    
    auto result = repo.execute_query(query, {&param}, {&len});
    if (!result.is_valid() || !result.has_rows()) {
        res.status = 500;
        res.set_content("{\"error\":\"internal\",\"details\":\"Failed to end session\"}", "application/json");
        return;
    }
    
    auto vin = result.get_value(0, 0);
    
    tx.commit();
    
    // 7. 记录审计日志
    nlohmann::json detail;
    if (!reason.empty()) {
        detail["reason"] = reason;
    }
    
    repo.audit_log(
        *user_id_opt,
        "session_end",
        vin,
        session_id,
        req.get_header_value("X-Forwarded-For"),
        req.get_header_value("User-Agent"),
        detail.dump()
    );
    
    // 8. 返回响应
    nlohmann::json response;
    response["sessionId"] = session_id;
    response["vin"] = vin ? *vin : "";
    response["state"] = "ENDED";
    response["endedAt"] = teleop::common::Timestamp::to_iso8601(teleop::common::Timestamp::now_ms());
    
    res.status = 200;
    res.set_content(response.dump(), "application/json");
    
    std::cout << "[Backend][EndSession] Success sessionId=" << session_id << " vin=" << (vin ? *vin : "") << " reason=" << reason << std::endl;
}

void SessionHandler::handle_unlock_session(
    const httplib::Request& req,
    httplib::Response& res,
    const std::vector<std::string>& expected_issuers,
    const std::vector<std::string>& expected_aud,
    const std::string& database_url
) {
    std::cout << "[Backend][POST /api/v1/sessions/*/unlock] " << req.path << std::endl;
    
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
    
    // 2. 提取路径中的 sessionId
    std::string session_id;
    {
        std::string path = req.path;
        size_t pos = path.find("/sessions/");
        if (pos != std::string::npos) {
            pos += 10;
            size_t end = path.find("/unlock", pos);
            if (end != std::string::npos) {
                session_id = path.substr(pos, end - pos);
            }
        }
    }
    
    if (session_id.empty()) {
        res.status = 400;
        res.set_content("{\"error\":\"bad_request\",\"details\":\"Missing sessionId\"}", "application/json");
        return;
    }
    
    // 3. 验证用户是该会话的 controller
    db::Repository::Config config{database_url, true, 10, 10};
    db::Repository repo(config);
    
    auto user_id_opt = get_user_id_from_sub(repo, sub);
    if (!user_id_opt) {
        res.status = 403;
        res.set_content("{\"error\":\"forbidden\",\"details\":\"User not found\"}", "application/json");
        return;
    }
    
    if (!is_session_controller(repo, session_id, *user_id_opt)) {
        res.status = 403;
        res.set_content("{\"error\":\"forbidden\",\"details\":\"Only controller can unlock session\"}", "application/json");
        return;
    }
    
    // 4. 在事务中释放锁
    db::Connection conn = repo.get_connection();
    db::Transaction tx(conn, db::Transaction::RollbackPolicy::ON_EXCEPTION);
    
    std::string query = R"(
        UPDATE sessions
        SET lock_id = NULL,
            lock_owner_user_id = NULL,
            lock_expires_at = NULL
        WHERE session_id = $1::uuid
          AND lock_owner_user_id = $2::uuid
          AND lock_expires_at IS NOT NULL
          AND lock_expires_at > now()
    )";
    
    const char* params[2] = {
        session_id.c_str(),
        user_id_opt->c_str()
    };
    
    int lens[2] = {
        static_cast<int>(session_id.size()),
        static_cast<int>(user_id_opt->size())
    };
    
    std::string error_msg;
    bool success = repo.execute_command(query, {params, 2}, {lens, 2}, &error_msg);
    if (!success) {
        res.status = 500;
        res.set_content("{\"error\":\"internal\",\"details\":\"Failed to unlock session: " + error_msg + "\"}", "application/json");
        return;
    }
    
    tx.commit();
    
    // 5. 记录审计日志
    repo.audit_log(
        *user_id_opt,
        "session_unlock",
        std::nullopt,
        session_id,
        req.get_header_value("X-Forwarded-For"),
        req.get_header_value("User-Agent"),
        "{}"
    );
    
    // 6. 返回响应
    nlohmann::json response;
    response["sessionId"] = session_id;
    response["unlocked"] = true;
    response["unlockedAt"] = teleop::common::Timestamp::to_iso8601(teleop::common::Timestamp::now_ms());
    response["unlockedBy"] = *user_id_opt;
    
    res.status = 200;
    res.set_content(response.dump(), "application/json");
    
    std::cout << "[Backend][UnlockSession] Success sessionId=" << session_id << std::endl;
}

} // namespace teleop::api
