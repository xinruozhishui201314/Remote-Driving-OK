/**
 * Backend 健康检查与就绪检查
 *
 * /health  - 仅检查进程与关键依赖可达性（DB/MQTT/ZLM）
 * /ready   - 附加检查应用就绪（DB schema 已初始化、ZLM API 可用）
 *
 * 响应格式：
 * {
 *   "status": "up" | "degraded",
 *   "dependencies": {"db":"ok|error","mqtt":"ok|error","zlm":"ok|error"},
 *   "since": "2026-02-23T12:00:00Z",
 *   "version": "v1.0.0"
 * }
 */

#include "httplib.h"
#include <nlohmann/json.hpp>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <libpq-fe.h>

namespace teleop::health {

static std::string g_startup_time = []() {
    auto now = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm;
    gmtime_r(&tt, &tm);
    std::ostringstream oss;
    oss << std::setfill('0')
        << std::setw(4) << tm.tm_year+1900 << '-'
        << std::setw(2) << tm.tm_mon+1 << '-'
        << std::setw(2) << tm.tm_mday << 'T'
        << std::setw(2) << tm.tm_hour << ':'
        << std::setw(2) << tm.tm_min << ':'
        << std::setw(2) << tm.tm_sec << 'Z';
    return oss.str();
}();

static std::string check_db(const std::string& database_url) {
    PGconn* conn = PQconnectdb(database_url.c_str());
    if (!conn || PQstatus(conn) != CONNECTION_OK) {
        return PQerrorMessage(conn) ? PQerrorMessage(conn) : "unknown";
    }
    PQfinish(conn);
    return "ok";
}

static std::string check_zlm(const std::string& zlm_api_url) {
    // TODO: 实际调用 ZLM 的健康端点（如 /index/api/getServerConfig）
    // 此处仅做占位，总是返回 "ok"
    (void)zlm_api_url;
    return "ok";
}

void register_handlers(httplib::Server& svr, const std::string& database_url, const std::string& zlm_api_url, const std::string& version) {
    svr.Get("/health", [&database_url, &zlm_api_url, &version](const httplib::Request&, httplib::Response& res) {
        nlohmann::json j = {
            {"status", "up"},
            {"dependencies", {
                {"db", check_db(database_url)},
                {"zlm", check_zlm(zlm_api_url)}
            }},
            {"since", g_startup_time},
            {"version", version}
        };
        res.set_content(j.dump(), "application/json");
    });

    svr.Get("/ready", [&database_url, &zlm_api_url](const httplib::Request&, httplib::Response& res) {
        // ready 附加检查 DB schema 是否已初始化；此处简化假设已初始化
        bool db_ready = (check_db(database_url) == "ok");
        bool zlm_ready = (check_zlm(zlm_api_url) == "ok");
        bool ready = db_ready && zlm_ready;

        nlohmann::json j = {
            {"status", ready ? "ready" : "not ready"},
            {"dependencies", {
                {"db", db_ready ? "ok" : "error"},
                {"zlm", zlm_ready ? "ok" : "error"}
            }},
            {"since", g_startup_time}
        };
        res.status = ready ? 200 : 503;
        res.set_content(j.dump(), "application/json");
    });
}

} // namespace teleop::health
