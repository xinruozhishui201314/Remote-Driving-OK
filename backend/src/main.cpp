/**
 * Teleop Backend - M1 第一～八批
 * GET /health, GET /ready（DB 可达 200，不可达 503）；
 * GET /api/v1/me、GET /api/v1/vins、
 * POST /api/v1/vins/{vin}/sessions、
 * GET /api/v1/sessions/{sessionId}/streams、
 * POST /api/v1/sessions/{sessionId}/lock（占位，需 Bearer JWT）。
 */

#include "httplib.h"
#include "auth/jwt_validator.h"
#include "health_handler.h"
#include "middleware/version_middleware.h"
#include "common/utils.h"
#include <nlohmann/json.hpp>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <vector>
#include <fstream>
#include <cerrno>
#include <cstring>
#include <thread>
#include <chrono>
#include <libpq-fe.h>

static int get_port() {
    const char* p = std::getenv("PORT");
    if (p) {
        int v = std::atoi(p);
        if (v > 0 && v <= 65535) return v;
    }
    return 8080;
}

static std::string get_env(const char* key, const std::string& fallback) {
    const char* p = std::getenv(key);
    return p ? std::string(p) : fallback;
}

static std::string get_version() {
    const char* p = std::getenv("VERSION");
    if (p && p[0]) return std::string(p);
    std::ifstream f("../VERSION.txt");
    if (!f.good()) f = std::ifstream("VERSION.txt");
    std::string v = "unknown";
    if (f.good()) std::getline(f, v);
    return v.empty() ? "unknown" : "v" + v;
}

/** 生成 UUID v4 格式字符串（简单实现，用于占位 sessionId） */
static std::string generate_uuid_v4() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);
    static std::uniform_int_distribution<> dis2(8, 11);
    std::ostringstream oss;
    oss << std::hex;
    for (int i = 0; i < 8; i++) oss << dis(gen);
    oss << "-";
    for (int i = 0; i < 4; i++) oss << dis(gen);
    oss << "-4";
    for (int i = 0; i < 3; i++) oss << dis(gen);
    oss << "-";
    oss << dis2(gen);
    for (int i = 0; i < 3; i++) oss << dis(gen);
    oss << "-";
    for (int i = 0; i < 12; i++) oss << dis(gen);
    return oss.str();
}

/** 从 URL 字符串解析 host 与 port（如 http://host:80/path 或 https://host）。返回 (host, port)。 */
static void parse_host_port(const std::string& url, std::string& out_host, std::string& out_port) {
    out_host = "zlmediakit";
    out_port = "80";
    size_t scheme_end = url.find("://");
    if (scheme_end == std::string::npos) return;
    size_t host_start = scheme_end + 3;
    size_t host_end = url.find("/", host_start);
    if (host_end == std::string::npos) host_end = url.size();
    std::string host_port = url.substr(host_start, host_end - host_start);
    size_t colon = host_port.find(":");
    if (colon != std::string::npos) {
        out_host = host_port.substr(0, colon);
        out_port = host_port.substr(colon + 1);
    } else {
        out_host = host_port;
        if (url.substr(0, 8) == "https://") out_port = "443";
        else if (url.substr(0, 7) == "http://") out_port = "80";
    }
}

/** 生成 WHEP 播放地址的 host:port；优先使用 ZLM_PUBLIC_BASE（客户端可见地址），否则用 ZLM_API_URL。 */
static void whep_whip_host_port(std::string& host, std::string& port) {
    const char* pub = std::getenv("ZLM_PUBLIC_BASE");
    if (pub && pub[0]) {
        parse_host_port(std::string(pub), host, port);
        return;
    }
    std::string zlm_api = get_env("ZLM_API_URL", "http://zlmediakit/index/api");
    parse_host_port(zlm_api, host, port);
}

/**
 * 生成 WHEP 播放地址。
 * 客户端从此 URL 解析出 base host，并结合自身存储的 VIN 构造四路流名：
 *   {vin}_cam_front, {vin}_cam_rear, {vin}_cam_left, {vin}_cam_right
 * stream 参数设为 {vin}_cam_front 作为示意；客户端不使用 stream 参数，仅使用 host + app。
 */
static std::string build_whep_url(const std::string& zlm_api_url, const std::string& vin, const std::string& session_id) {
    (void)session_id;  // 不再使用 sessionId（流名由 VIN 决定，无需会话隔离）
    std::string host, port;
    whep_whip_host_port(host, port);
    std::string stream_name = vin + "_cam_front";  // 代表性流名，客户端解析 host 后自行构造四路
    return "whep://" + host + ":" + port + "/index/api/webrtc?app=teleop&stream=" + stream_name + "&type=play";
}

/**
 * 生成 WHIP 推流地址。
 * 车端推流使用 VIN-prefixed 流名，格式 {vin}_cam_front 等。
 * 此处返回 cam_front 作为代表，车端 SDK 应自行构造四路地址。
 */
static std::string build_whip_url(const std::string& zlm_api_url, const std::string& vin, const std::string& session_id) {
    (void)session_id;
    std::string host, port;
    whep_whip_host_port(host, port);
    std::string stream_name = vin + "_cam_front";
    return "whip://" + host + ":" + port + "/index/api/webrtc?app=teleop&stream=" + stream_name + "&type=push";
}

/** 若 DATABASE_URL 为空则视为无 DB 要求返回 true；否则连接并 SELECT 1，成功返回 true。 */
static bool check_db_ready(const std::string& database_url) {
    if (database_url.empty())
        return true;
    // PQconnectdb 在数据库不可达时会长时间阻塞。使用带超时的独立线程模拟连接超时，
    // 避免 health check 线程被 DB 不可达阻塞整个进程响应。
    PGconn* conn = nullptr;
    std::atomic<bool> done{false};
    std::thread conn_thread([&]() {
        conn = PQconnectdb(database_url.c_str());
        done.store(true, std::memory_order_release);
    });
    // 轮询 done 标志，最长等待 kConnectTimeoutMs ms
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(5000);
    while (std::chrono::steady_clock::now() < deadline) {
        if (done.load(std::memory_order_acquire)) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    if (!done.load(std::memory_order_acquire)) {
        // 超时：不等线程退出（PQconnectdb 内部无 cancel API），记录并标记失败
        std::cerr << "[Backend][DB] PQconnectdb timed out after 5000ms — database unreachable" << std::endl;
        // 不调用 terminate() — 健康检查线程应该优雅失败而不是崩溃进程
        if (conn_thread.joinable()) conn_thread.detach();
        return false;
    }
    if (conn_thread.joinable()) conn_thread.join();
    if (!conn || PQstatus(conn) != CONNECTION_OK) {
        if (conn) PQfinish(conn);
        return false;
    }
    PGresult* res = PQexec(conn, "SELECT 1");
    bool ok = (res && PQresultStatus(res) == PGRES_TUPLES_OK);
    if (res) PQclear(res);
    PQfinish(conn);
    return ok;
}

/** 根据 keycloak_sub 查 users → account_vehicles + vin_grants，返回可访问的 VIN 列表。失败返回 nullopt（调用方返回 503）。若 out_user_id 非空，则一并返回 user_id。 */
static std::optional<std::vector<std::string>> get_vins_for_sub(const std::string& database_url,
                                                                 const std::string& keycloak_sub,
                                                                 std::string* out_user_id = nullptr) {
    if (database_url.empty())
        return std::vector<std::string>{};
    PGconn* conn = PQconnectdb(database_url.c_str());
    if (!conn || PQstatus(conn) != CONNECTION_OK) {
        if (conn) PQfinish(conn);
        return std::nullopt;
    }
    const char* param1 = keycloak_sub.c_str();
    int len1 = static_cast<int>(keycloak_sub.size());
    PGresult* res = PQexecParams(conn, "SELECT id::text, account_id::text FROM users WHERE keycloak_sub = $1", 1, nullptr, &param1, &len1, nullptr, 0);
    if (!res || PQresultStatus(res) != PGRES_TUPLES_OK) {
        if (res) PQclear(res);
        PQfinish(conn);
        return std::nullopt;
    }
    int n = PQntuples(res);
    if (n == 0) {
        PQclear(res);
        PQfinish(conn);
        return std::vector<std::string>{};
    }
    const char* user_id = PQgetvalue(res, 0, 0);
    const char* account_id = PQgetvalue(res, 0, 1);
    if (!user_id || !account_id) {
        PQclear(res);
        PQfinish(conn);
        return std::vector<std::string>{};
    }
    std::string uid(user_id);
    std::string aid(account_id);
    PQclear(res);

    if (out_user_id) {
        *out_user_id = uid;
    }

    const char* params[2] = { aid.c_str(), uid.c_str() };
    int lens[2] = { static_cast<int>(aid.size()), static_cast<int>(uid.size()) };
    res = PQexecParams(conn,
        "SELECT vin FROM account_vehicles WHERE account_id = $1::uuid AND status = 'active' "
        "UNION SELECT vin FROM vin_grants WHERE grantee_user_id = $2::uuid AND (expires_at IS NULL OR expires_at > now())",
        2, nullptr, params, lens, nullptr, 0);
    if (!res || PQresultStatus(res) != PGRES_TUPLES_OK) {
        if (res) PQclear(res);
        PQfinish(conn);
        return std::nullopt;
    }
    std::vector<std::string> vins;
    n = PQntuples(res);
    for (int i = 0; i < n; i++) {
        const char* v = PQgetvalue(res, i, 0);
        if (v) vins.emplace_back(v);
    }
    PQclear(res);
    PQfinish(conn);
    return vins;
}

/** 根据 keycloak_sub 查 users 表，返回该用户所属的 account_id。用户不存在或未绑定账号时返回 nullopt。 */
static std::optional<std::string> get_account_id_for_sub(const std::string& database_url, const std::string& keycloak_sub) {
    if (database_url.empty()) return std::nullopt;
    PGconn* conn = PQconnectdb(database_url.c_str());
    if (!conn || PQstatus(conn) != CONNECTION_OK) {
        if (conn) PQfinish(conn);
        return std::nullopt;
    }
    const char* param = keycloak_sub.c_str();
    int len = static_cast<int>(keycloak_sub.size());
    PGresult* res = PQexecParams(conn, "SELECT account_id::text FROM users WHERE keycloak_sub = $1", 1, nullptr, &param, &len, nullptr, 0);
    if (!res || PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0) {
        if (res) PQclear(res);
        PQfinish(conn);
        return std::nullopt;
    }
    const char* account_id = PQgetvalue(res, 0, 0);
    std::string aid = account_id ? std::string(account_id) : "";
    PQclear(res);
    PQfinish(conn);
    return aid.empty() ? std::nullopt : std::optional<std::string>(aid);
}

/** 根据 keycloak_sub 查 users 表，返回该用户的 id (UUID)。用于 vin_grants.created_by / grantee_user_id。 */
static std::optional<std::string> get_user_id_for_sub(const std::string& database_url, const std::string& keycloak_sub) {
    if (database_url.empty()) return std::nullopt;
    PGconn* conn = PQconnectdb(database_url.c_str());
    if (!conn || PQstatus(conn) != CONNECTION_OK) {
        if (conn) PQfinish(conn);
        return std::nullopt;
    }
    const char* param = keycloak_sub.c_str();
    int len = static_cast<int>(keycloak_sub.size());
    PGresult* res = PQexecParams(conn, "SELECT id::text FROM users WHERE keycloak_sub = $1", 1, nullptr, &param, &len, nullptr, 0);
    if (!res || PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0) {
        if (res) PQclear(res);
        PQfinish(conn);
        return std::nullopt;
    }
    const char* uid = PQgetvalue(res, 0, 0);
    std::string id = uid ? std::string(uid) : "";
    PQclear(res);
    PQfinish(conn);
    return id.empty() ? std::nullopt : std::optional<std::string>(id);
}

/** Just-In-Time 同步：若 users 表中无此 keycloak_sub，则创建 account + user（账号名 Auto-{sub}），
 *  使在 Keycloak 8080 添加的用户首次访问 backend 时即可用。成功或用户已存在返回 true，DB 错误返回 false。 */
static bool ensure_user_for_sub(const std::string& database_url, const std::string& keycloak_sub,
                                const std::string& preferred_username, const std::string& email) {
    if (database_url.empty() || keycloak_sub.empty()) return false;
    PGconn* conn = PQconnectdb(database_url.c_str());
    if (!conn || PQstatus(conn) != CONNECTION_OK) {
        if (conn) PQfinish(conn);
        return false;
    }
    std::string account_name = "Auto-" + keycloak_sub;
    if (account_name.size() > 255) account_name = account_name.substr(0, 255);
    const char* p_name = account_name.c_str();
    int len_name = static_cast<int>(account_name.size());
    PGresult* ra = PQexecParams(conn, "INSERT INTO accounts (id, name) VALUES (uuid_generate_v4(), $1) ON CONFLICT (name) DO NOTHING", 1, nullptr, &p_name, &len_name, nullptr, 0);
    if (!ra || (PQresultStatus(ra) != PGRES_COMMAND_OK && PQresultStatus(ra) != PGRES_TUPLES_OK)) {
        if (ra) PQclear(ra);
        PQfinish(conn);
        return false;
    }
    PQclear(ra);
    PGresult* rs = PQexecParams(conn, "SELECT id::text FROM accounts WHERE name = $1", 1, nullptr, &p_name, &len_name, nullptr, 0);
    if (!rs || PQresultStatus(rs) != PGRES_TUPLES_OK || PQntuples(rs) == 0) {
        if (rs) PQclear(rs);
        PQfinish(conn);
        return false;
    }
    const char* aid = PQgetvalue(rs, 0, 0);
    std::string account_id = aid ? std::string(aid) : "";
    PQclear(rs);
    if (account_id.empty()) { PQfinish(conn); return false; }
    std::string uname = preferred_username.size() > 255 ? preferred_username.substr(0, 255) : preferred_username;
    std::string em = email.size() > 255 ? email.substr(0, 255) : email;
    const char* p_sub = keycloak_sub.c_str();
    int len_sub = static_cast<int>(keycloak_sub.size());
    const char* p_aid = account_id.c_str();
    int len_aid = static_cast<int>(account_id.size());
    const char* p_uname = uname.c_str();
    int len_uname = static_cast<int>(uname.size());
    const char* p_em = em.c_str();
    int len_em = static_cast<int>(em.size());
    const char* params[4] = { p_sub, p_aid, p_uname, p_em };
    int lens[4] = { len_sub, len_aid, len_uname, len_em };
    PGresult* ru = PQexecParams(conn,
        "INSERT INTO users (id, keycloak_sub, account_id, username, email) VALUES (uuid_generate_v4(), $1, $2::uuid, $3, $4) ON CONFLICT (keycloak_sub) DO NOTHING",
        4, nullptr, params, lens, nullptr, 0);
    if (!ru || (PQresultStatus(ru) != PGRES_COMMAND_OK && PQresultStatus(ru) != PGRES_TUPLES_OK)) {
        if (ru) PQclear(ru);
        PQfinish(conn);
        return false;
    }
    PQclear(ru);
    PQfinish(conn);
    std::cout << "[Backend][JIT] 已同步用户 sub=" << keycloak_sub << " account=" << account_id << " username=" << uname << std::endl;
    return true;
}

static int get_int_env(const char* key, int fallback) {
    const char* p = std::getenv(key);
    if (!p) return fallback;
    int v = std::atoi(p);
    return v > 0 ? v : fallback;
}

int main() {
    httplib::Server svr;
    const int port = get_port();

    // 初始化版本协商中间件
    std::string backend_version = get_env("BACKEND_VERSION", "1.1.0");
    teleop::middleware::VersionMiddleware version_middleware(backend_version);
    bool enable_version_validation = get_env("ENABLE_VERSION_VALIDATION", "true") == "true";
    
    std::cout << "[Backend] Version middleware initialized, version=" << backend_version 
              << ", validation=" << (enable_version_validation ? "enabled" : "disabled") << std::endl;

    std::string keycloak_url = get_env("KEYCLOAK_URL", "http://keycloak:8080");
    std::string keycloak_realm = get_env("KEYCLOAK_REALM", "teleop");
    std::string keycloak_client_id = get_env("KEYCLOAK_CLIENT_ID", "teleop-backend");
    std::string default_iss = keycloak_url + "/realms/" + keycloak_realm;
    std::vector<std::string> expected_issuers = { default_iss };
    std::string issuer_extra = get_env("KEYCLOAK_ISSUER_EXTRA", "");
    if (!issuer_extra.empty()) {
        for (size_t pos = 0; pos < issuer_extra.size(); ) {
            size_t next = issuer_extra.find(',', pos);
            std::string s = (next == std::string::npos) ? issuer_extra.substr(pos) : issuer_extra.substr(pos, next - pos);
            pos = (next == std::string::npos) ? issuer_extra.size() : next + 1;
            while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(0, 1);
            while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
            if (!s.empty()) expected_issuers.push_back(s);
        }
    }
    std::vector<std::string> expected_aud = { keycloak_client_id, "account", "teleop-client" };

    // 会话与锁 TTL（秒）
    const int session_ttl_seconds = get_int_env("SESSION_TTL_SECONDS", 1800); // 默认 30 分钟
    const int lock_ttl_seconds = get_int_env("LOCK_TTL_SECONDS", session_ttl_seconds);

    // 注册健康检查端点（/health 与 /ready），统一格式与依赖检查
    {
        std::string database_url = get_env("DATABASE_URL", "");
        std::string zlm_api_url = get_env("ZLM_API_URL", "http://zlmediakit/index/api");
        std::string version = get_version();
        teleop::health::register_handlers(svr, database_url, zlm_api_url, version);
        std::cout << "[Backend] 已注册 /health 与 /ready 端点，version=" << version << std::endl;
    }

    // GET /api/v1/me - 需 Authorization: Bearer <JWT>
    svr.Get("/api/v1/me", [&expected_issuers, &expected_aud, &version_middleware, enable_version_validation](const httplib::Request& req, httplib::Response& res) {
        // 版本协商
        if (enable_version_validation) {
            std::string client_version = req.get_header_value("API-Version");
            std::string error_msg;
            if (!version_middleware.validate_client_version(client_version, error_msg)) {
                res.status = 400;
                nlohmann::json err;
                err["error"] = "version_mismatch";
                err["details"] = error_msg;
                err["clientVersion"] = client_version;
                err["serverVersion"] = version_middleware.get_backend_version();
                res.set_content(err.dump(), "application/json");
                return;
            }
            
            // 设置响应版本头
            std::string response_version = version_middleware.get_response_version(
                teleop::middleware::Version::parse(client_version)
            );
            res.set_header("API-Version", response_version);
        }
        
        std::string auth = req.get_header_value("Authorization");
        if (auth.empty() || auth.find("Bearer ") != 0) {
            res.status = 401;
            res.set_content("{\"error\":\"unauthorized\"}", "application/json");
            return;
        }
        std::string token = auth.substr(7);
        auto payload_opt = teleop::auth::validate_jwt_claims(token, expected_issuers, expected_aud);
        if (!payload_opt) {
            res.status = 401;
            res.set_content("{\"error\":\"unauthorized\"}", "application/json");
            return;
        }
        try {
            auto j = nlohmann::json::parse(*payload_opt);
            nlohmann::json out;
            out["apiVersion"] = "1.1.0";  // 添加版本字段
            out["sub"] = j.value("sub", "");
            out["preferred_username"] = j.value("preferred_username", "");
            out["email"] = j.value("email", "");  // v1.1.0新增
            if (j.contains("realm_access") && j["realm_access"].contains("roles")) {
                out["roles"] = j["realm_access"]["roles"];
            } else {
                out["roles"] = nlohmann::json::array();
            }
            res.status = 200;
            res.set_content(out.dump(), "application/json");
        } catch (...) {
            res.status = 401;
            res.set_content("{\"error\":\"unauthorized\"}", "application/json");
        }
    });

    // GET /api/v1/vins - 需 JWT，从 DB 查 users → account_vehicles + vin_grants
    svr.Get("/api/v1/vins", [&expected_issuers, &expected_aud, &version_middleware, enable_version_validation](const httplib::Request& req, httplib::Response& res) {
        // 版本协商
        if (enable_version_validation) {
            std::string client_version = req.get_header_value("API-Version");
            std::string error_msg;
            if (!version_middleware.validate_client_version(client_version, error_msg)) {
                res.status = 400;
                nlohmann::json err;
                err["error"] = "version_mismatch";
                err["details"] = error_msg;
                err["clientVersion"] = client_version;
                err["serverVersion"] = version_middleware.get_backend_version();
                res.set_content(err.dump(), "application/json");
                return;
            }
            
            // 设置响应版本头
            std::string response_version = version_middleware.get_response_version(
                teleop::middleware::Version::parse(client_version)
            );
            res.set_header("API-Version", response_version);
        }
        
        std::string auth = req.get_header_value("Authorization");
        if (auth.empty() || auth.find("Bearer ") != 0) {
            res.status = 401;
            res.set_content("{\"error\":\"unauthorized\"}", "application/json");
            return;
        }
        std::string token = auth.substr(7);
        auto payload_opt = teleop::auth::validate_jwt_claims(token, expected_issuers, expected_aud);
        if (!payload_opt) {
            res.status = 401;
            res.set_content("{\"error\":\"unauthorized\"}", "application/json");
            return;
        }
        std::string sub, preferred_username, email;
        try {
            auto j = nlohmann::json::parse(*payload_opt);
            sub = j.value("sub", "");
            preferred_username = j.value("preferred_username", "");
            email = j.value("email", "");
        } catch (...) {
            res.status = 401;
            res.set_content("{\"error\":\"unauthorized\"}", "application/json");
            return;
        }
        std::string database_url = get_env("DATABASE_URL", "");
        auto vins_opt = get_vins_for_sub(database_url, sub, nullptr);
        if (!vins_opt) {
            if (ensure_user_for_sub(database_url, sub, preferred_username, email)) {
                vins_opt = get_vins_for_sub(database_url, sub, nullptr);
            }
            if (!vins_opt) {
                std::cout << "[Backend][GET /api/v1/vins] sub=" << sub << " failed code=E_DB_QUERY" << std::endl;
                res.status = 503;
                res.set_content("{\"error\":\"internal\",\"code\":\"E_DB_QUERY\"}", "application/json");
                return;
            }
        }
        nlohmann::json out = nlohmann::json::object();
        out["apiVersion"] = "1.1.0";  // 添加版本字段
        out["vins"] = *vins_opt;
        res.status = 200;
        res.set_content(out.dump(), "application/json");
        std::cout << "[Backend][GET /api/v1/vins] sub=" << sub << " vins=" << out["vins"].dump() << std::endl;
    });

    // POST /api/v1/vehicles - 需 JWT，将新车绑定到当前用户所在账号（网页管理端“增加车辆”用）
    svr.Post("/api/v1/vehicles", [&expected_issuers, &expected_aud](const httplib::Request& req, httplib::Response& res) {
        std::cout << "[Backend][POST /api/v1/vehicles] 进入 handler（请求已匹配到该路由）" << std::endl;
        std::string auth = req.get_header_value("Authorization");
        if (auth.empty() || auth.find("Bearer ") != 0) {
            res.status = 401;
            res.set_content("{\"error\":\"unauthorized\"}", "application/json");
            return;
        }
        std::string token = auth.substr(7);
        auto payload_opt = teleop::auth::validate_jwt_claims(token, expected_issuers, expected_aud);
        if (!payload_opt) {
            res.status = 401;
            res.set_content("{\"error\":\"unauthorized\"}", "application/json");
            return;
        }
        std::string sub, preferred_username, email;
        try {
            auto j = nlohmann::json::parse(*payload_opt);
            sub = j.value("sub", "");
            preferred_username = j.value("preferred_username", "");
            email = j.value("email", "");
        } catch (...) {
            res.status = 401;
            res.set_content("{\"error\":\"unauthorized\"}", "application/json");
            return;
        }
        std::string database_url = get_env("DATABASE_URL", "");
        if (database_url.empty()) {
            std::cout << "[Backend][POST /api/v1/vehicles] 403 sub=" << sub << " (DATABASE_URL 未配置，无法连接数据库查 users)" << std::endl;
            res.status = 403;
            res.set_content("{\"error\":\"forbidden\",\"details\":\"user not found or no account (DATABASE_URL not configured)\"}", "application/json");
            return;
        }
        auto account_id_opt = get_account_id_for_sub(database_url, sub);
        if (!account_id_opt || account_id_opt->empty()) {
            // Just-In-Time：Keycloak 中新建用户首次访问时自动在 backend 创建 account + user
            if (ensure_user_for_sub(database_url, sub, preferred_username, email)) {
                account_id_opt = get_account_id_for_sub(database_url, sub);
            }
            if (!account_id_opt || account_id_opt->empty()) {
                std::cout << "[Backend][POST /api/v1/vehicles] 403 sub=" << sub << " (user not found or no account; 若 JIT 已执行仍失败，请检查 DB 是否已执行 migrations/001 及 ensure-seed-data.sh)" << std::endl;
                res.status = 403;
                res.set_content("{\"error\":\"forbidden\",\"details\":\"user not found or no account\"}", "application/json");
                return;
            }
        }
        auto user_id_opt = get_user_id_for_sub(database_url, sub);
        if (!user_id_opt || user_id_opt->empty()) {
            std::cout << "[Backend][POST /api/v1/vehicles] 403 sub=" << sub << " (user id not found)" << std::endl;
            res.status = 403;
            res.set_content("{\"error\":\"forbidden\",\"details\":\"user id not found\"}", "application/json");
            return;
        }
        std::string vin, model;
        try {
            auto body = nlohmann::json::parse(req.body);
            vin = body.value("vin", "");
            model = body.value("model", "");
        } catch (...) {
            std::cout << "[Backend][POST /api/v1/vehicles] 400 invalid JSON body" << std::endl;
            res.status = 400;
            res.set_content("{\"error\":\"bad_request\",\"details\":\"invalid JSON or missing vin\"}", "application/json");
            return;
        }
        std::cout << "[Backend][POST /api/v1/vehicles] 请求 sub=" << sub << " vin=" << vin << " model=" << model << std::endl;
        if (vin.empty()) {
            std::cout << "[Backend][POST /api/v1/vehicles] 400 vin empty" << std::endl;
            res.status = 400;
            res.set_content("{\"error\":\"bad_request\",\"details\":\"vin is required\"}", "application/json");
            return;
        }
        if (vin.size() > 17) {
            std::cout << "[Backend][POST /api/v1/vehicles] 400 vin too long len=" << vin.size() << std::endl;
            res.status = 400;
            res.set_content("{\"error\":\"bad_request\",\"details\":\"vin max 17 chars\"}", "application/json");
            return;
        }
        PGconn* conn = PQconnectdb(database_url.c_str());
        if (!conn || PQstatus(conn) != CONNECTION_OK) {
            if (conn) PQfinish(conn);
            res.status = 503;
            res.set_content("{\"error\":\"internal\"}", "application/json");
            return;
        }
        const char* p_vin = vin.c_str();
        const char* p_model = model.c_str();
        int len_vin = static_cast<int>(vin.size());
        int len_model = static_cast<int>(model.size());
        const char* params_ins[2] = { p_vin, p_model };
        int lens_ins[2] = { len_vin, len_model };
        PGresult* rv = PQexecParams(conn, "INSERT INTO vehicles (vin, model) VALUES ($1, $2) ON CONFLICT (vin) DO NOTHING", 2, nullptr, params_ins, lens_ins, nullptr, 0);
        if (!rv || (PQresultStatus(rv) != PGRES_COMMAND_OK && PQresultStatus(rv) != PGRES_TUPLES_OK)) {
            std::cout << "[Backend][AddVehicle] 503 vehicles insert failed vin=" << vin << " err=" << (conn ? PQerrorMessage(conn) : "") << std::endl;
            if (rv) PQclear(rv);
            PQfinish(conn);
            res.status = 503;
            res.set_content("{\"error\":\"internal\",\"details\":\"insert vehicles failed\"}", "application/json");
            return;
        }
        std::cout << "[Backend][AddVehicle] vehicles inserted vin=" << vin << " model=" << model << std::endl;
        PQclear(rv);
        const char* p_aid = account_id_opt->c_str();
        int len_aid = static_cast<int>(account_id_opt->size());
        const char* params_av[3] = { p_aid, p_vin, "active" };
        int lens_av[3] = { len_aid, len_vin, 6 };
        PGresult* rav = PQexecParams(conn, "INSERT INTO account_vehicles (account_id, vin, status) VALUES ($1::uuid, $2, $3) ON CONFLICT (account_id, vin) DO NOTHING", 3, nullptr, params_av, lens_av, nullptr, 0);
        if (!rav || (PQresultStatus(rav) != PGRES_COMMAND_OK && PQresultStatus(rav) != PGRES_TUPLES_OK)) {
            std::cout << "[Backend][AddVehicle] 503 account_vehicles insert failed vin=" << vin << " err=" << (conn ? PQerrorMessage(conn) : "") << std::endl;
            if (rav) PQclear(rav);
            PQfinish(conn);
            res.status = 503;
            res.set_content("{\"error\":\"internal\",\"details\":\"insert account_vehicles failed\"}", "application/json");
            return;
        }
        std::cout << "[Backend][AddVehicle] account_vehicles inserted vin=" << vin << " account_id=" << *account_id_opt << std::endl;
        PQclear(rav);
        // 为当前用户写入 vin_grants：vin.view + vin.control + vin.maintain，保证整条远驾链路认可、可正常接管
        const char* p_uid = user_id_opt->c_str();
        int len_uid = static_cast<int>(user_id_opt->size());
        const char* params_vg[3] = { p_vin, p_uid, p_uid };
        int lens_vg[3] = { len_vin, len_uid, len_uid };
        PGresult* rvg = PQexecParams(conn,
            "INSERT INTO vin_grants (vin, grantee_user_id, permissions, created_by) VALUES ($1, $2::uuid, ARRAY['vin.view','vin.control','vin.maintain'], $3::uuid) "
            "ON CONFLICT (vin, grantee_user_id) DO UPDATE SET permissions = ARRAY['vin.view','vin.control','vin.maintain'], updated_at = now()",
            3, nullptr, params_vg, lens_vg, nullptr, 0);
        if (!rvg || (PQresultStatus(rvg) != PGRES_COMMAND_OK && PQresultStatus(rvg) != PGRES_TUPLES_OK)) {
            std::cout << "[Backend][AddVehicle] 503 vin_grants insert failed vin=" << vin << " grantee_user_id=" << *user_id_opt << " err=" << (conn ? PQerrorMessage(conn) : "") << std::endl;
            if (rvg) PQclear(rvg);
            PQfinish(conn);
            res.status = 503;
            res.set_content("{\"error\":\"internal\",\"details\":\"insert vin_grants failed\"}", "application/json");
            return;
        }
        std::cout << "[Backend][AddVehicle] vin_grants inserted vin=" << vin << " grantee_user_id=" << *user_id_opt << " permissions=vin.view,vin.control,vin.maintain" << std::endl;
        PQclear(rvg);
        PQfinish(conn);
        nlohmann::json out = nlohmann::json::object();
        out["vin"] = vin;
        out["model"] = model;
        res.status = 201;
        res.set_content(out.dump(), "application/json");
        std::cout << "[Backend][POST /api/v1/vehicles] 201 vin=" << vin << std::endl;
        // Plan 5.8: 审计日志格式化 (JSON)
        auto now = std::chrono::system_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        time_t tt = std::chrono::system_clock::to_time_t(now);
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", std::gmtime(&tt));
        std::cout << "{\"timestamp\": \"" << buf << "." << (ms % 1000) << "\", \"level\": \"AUDIT\", \"event\": \"VEHICLE_ADD\", \"vin\": \"" << vin << "\", \"user\": \"" << sub << "\", \"account_id\": \"" << *account_id_opt << "\"}" << std::endl;
    });

    // DELETE /api/v1/vehicles/{vin} - 需 JWT，从当前用户所在账号解除该 VIN 绑定（删除后该账号选车列表不再显示，数据持久化至下次启动仍生效除非再次添加）
    svr.Delete("/api/v1/vehicles/.*", [&expected_issuers, &expected_aud](const httplib::Request& req, httplib::Response& res) {
        std::string auth = req.get_header_value("Authorization");
        if (auth.empty() || auth.find("Bearer ") != 0) {
            res.status = 401;
            res.set_content("{\"error\":\"unauthorized\"}", "application/json");
            return;
        }
        std::string token = auth.substr(7);
        auto payload_opt = teleop::auth::validate_jwt_claims(token, expected_issuers, expected_aud);
        if (!payload_opt) {
            res.status = 401;
            res.set_content("{\"error\":\"unauthorized\"}", "application/json");
            return;
        }
        std::string sub, preferred_username, email;
        try {
            auto j = nlohmann::json::parse(*payload_opt);
            sub = j.value("sub", "");
            preferred_username = j.value("preferred_username", "");
            email = j.value("email", "");
        } catch (...) {
            res.status = 401;
            res.set_content("{\"error\":\"unauthorized\"}", "application/json");
            return;
        }
        std::string path = req.path;
        size_t qmark = path.find('?');
        if (qmark != std::string::npos) path = path.substr(0, qmark);
        size_t prefix = path.find("/vehicles/");
        if (prefix == std::string::npos) {
            res.status = 400;
            res.set_content("{\"error\":\"invalid_path\"}", "application/json");
            return;
        }
        std::string vin = path.substr(prefix + 9); // "/vehicles/".length() == 9
        while (!vin.empty() && vin.front() == '/') vin.erase(0, 1);
        while (!vin.empty() && vin.back() == '/') vin.pop_back();
        if (vin.empty()) {
            res.status = 400;
            res.set_content("{\"error\":\"bad_request\",\"details\":\"vin required\"}", "application/json");
            return;
        }
        std::string database_url = get_env("DATABASE_URL", "");
        auto account_id_opt = get_account_id_for_sub(database_url, sub);
        if (!account_id_opt || account_id_opt->empty()) {
            if (ensure_user_for_sub(database_url, sub, preferred_username, email)) {
                account_id_opt = get_account_id_for_sub(database_url, sub);
            }
            if (!account_id_opt || account_id_opt->empty()) {
                std::cout << "[Backend][DELETE /api/v1/vehicles/" << vin << "] 403 sub=" << sub << " (user not found or no account)" << std::endl;
                res.status = 403;
                res.set_content("{\"error\":\"forbidden\",\"details\":\"user not found or no account\"}", "application/json");
                return;
            }
        }
        PGconn* conn = PQconnectdb(database_url.c_str());
        if (!conn || PQstatus(conn) != CONNECTION_OK) {
            if (conn) PQfinish(conn);
            res.status = 503;
            res.set_content("{\"error\":\"internal\"}", "application/json");
            return;
        }
        const char* p_aid = account_id_opt->c_str();
        const char* p_vin = vin.c_str();
        int len_aid = static_cast<int>(account_id_opt->size());
        int len_vin = static_cast<int>(vin.size());
        const char* params[2] = { p_aid, p_vin };
        int lens[2] = { len_aid, len_vin };
        PGresult* rd = PQexecParams(conn, "DELETE FROM account_vehicles WHERE account_id = $1::uuid AND vin = $2", 2, nullptr, params, lens, nullptr, 0);
        if (!rd || PQresultStatus(rd) != PGRES_COMMAND_OK) {
            std::cout << "[Backend][DELETE /api/v1/vehicles/" << vin << "] 503 delete failed: " << (conn ? PQerrorMessage(conn) : "") << std::endl;
            if (rd) PQclear(rd);
            PQfinish(conn);
            res.status = 503;
            res.set_content("{\"error\":\"internal\"}", "application/json");
            return;
        }
        // 解除绑定时同步删除该账号下该 VIN 的 vin_grants（参数顺序 $1=vin $2=account_id）
        const char* params_vg[2] = { p_vin, p_aid };
        int lens_vg[2] = { len_vin, len_aid };
        PGresult* rdg = PQexecParams(conn, "DELETE FROM vin_grants WHERE vin = $1 AND grantee_user_id IN (SELECT id FROM users WHERE account_id = $2::uuid)", 2, nullptr, params_vg, lens_vg, nullptr, 0);
        int n_grants = 0;
        if (rdg) {
            const char* tuples = PQcmdTuples(rdg);
            if (tuples && *tuples) n_grants = std::atoi(tuples);
            std::cout << "[Backend][AddVehicle] vin_grants deleted vin=" << vin << " account_id=" << *account_id_opt << " rows=" << n_grants << std::endl;
            PQclear(rdg);
        }
        int n = PQcmdTuples(rd) ? std::atoi(PQcmdTuples(rd)) : 0;
        PQclear(rd);
        PQfinish(conn);
        if (n == 0) {
            std::cout << "[Backend][DELETE /api/v1/vehicles/" << vin << "] 404 本账号未绑定该 VIN account_id=" << *account_id_opt << std::endl;
            res.status = 404;
            res.set_content("{\"error\":\"not_found\",\"details\":\"vehicle not bound to this account\"}", "application/json");
            return;
        }
        std::cout << "[Backend][DELETE /api/v1/vehicles/" << vin << "] 204 已解除绑定 vin=" << vin << " account_id=" << *account_id_opt << " vin_grants_deleted=" << n_grants << " (客户端刷新选车页后不再显示)" << std::endl;
        res.status = 204;
        res.set_content("", "text/plain");
    });

    // POST /api/v1/vins/{vin}/sessions - 需 JWT，校验 VIN 权限后在 DB 创建 session 并返回 session_id + 媒体 URL
    svr.Post("/api/v1/vins/.*/sessions", [&expected_issuers, &expected_aud, session_ttl_seconds, &version_middleware, enable_version_validation](const httplib::Request& req, httplib::Response& res) {
        // 版本协商
        if (enable_version_validation) {
            std::string client_version = req.get_header_value("API-Version");
            std::string error_msg;
            if (!version_middleware.validate_client_version(client_version, error_msg)) {
                res.status = 400;
                nlohmann::json err;
                err["error"] = "version_mismatch";
                err["details"] = error_msg;
                err["clientVersion"] = client_version;
                err["serverVersion"] = version_middleware.get_backend_version();
                res.set_content(err.dump(), "application/json");
                return;
            }
            
            // 设置响应版本头
            std::string response_version = version_middleware.get_response_version(
                teleop::middleware::Version::parse(client_version)
            );
            res.set_header("API-Version", response_version);
        }
        
        std::string auth = req.get_header_value("Authorization");
        if (auth.empty() || auth.find("Bearer ") != 0) {
            res.status = 401;
            res.set_content("{\"error\":\"unauthorized\"}", "application/json");
            return;
        }
        std::string token = auth.substr(7);
        auto payload_opt = teleop::auth::validate_jwt_claims(token, expected_issuers, expected_aud);
        if (!payload_opt) {
            res.status = 401;
            res.set_content("{\"error\":\"unauthorized\"}", "application/json");
            return;
        }
        std::string sub;
        try {
            auto j = nlohmann::json::parse(*payload_opt);
            sub = j.value("sub", "");
        } catch (...) {
            res.status = 401;
            res.set_content("{\"error\":\"unauthorized\"}", "application/json");
            return;
        }
        // 提取路径中的 VIN：/api/v1/vins/{vin}/sessions
        std::string path = req.path;
        size_t vin_start = path.find("/vins/");
        if (vin_start == std::string::npos) {
            res.status = 400;
            res.set_content("{\"error\":\"invalid_path\"}", "application/json");
            return;
        }
        vin_start += 6; // "/vins/".length()
        size_t vin_end = path.find("/sessions", vin_start);
        if (vin_end == std::string::npos) {
            res.status = 400;
            res.set_content("{\"error\":\"invalid_path\"}", "application/json");
            return;
        }
        std::string vin = path.substr(vin_start, vin_end - vin_start);
        // 获取用户可访问的 VIN 列表
        std::string database_url = get_env("DATABASE_URL", "");
        std::string controller_user_id;
        auto vins_opt = get_vins_for_sub(database_url, sub, &controller_user_id);
        if (!vins_opt) {
            std::cout << "[Backend][POST sessions] 503 reason=get_vins_for_sub_failed vin=" << vin << " sub=" << sub << std::endl;
            nlohmann::json error_json = nlohmann::json::object();
            error_json["error"] = "internal";
            error_json["details"] = "get_vins_for_sub failed for sub=" + sub;
            res.status = 503;
            res.set_content(error_json.dump(), "application/json");
            return;
        }
        if (controller_user_id.empty()) {
            std::cout << "[Backend][POST sessions] 503 reason=controller_user_id_empty vin=" << vin << " sub=" << sub << " (users 表无此 keycloak_sub 或未返回 user_id)" << std::endl;
            nlohmann::json error_json = nlohmann::json::object();
            error_json["error"] = "internal";
            error_json["details"] = "controller_user_id is empty for sub=" + sub;
            res.status = 503;
            res.set_content(error_json.dump(), "application/json");
            return;
        }
        // 检查 VIN 是否在列表中
        bool has_access = false;
        for (const auto& v : *vins_opt) {
            if (v == vin) {
                has_access = true;
                break;
            }
        }
        if (!has_access) {
            res.status = 403;
            res.set_content("{\"error\":\"forbidden\"}", "application/json");
            return;
        }
        // 在 DB 中创建 session 记录并返回 session_id
        PGconn* conn = PQconnectdb(database_url.c_str());
        if (!conn || PQstatus(conn) != CONNECTION_OK) {
            std::string conn_error = conn ? PQerrorMessage(conn) : "Failed to create connection";
            if (conn) PQfinish(conn);
            std::cout << "[Backend][POST sessions] 503 reason=db_connection_failed vin=" << vin << " error=" << conn_error << std::endl;
            nlohmann::json error_json = nlohmann::json::object();
            error_json["error"] = "internal";
            error_json["details"] = "Database connection failed: " + conn_error;
            res.status = 503;
            res.set_content(error_json.dump(), "application/json");
            return;
        }
        // 生成控制密钥（32 字节随机值），用于 HMAC 等控制消息签名
        const int CONTROL_SECRET_LEN = 32;
        std::string control_secret_bin;
        control_secret_bin.resize(CONTROL_SECRET_LEN);
        for (int i = 0; i < CONTROL_SECRET_LEN; ++i) {
            control_secret_bin[i] = static_cast<char>(std::rand() & 0xFF);
        }
        
        // 将二进制数据转换为十六进制字符串（用于 decode() 函数）
        std::string control_secret_hex;
        const char hex_chars[] = "0123456789abcdef";
        for (int i = 0; i < CONTROL_SECRET_LEN; ++i) {
            unsigned char byte = static_cast<unsigned char>(control_secret_bin[i]);
            control_secret_hex += hex_chars[(byte >> 4) & 0x0F];
            control_secret_hex += hex_chars[byte & 0x0F];
        }

        std::cout << "[Backend][POST /api/v1/vins/" << vin << "/sessions] 创建会话 controller=" << controller_user_id << std::endl;
        
        const char* params1[3];
        int lens1[3];
        params1[0] = vin.c_str();
        lens1[0] = static_cast<int>(vin.size());
        params1[1] = controller_user_id.c_str();
        lens1[1] = static_cast<int>(controller_user_id.size());
        params1[2] = control_secret_hex.c_str();
        lens1[2] = static_cast<int>(control_secret_hex.size());
        PGresult* res_session = PQexecParams(
            conn,
            "INSERT INTO sessions (vin, controller_user_id, state, started_at, last_heartbeat_at, control_secret, control_seq_start) "
            "VALUES ($1, $2::uuid, 'ACTIVE', now(), now(), decode($3, 'hex'), 1) "
            "RETURNING session_id::text",
            3, nullptr, params1, lens1, nullptr, 0);
        if (!res_session || PQresultStatus(res_session) != PGRES_TUPLES_OK || PQntuples(res_session) != 1) {
            std::string error_msg = "INSERT sessions failed: ";
            if (res_session) {
                error_msg += "Status=" + std::to_string(PQresultStatus(res_session));
                error_msg += ", Error=" + std::string(PQerrorMessage(conn));
                PQclear(res_session);
            } else {
                error_msg += "res_session is null, Error=" + std::string(PQerrorMessage(conn));
            }
            std::cout << "[Backend][POST sessions] 503 reason=insert_sessions_failed vin=" << vin << " " << error_msg << std::endl;
            PQfinish(conn);
            res.status = 503;
            nlohmann::json error_json = nlohmann::json::object();
            error_json["error"] = "internal";
            error_json["details"] = error_msg;  // 临时添加详细信息用于调试
            res.set_content(error_json.dump(), "application/json");
            return;
        }
        const char* session_id_c = PQgetvalue(res_session, 0, 0);
        if (!session_id_c) {
            std::cout << "[Backend][POST sessions] 503 reason=session_id_null vin=" << vin << std::endl;
            PQclear(res_session);
            PQfinish(conn);
            res.status = 503;
            res.set_content("{\"error\":\"internal\"}", "application/json");
            return;
        }
        std::string session_id(session_id_c);
        PQclear(res_session);
        // 插入 session_participants 记录
        const char* params2[3];
        int lens2[3];
        params2[0] = session_id.c_str();
        lens2[0] = static_cast<int>(session_id.size());
        params2[1] = controller_user_id.c_str();
        lens2[1] = static_cast<int>(controller_user_id.size());
        const std::string role = "controller";
        params2[2] = role.c_str();
        lens2[2] = static_cast<int>(role.size());
        PGresult* res_part = PQexecParams(
            conn,
            "INSERT INTO session_participants (session_id, user_id, role_in_session) "
            "VALUES ($1::uuid, $2::uuid, $3)",
            3, nullptr, params2, lens2, nullptr, 0);
        if (!res_part || PQresultStatus(res_part) != PGRES_COMMAND_OK) {
            std::cout << "[Backend][POST sessions] 503 reason=insert_session_participants_failed vin=" << vin
                      << " status=" << (res_part ? PQresultStatus(res_part) : -1)
                      << " pg_error=" << (conn ? PQerrorMessage(conn) : "") << std::endl;
            if (res_part) PQclear(res_part);
            PQfinish(conn);
            res.status = 503;
            res.set_content("{\"error\":\"internal\"}", "application/json");
            return;
        }
        PQclear(res_part);
        PQfinish(conn);

        // 生成 WHIP/WHEP 媒体 URL
        std::string zlm_api_url = get_env("ZLM_API_URL", "http://zlmediakit/index/api");
        std::string whip_url = build_whip_url(zlm_api_url, vin, session_id);
        std::string whep_url = build_whep_url(zlm_api_url, vin, session_id);

        // 控制通道参数（控制密钥在客户端侧使用 base64url 编码；此处先不返回原始密钥）
        // 为避免在日志/抓包中直接暴露二进制密钥，这里暂不在响应中携带 control_secret，
        // 后续可以通过受限的内部 API 由控制网关/车端拉取。

        nlohmann::json out = nlohmann::json::object();
        out["sessionId"] = session_id;
        out["vin"] = vin; // 与路径 /api/v1/vins/{vin}/sessions 一致，供客户端校验响应与选车
        nlohmann::json media = nlohmann::json::object();
        media["whip"] = whip_url;
        media["whep"] = whep_url;
        out["media"] = media;
        // 预留控制通道配置；mqtt_broker_url 供客户端按 VIN/会话连接车端 MQTT
        nlohmann::json control = nlohmann::json::object();
        control["algo"] = "HMAC-SHA256";
        control["seqStart"] = 1;
        control["tsWindowMs"] = 2000;
        std::string mqtt_broker = get_env("MQTT_BROKER_URL", "");
        if (!mqtt_broker.empty()) {
            control["mqtt_broker_url"] = mqtt_broker;
            std::string mqtt_client_id = get_env("MQTT_CLIENT_ID", "");
            if (!mqtt_client_id.empty())
                control["mqtt_client_id"] = mqtt_client_id;
        }
        out["control"] = control;

        res.status = 201;
        res.set_content(out.dump(), "application/json");
        std::cout << "[Backend][POST sessions] 会话已创建 vin=" << vin << " session_id=" << session_id << std::endl;
    });

    // GET /api/v1/sessions/{sessionId} - 需 JWT，只读返回会话与锁状态
    svr.Get("/api/v1/sessions/[^/]+", [&expected_issuers, &expected_aud](const httplib::Request& req, httplib::Response& res) {
        std::string auth = req.get_header_value("Authorization");
        if (auth.empty() || auth.find("Bearer ") != 0) {
            res.status = 401;
            res.set_content("{\"error\":\"unauthorized\"}", "application/json");
            return;
        }
        std::string token = auth.substr(7);
        auto payload_opt = teleop::auth::validate_jwt_claims(token, expected_issuers, expected_aud);
        if (!payload_opt) {
            res.status = 401;
            res.set_content("{\"error\":\"unauthorized\"}", "application/json");
            return;
        }

        // 提取 sessionId：/api/v1/sessions/{sessionId}
        std::string path = req.path;
        size_t pos = path.find("/sessions/");
        if (pos == std::string::npos) {
            res.status = 400;
            res.set_content("{\"error\":\"invalid_path\"}", "application/json");
            return;
        }
        pos += 10; // "/sessions/".length()
        std::string session_id = path.substr(pos);

        std::string database_url = get_env("DATABASE_URL", "");
        if (database_url.empty()) {
            res.status = 503;
            res.set_content("{\"error\":\"internal\"}", "application/json");
            return;
        }
        PGconn* conn = PQconnectdb(database_url.c_str());
        if (!conn || PQstatus(conn) != CONNECTION_OK) {
            if (conn) PQfinish(conn);
            res.status = 503;
            res.set_content("{\"error\":\"internal\"}", "application/json");
            return;
        }
        const char* params[1];
        int lens[1];
        params[0] = session_id.c_str();
        lens[0] = static_cast<int>(session_id.size());
        PGresult* rs = PQexecParams(
            conn,
            "SELECT vin::text, state, "
            "       (lock_owner_user_id IS NOT NULL "
            "        AND lock_expires_at IS NOT NULL "
            "        AND lock_expires_at > now()) AS locked, "
            "       lock_expires_at "
            "FROM sessions WHERE session_id = $1::uuid",
            1, nullptr, params, lens, nullptr, 0);
        if (!rs || PQresultStatus(rs) != PGRES_TUPLES_OK) {
            if (rs) PQclear(rs);
            PQfinish(conn);
            res.status = 503;
            res.set_content("{\"error\":\"internal\"}", "application/json");
            return;
        }
        int n = PQntuples(rs);
        if (n == 0) {
            PQclear(rs);
            PQfinish(conn);
            res.status = 404;
            res.set_content("{\"error\":\"session_not_found\"}", "application/json");
            return;
        }
        const char* vin_c = PQgetvalue(rs, 0, 0);
        const char* state_c = PQgetvalue(rs, 0, 1);
        const char* locked_c = PQgetvalue(rs, 0, 2);
        const char* lock_expires_at_c = PQgetvalue(rs, 0, 3);

        bool locked = (locked_c && locked_c[0] == 't');

        nlohmann::json out = nlohmann::json::object();
        out["sessionId"] = session_id;
        out["vin"] = vin_c ? std::string(vin_c) : "";
        out["state"] = state_c ? std::string(state_c) : "";
        out["locked"] = locked;
        if (lock_expires_at_c && lock_expires_at_c[0] != '\0') {
            out["lockExpiresAt"] = lock_expires_at_c;
        }

        PQclear(rs);
        PQfinish(conn);

        res.status = 200;
        res.set_content(out.dump(), "application/json");
    });

    // GET /api/v1/sessions/{sessionId}/streams - 需 JWT，基于 DB 中的 vin+sessionId 生成 WHEP 播放地址
    svr.Get("/api/v1/sessions/.*/streams", [&expected_issuers, &expected_aud](const httplib::Request& req, httplib::Response& res) {
        std::string auth = req.get_header_value("Authorization");
        if (auth.empty() || auth.find("Bearer ") != 0) {
            res.status = 401;
            res.set_content("{\"error\":\"unauthorized\"}", "application/json");
            return;
        }
        std::string token = auth.substr(7);
        auto payload_opt = teleop::auth::validate_jwt_claims(token, expected_issuers, expected_aud);
        if (!payload_opt) {
            res.status = 401;
            res.set_content("{\"error\":\"unauthorized\"}", "application/json");
            return;
        }
        // 提取路径中的 sessionId：/api/v1/sessions/{sessionId}/streams
        std::string path = req.path;
        size_t session_start = path.find("/sessions/");
        if (session_start == std::string::npos) {
            res.status = 400;
            res.set_content("{\"error\":\"invalid_path\"}", "application/json");
            return;
        }
        session_start += 10; // "/sessions/".length()
        size_t session_end = path.find("/streams", session_start);
        if (session_end == std::string::npos) {
            res.status = 400;
            res.set_content("{\"error\":\"invalid_path\"}", "application/json");
            return;
        }
        std::string session_id = path.substr(session_start, session_end - session_start);

        // 从 DB 中根据 sessionId 查出 vin
        std::string database_url = get_env("DATABASE_URL", "");
        if (database_url.empty()) {
            res.status = 503;
            res.set_content("{\"error\":\"internal\"}", "application/json");
            return;
        }
        PGconn* conn = PQconnectdb(database_url.c_str());
        if (!conn || PQstatus(conn) != CONNECTION_OK) {
            if (conn) PQfinish(conn);
            res.status = 503;
            res.set_content("{\"error\":\"internal\"}", "application/json");
            return;
        }
        const char* params[1];
        int lens[1];
        params[0] = session_id.c_str();
        lens[0] = static_cast<int>(session_id.size());
        PGresult* res_sess = PQexecParams(
            conn,
            "SELECT vin::text FROM sessions WHERE session_id = $1::uuid",
            1, nullptr, params, lens, nullptr, 0);
        if (!res_sess || PQresultStatus(res_sess) != PGRES_TUPLES_OK) {
            if (res_sess) PQclear(res_sess);
            PQfinish(conn);
            res.status = 503;
            res.set_content("{\"error\":\"internal\"}", "application/json");
            return;
        }
        int n = PQntuples(res_sess);
        if (n == 0) {
            PQclear(res_sess);
            PQfinish(conn);
            res.status = 404;
            res.set_content("{\"error\":\"session_not_found\"}", "application/json");
            return;
        }
        const char* vin_c = PQgetvalue(res_sess, 0, 0);
        if (!vin_c) {
            PQclear(res_sess);
            PQfinish(conn);
            res.status = 503;
            res.set_content("{\"error\":\"internal\"}", "application/json");
            return;
        }
        std::string vin(vin_c);
        PQclear(res_sess);
        PQfinish(conn);

        // 生成 WHEP 播放地址
        std::string zlm_api_url = get_env("ZLM_API_URL", "http://zlmediakit/index/api");
        std::string stream_url = build_whep_url(zlm_api_url, vin, session_id);
        nlohmann::json out = nlohmann::json::object();
        nlohmann::json webrtc_obj = nlohmann::json::object();
        webrtc_obj["play"] = stream_url;
        out["webrtc"] = webrtc_obj;
        res.status = 200;
        res.set_content(out.dump(), "application/json");
    });

    // GET /internal/sessions/{sessionId}/control - 仅供内部控制网关/车端获取控制密钥等参数
    svr.Get("/internal/sessions/[^/]+/control", [](const httplib::Request& req, httplib::Response& res) {
        std::string database_url = get_env("DATABASE_URL", "");
        if (database_url.empty()) {
            res.status = 503;
            res.set_content("{\"error\":\"internal\"}", "application/json");
            return;
        }

        // 简单的内部访问控制：可选 X-Internal-Token 头，与环境变量 INTERNAL_CONTROL_API_TOKEN 匹配
        std::string required_token = get_env("INTERNAL_CONTROL_API_TOKEN", "");
        if (!required_token.empty()) {
            std::string header_token = req.get_header_value("X-Internal-Token");
            if (header_token != required_token) {
                res.status = 403;
                res.set_content("{\"error\":\"forbidden\"}", "application/json");
                return;
            }
        }

        // 提取 sessionId：/internal/sessions/{sessionId}/control
        std::string path = req.path;
        size_t pos = path.find("/internal/sessions/");
        if (pos == std::string::npos) {
            res.status = 400;
            res.set_content("{\"error\":\"invalid_path\"}", "application/json");
            return;
        }
        pos += std::string("/internal/sessions/").size();
        size_t end = path.find("/control", pos);
        if (end == std::string::npos) {
            res.status = 400;
            res.set_content("{\"error\":\"invalid_path\"}", "application/json");
            return;
        }
        std::string session_id = path.substr(pos, end - pos);

        PGconn* conn = PQconnectdb(database_url.c_str());
        if (!conn || PQstatus(conn) != CONNECTION_OK) {
            if (conn) PQfinish(conn);
            res.status = 503;
            res.set_content("{\"error\":\"internal\"}", "application/json");
            return;
        }

        const char* params[1];
        int lens[1];
        params[0] = session_id.c_str();
        lens[0] = static_cast<int>(session_id.size());

        // 使用 encode(control_secret, 'base64') 将密钥以 base64 文本返回给网关
        PGresult* rs = PQexecParams(
            conn,
            "SELECT vin::text, controller_user_id::text, control_seq_start, "
            "       encode(control_secret, 'base64') AS control_secret_b64 "
            "FROM sessions WHERE session_id = $1::uuid",
            1, nullptr, params, lens, nullptr, 0);

        if (!rs || PQresultStatus(rs) != PGRES_TUPLES_OK) {
            if (rs) PQclear(rs);
            PQfinish(conn);
            res.status = 503;
            res.set_content("{\"error\":\"internal\"}", "application/json");
            return;
        }

        int n = PQntuples(rs);
        if (n == 0) {
            PQclear(rs);
            PQfinish(conn);
            res.status = 404;
            res.set_content("{\"error\":\"session_not_found\"}", "application/json");
            return;
        }

        const char* vin_c = PQgetvalue(rs, 0, 0);
        const char* controller_user_id_c = PQgetvalue(rs, 0, 1);
        const char* seq_start_c = PQgetvalue(rs, 0, 2);
        const char* secret_b64_c = PQgetvalue(rs, 0, 3);

        nlohmann::json out = nlohmann::json::object();
        out["sessionId"] = session_id;
        out["vin"] = vin_c ? std::string(vin_c) : "";
        out["controllerUserId"] = controller_user_id_c ? std::string(controller_user_id_c) : "";

        nlohmann::json control = nlohmann::json::object();
        control["algo"] = "HMAC-SHA256";
        // std::stoll 可抛 std::invalid_argument / std::out_of_range；捕获后回退到 1
        try {
            control["seqStart"] = seq_start_c ? std::stoll(seq_start_c) : 1;
        } catch (const std::exception& e) {
            std::cerr << "[Backend][Session] seqStart parse error seq=" << (seq_start_c ? seq_start_c : "(null)") << " err=" << e.what() << std::endl;
            control["seqStart"] = 1;
        }
        control["tsWindowMs"] = 2000;
        if (secret_b64_c && secret_b64_c[0] != '\0') {
            control["secretB64"] = secret_b64_c;
        }
        out["control"] = control;

        PQclear(rs);
        PQfinish(conn);

        res.status = 200;
        res.set_content(out.dump(), "application/json");
    });

    // POST /api/v1/sessions/{sessionId}/lock - 需 JWT，单持有者锁 + TTL（持久化到 sessions）
    svr.Post("/api/v1/sessions/.*/lock", [&expected_issuers, &expected_aud, session_ttl_seconds, lock_ttl_seconds](const httplib::Request& req, httplib::Response& res) {
        std::string auth = req.get_header_value("Authorization");
        if (auth.empty() || auth.find("Bearer ") != 0) {
            res.status = 401;
            res.set_content("{\"error\":\"unauthorized\"}", "application/json");
            return;
        }
        std::string token = auth.substr(7);
        auto payload_opt = teleop::auth::validate_jwt_claims(token, expected_issuers, expected_aud);
        if (!payload_opt) {
            res.status = 401;
            res.set_content("{\"error\":\"unauthorized\"}", "application/json");
            return;
        }
        std::string sub;
        try {
            auto j = nlohmann::json::parse(*payload_opt);
            sub = j.value("sub", "");
        } catch (...) {
            res.status = 401;
            res.set_content("{\"error\":\"unauthorized\"}", "application/json");
            return;
        }
        // 提取路径中的 sessionId：/api/v1/sessions/{sessionId}/lock
        std::string path = req.path;
        size_t session_start = path.find("/sessions/");
        if (session_start == std::string::npos) {
            res.status = 400;
            res.set_content("{\"error\":\"invalid_path\"}", "application/json");
            return;
        }
        session_start += 10; // "/sessions/".length()
        size_t session_end = path.find("/lock", session_start);
        if (session_end == std::string::npos) {
            res.status = 400;
            res.set_content("{\"error\":\"invalid_path\"}", "application/json");
            return;
        }
        std::string session_id = path.substr(session_start, session_end - session_start);

        std::string database_url = get_env("DATABASE_URL", "");
        if (database_url.empty()) {
            res.status = 503;
            res.set_content("{\"error\":\"internal\"}", "application/json");
            return;
        }
        // 获取当前 user_id
        std::string current_user_id;
        auto vins_opt = get_vins_for_sub(database_url, sub, &current_user_id);
        if (!vins_opt || current_user_id.empty()) {
            res.status = 503;
            res.set_content("{\"error\":\"internal\"}", "application/json");
            return;
        }
        PGconn* conn = PQconnectdb(database_url.c_str());
        if (!conn || PQstatus(conn) != CONNECTION_OK) {
            if (conn) PQfinish(conn);
            res.status = 503;
            res.set_content("{\"error\":\"internal\"}", "application/json");
            return;
        }
        // 先检查 session 是否存在及其状态 / TTL / 锁冲突
        const char* params_sel[3];
        int lens_sel[3];
        std::string session_ttl_str = std::to_string(session_ttl_seconds);
        params_sel[0] = session_id.c_str();
        lens_sel[0] = static_cast<int>(session_id.size());
        params_sel[1] = session_ttl_str.c_str();
        lens_sel[1] = static_cast<int>(session_ttl_str.size());
        params_sel[2] = current_user_id.c_str();
        lens_sel[2] = static_cast<int>(current_user_id.size());
        PGresult* res_sel = PQexecParams(
            conn,
            "SELECT state, ended_at, "
            "       (started_at + make_interval(secs := $2::int) <= now()) AS session_expired, "
            "       (lock_owner_user_id IS NOT NULL "
            "        AND lock_owner_user_id <> $3::uuid "
            "        AND lock_expires_at IS NOT NULL "
            "        AND lock_expires_at > now()) AS lock_conflict "
            "FROM sessions WHERE session_id = $1::uuid",
            3, nullptr, params_sel, lens_sel, nullptr, 0);
        if (!res_sel || PQresultStatus(res_sel) != PGRES_TUPLES_OK) {
            if (res_sel) PQclear(res_sel);
            PQfinish(conn);
            res.status = 503;
            res.set_content("{\"error\":\"internal\"}", "application/json");
            return;
        }
        int n = PQntuples(res_sel);
        if (n == 0) {
            PQclear(res_sel);
            PQfinish(conn);
            res.status = 404;
            res.set_content("{\"error\":\"session_not_found\"}", "application/json");
            return;
        }
        const char* state = PQgetvalue(res_sel, 0, 0);
        const char* ended_at = PQgetvalue(res_sel, 0, 1);
        const char* session_expired = PQgetvalue(res_sel, 0, 2);
        const char* lock_conflict = PQgetvalue(res_sel, 0, 3);
        bool is_session_expired = (session_expired && session_expired[0] == 't');
        bool has_lock_conflict = (lock_conflict && lock_conflict[0] == 't');
        bool has_ended = (ended_at && ended_at[0] != '\0');
        std::string state_str = state ? std::string(state) : std::string();
        if (state_str != "REQUESTED" && state_str != "ACTIVE") {
            PQclear(res_sel);
            PQfinish(conn);
            res.status = 409;
            res.set_content("{\"error\":\"session_invalid_state\"}", "application/json");
            return;
        }
        if (has_ended || is_session_expired) {
            PQclear(res_sel);
            PQfinish(conn);
            res.status = 409;
            res.set_content("{\"error\":\"session_ended_or_expired\"}", "application/json");
            return;
        }
        if (has_lock_conflict) {
            PQclear(res_sel);
            PQfinish(conn);
            res.status = 409;
            res.set_content("{\"error\":\"lock_conflict\"}", "application/json");
            return;
        }
        PQclear(res_sel);

        // 尝试获取/续约锁
        const char* params_upd[3];
        int lens_upd[3];
        std::string lock_ttl_str = std::to_string(lock_ttl_seconds);
        params_upd[0] = session_id.c_str();
        lens_upd[0] = static_cast<int>(session_id.size());
        params_upd[1] = current_user_id.c_str();
        lens_upd[1] = static_cast<int>(current_user_id.size());
        params_upd[2] = lock_ttl_str.c_str();
        lens_upd[2] = static_cast<int>(lock_ttl_str.size());
        PGresult* res_upd = PQexecParams(
            conn,
            "UPDATE sessions "
            "SET lock_id = uuid_generate_v4(), "
            "    lock_owner_user_id = $2::uuid, "
            "    lock_expires_at = now() + make_interval(secs := $3::int), "
            "    state = 'ACTIVE' "
            "WHERE session_id = $1::uuid "
            "RETURNING lock_id::text",
            3, nullptr, params_upd, lens_upd, nullptr, 0);
        if (!res_upd || PQresultStatus(res_upd) != PGRES_TUPLES_OK || PQntuples(res_upd) != 1) {
            if (res_upd) PQclear(res_upd);
            PQfinish(conn);
            res.status = 503;
            res.set_content("{\"error\":\"internal\"}", "application/json");
            return;
        }
        const char* lock_id_c = PQgetvalue(res_upd, 0, 0);
        if (!lock_id_c) {
            PQclear(res_upd);
            PQfinish(conn);
            res.status = 503;
            res.set_content("{\"error\":\"internal\"}", "application/json");
            return;
        }
        std::string lock_id(lock_id_c);
        PQclear(res_upd);
        PQfinish(conn);

        nlohmann::json out = nlohmann::json::object();
        out["locked"] = true;
        out["lockId"] = lock_id;
        res.status = 200;
        res.set_content(out.dump(), "application/json");
    });

    // GET /admin/add-vehicle - 提供“增加车辆”管理页（静态 HTML）
    svr.Get("/admin/add-vehicle", [](const httplib::Request&, httplib::Response& res) {
        std::string static_dir = get_env("STATIC_DIR", "/app/static");
        std::string path = static_dir + "/add-vehicle.html";
        std::cout << "[Backend][GET /admin/add-vehicle] 收到请求 path=" << path << " STATIC_DIR=" << static_dir << std::endl;
        errno = 0;
        std::ifstream f(path);
        if (!f.good()) {
            int err = errno;
            std::cout << "[Backend][GET /admin/add-vehicle] 404 无法打开文件 path=" << path << " errno=" << err
                      << " (" << (err ? std::strerror(err) : "unknown") << ") 请检查镜像是否 COPY static 或 compose 是否挂载 backend/static" << std::endl;
            res.status = 404;
            res.set_header("Content-Type", "text/html; charset=utf-8");
            res.set_content(
                "<!DOCTYPE html><html><head><meta charset=\"utf-8\"><title>未找到管理页</title></head><body>"
                "<h2>未找到增加车辆管理页</h2><p>静态文件 add-vehicle.html 不存在（path=" + path + "）。</p>"
                "<p><b>处理：</b>请重新构建 backend 镜像（确保 Dockerfile 含 COPY static /app/static）并重启：<br>"
                "<code>docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml build backend && docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml up -d backend</code></p>"
                "<p>详见项目 docs/ADD_VEHICLE_GUIDE.md §8.4</p></body></html>",
                "text/html; charset=utf-8");
            return;
        }
        std::string html((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        f.close();
        res.set_header("Content-Type", "text/html; charset=utf-8");
        res.status = 200;
        res.set_content(html, "text/html; charset=utf-8");
        std::cout << "[Backend][GET /admin/add-vehicle] 200 已返回 HTML size=" << html.size() << std::endl;
    });

    // GET /admin/get-token - 提供“获取 JWT Token”页（网页上填账号密码即可拿 token，用于粘贴到增加车辆页）
    svr.Get("/admin/get-token", [](const httplib::Request&, httplib::Response& res) {
        std::string static_dir = get_env("STATIC_DIR", "/app/static");
        std::string path = static_dir + "/get-token.html";
        errno = 0;
        std::ifstream f(path);
        if (!f.good()) {
            res.status = 404;
            res.set_header("Content-Type", "text/html; charset=utf-8");
            res.set_content(
                "<!DOCTYPE html><html><head><meta charset=\"utf-8\"><title>未找到</title></head><body>"
                "<h2>未找到 get-token.html</h2><p>path=" + path + "</p></body></html>",
                "text/html; charset=utf-8");
            return;
        }
        std::string html((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        f.close();
        res.set_header("Content-Type", "text/html; charset=utf-8");
        res.status = 200;
        res.set_content(html, "text/html; charset=utf-8");
    });

    // POST /admin/proxy-token - 代理请求 Keycloak 获取 token（避免前端跨域），body: {"username","password","client_secret"(可选)}
    svr.Post("/admin/proxy-token", [](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Content-Type", "application/json; charset=utf-8");
        std::string kc_url = get_env("KEYCLOAK_URL", "http://keycloak:8080");
        std::string body = req.body;
        nlohmann::json in;
        try {
            in = nlohmann::json::parse(body.empty() ? "{}" : body);
        } catch (...) {
            res.status = 400;
            res.set_content("{\"error\":\"invalid_json\"}", "application/json");
            return;
        }
        std::string username = in.value("username", "");
        std::string password = in.value("password", "");
        std::string client_secret = in.value("client_secret", "change-me-in-production");
        if (client_secret.empty()) client_secret = "change-me-in-production";
        if (username.empty()) {
            res.status = 400;
            res.set_content("{\"error\":\"username_required\"}", "application/json");
            return;
        }
        // 解析 KEYCLOAK_URL 得到 host 和 port
        std::string host = "keycloak";
        int port = 8080;
        if (kc_url.size() > 8 && kc_url.substr(0, 7) == "http://") {
            std::string rest = kc_url.substr(7);
            size_t colon = rest.find(':');
            size_t slash = rest.find('/');
            if (colon != std::string::npos && (slash == std::string::npos || colon < slash)) {
                host = rest.substr(0, colon);
                port = std::atoi(rest.substr(colon + 1, slash != std::string::npos ? slash - colon - 1 : std::string::npos).c_str());
                if (port <= 0) port = 8080;
            } else if (slash != std::string::npos) {
                host = rest.substr(0, slash);
            } else {
                host = rest;
            }
        }
        std::string form = "client_id=teleop-backend&client_secret=" + client_secret + "&username=" + username + "&password=" + password + "&grant_type=password";
        httplib::Client cli(host, port);
        cli.set_connection_timeout(5, 0);
        cli.set_read_timeout(10, 0);
        auto r = cli.Post("/realms/teleop/protocol/openid-connect/token", form, "application/x-www-form-urlencoded");
        if (!r) {
            res.status = 502;
            res.set_content("{\"error\":\"keycloak_unreachable\",\"error_description\":\"backend could not reach Keycloak\"}", "application/json");
            return;
        }
        // 兼容旧 realm 导入后 client secret 为 -change-me-in-production 的情况：首次用 change-me-in-production 若返回 unauthorized_client 则用 -change-me-in-production 重试一次
        // Keycloak 对 Invalid client credentials 可能返回 400 或 401，均需触发重试
        if ((r->status == 400 || r->status == 401) && r->body.find("unauthorized_client") != std::string::npos && client_secret == "change-me-in-production") {
            std::cout << "[Backend][proxy-token] 首次 client_secret 失败 status=" << r->status << "，重试 -change-me-in-production" << std::endl;
            std::string form2 = "client_id=teleop-backend&client_secret=-change-me-in-production&username=" + username + "&password=" + password + "&grant_type=password";
            auto r2 = cli.Post("/realms/teleop/protocol/openid-connect/token", form2, "application/x-www-form-urlencoded");
            if (r2) {
                res.status = r2->status;
                res.set_content(r2->body, "application/json");
                return;
            }
        }
        res.status = r->status;
        res.set_content(r->body, "application/json");
    });

    // ZLMediaKit 鉴权钩子：播放/推流时 ZLM 会 POST 到此，需返回 code=0 放行，否则 ZLM 返回 -400 (connection refused 等)
    svr.Post("/api/v1/hooks/on_play", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("{\"code\":0,\"msg\":\"success\"}", "application/json");
        res.status = 200;
    });
    svr.Post("/api/v1/hooks/on_publish", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("{\"code\":0,\"msg\":\"success\"}", "application/json");
        res.status = 200;
    });

    // 未匹配路由或 4xx/5xx 时记录日志，便于验证时依据日志判断（如 POST /api/v1/vehicles 返回 404 说明当前进程未注册该路由，需重新构建并重启）
    svr.set_error_handler([](const httplib::Request& req, httplib::Response& res) {
        std::cout << "[Backend][未匹配] method=" << req.method << " path=" << req.path << " status=" << res.status
                  << " (无对应路由；若 path 为 /api/v1/vehicles 请确认已重新构建 backend 并重启)" << std::endl;
        return httplib::Server::HandlerResponse::Unhandled;
    });

    // ── 全局异常处理：捕获所有未处理的 std::exception 并记录 traceback ─────────────
    svr.set_exception_handler([](const httplib::Request& req, httplib::Response& res, std::exception_ptr ep) {
        std::cerr << "[Backend][FATAL] 未处理异常 method=" << req.method << " path=" << req.path
                  << " — 正在尝试恢复响应..." << std::endl;
        try {
            if (ep) std::rethrow_exception(ep);
        } catch (const std::exception& e) {
            std::cerr << "[Backend][FATAL] exception: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "[Backend][FATAL] unknown exception" << std::endl;
        }
        res.status = 500;
        res.set_content("{\"error\":\"internal_server_error\",\"code\":\"E_UNHANDLED\"}", "application/json");
    });

    std::cout << "[Backend][启动] 路由已注册: GET /health, /ready, /api/v1/me, /api/v1/vins, "
              << "POST /api/v1/vehicles, DELETE /api/v1/vehicles/{vin}, GET /admin/add-vehicle, GET /admin/get-token, POST /admin/proxy-token, "
              << "POST /api/v1/vins/{vin}/sessions, GET /api/v1/sessions/{sessionId}/streams, "
              << "POST /api/v1/sessions/{sessionId}/lock, hooks on_play/on_publish\n";
    {
        std::string static_dir = get_env("STATIC_DIR", "/app/static");
        std::string check_path = static_dir + "/add-vehicle.html";
        std::ifstream check(check_path);
        if (check.good()) {
            std::cout << "[Backend][启动] add-vehicle.html 可读=是 path=" << check_path << std::endl;
        } else {
            std::cout << "[Backend][启动] add-vehicle.html 可读=否 path=" << check_path << " errno=" << errno
                      << " (" << std::strerror(errno) << ") 请挂载 backend/static 或重建镜像含 COPY static" << std::endl;
        }
    }
    std::cout << "Teleop Backend listening on 0.0.0.0:" << port
              << " (GET /health, /ready, /api/v1/me, /api/v1/vins, "
              << "POST /api/v1/vehicles, DELETE /api/v1/vehicles/{vin}, GET /admin/add-vehicle, GET /admin/get-token, POST /admin/proxy-token, "
              << "POST /api/v1/vins/{vin}/sessions, "
              << "GET /api/v1/sessions/{sessionId}/streams, "
              << "POST /api/v1/sessions/{sessionId}/lock, "
              << "GET /admin/get-token, POST /admin/proxy-token, hooks: on_play, on_publish)\n";
    try {
        if (!svr.listen("0.0.0.0", static_cast<int>(port))) {
            std::cerr << "[Backend][FATAL] Failed to bind to port " << port << "\n";
            return 1;
        }
    } catch (const std::exception& e) {
        std::cerr << "[Backend][FATAL] svr.listen 异常: " << e.what() << "\n";
        return 1;
    } catch (...) {
        std::cerr << "[Backend][FATAL] svr.listen 未知异常\n";
        return 1;
    }
    return 0;
}
// test
// Test comment for auto-rebuild
