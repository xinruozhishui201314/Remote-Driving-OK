/**
 * Database Repository 实现
 */

#include "db/repository.h"
#include <iostream>
#include <algorithm>

namespace teleop::db {

// ========== Connection 实现 ==========

Connection::Connection(PGconn* conn) 
    : conn_(conn), owns_connection_(true) {
}

Connection::~Connection() {
    if (conn_ && owns_connection_) {
        PQfinish(conn_);
    }
}

Connection::Connection(Connection&& other) noexcept
    : conn_(other.conn_), owns_connection_(other.owns_connection_) {
    other.conn_ = nullptr;
    other.owns_connection_ = false;
}

Connection& Connection::operator=(Connection&& other) noexcept {
    if (this != &other) {
        if (conn_ && owns_connection_) {
            PQfinish(conn_);
        }
        conn_ = other.conn_;
        owns_connection_ = other.owns_connection_;
        other.conn_ = nullptr;
        other.owns_connection_ = false;
    }
    return *this;
}

// ========== Transaction 实现 ==========

Transaction::Transaction(Connection& conn, RollbackPolicy policy)
    : conn_(conn), committed_(false), active_(true), policy_(policy) {
    const char* query = "BEGIN";
    PGresult* res = PQexec(conn_.get(), query);
    if (!res || PQresultStatus(res) != PGRES_COMMAND_OK) {
        std::string error = conn_.is_valid() ? PQerrorMessage(conn_.get()) : "Connection invalid";
        if (res) PQclear(res);
        throw std::runtime_error("Failed to begin transaction: " + error);
    }
    PQclear(res);
}

Transaction::~Transaction() {
    if (!active_) return;
    
    if (committed_) {
        // 已手动提交
        return;
    }
    
    if (policy_ == RollbackPolicy::ALWAYS_ROLLBACK) {
        rollback();
    }
}

void Transaction::commit() {
    if (!active_ || committed_) return;
    
    const char* query = "COMMIT";
    PGresult* res = PQexec(conn_.get(), query);
    if (!res || PQresultStatus(res) != PGRES_COMMAND_OK) {
        std::string error = conn_.is_valid() ? PQerrorMessage(conn_.get()) : "Connection invalid";
        if (res) PQclear(res);
        throw std::runtime_error("Failed to commit transaction: " + error);
    }
    PQclear(res);
    committed_ = true;
    active_ = false;
}

void Transaction::rollback() {
    if (!active_ || committed_) return;
    
    const char* query = "ROLLBACK";
    PGresult* res = PQexec(conn_.get(), query);
    if (!res) {
        // ROLLBACK 失败也不抛异常，避免覆盖原始异常
        return;
    }
    PQclear(res);
    active_ = false;
}

// ========== QueryResult 实现 ==========

QueryResult::QueryResult(PGresult* result) : result_(result) {
}

QueryResult::~QueryResult() {
    if (result_) {
        PQclear(result_);
    }
}

QueryResult::QueryResult(QueryResult&& other) noexcept
    : result_(other.result_) {
    other.result_ = nullptr;
}

QueryResult& QueryResult::operator=(QueryResult&& other) noexcept {
    if (this != &other) {
        if (result_) PQclear(result_);
        result_ = other.result_;
        other.result_ = nullptr;
    }
    return *this;
}

int QueryResult::num_rows() const {
    if (!result_) return 0;
    return PQntuples(result_);
}

int QueryResult::num_fields() const {
    if (!result_) return 0;
    return PQnfields(result_);
}

std::optional<std::string> QueryResult::get_value(int row, int col) const {
    if (!result_ || row >= num_rows() || col >= num_fields()) return std::nullopt;
    
    const char* value = PQgetvalue(result_, row, col);
    if (!value || value[0] == '\0') return std::nullopt;
    
    return std::string(value);
}

std::optional<int> QueryResult::get_int(int row, int col) const {
    auto val = get_value(row, col);
    if (!val) return std::nullopt;
    try {
        return std::stoi(*val);
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<int64_t> QueryResult::get_int64(int row, int col) const {
    auto val = get_value(row, col);
    if (!val) return std::nullopt;
    try {
        return std::stoll(*val);
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<double> QueryResult::get_double(int row, int col) const {
    auto val = get_value(row, col);
    if (!val) return std::nullopt;
    try {
        return std::stod(*val);
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<bool> QueryResult::get_bool(int row, int col) const {
    auto val = get_value(row, col);
    if (!val) return std::nullopt;
    return (val->length() == 1 && (val->at(0) == 't' || val->at(0) == 'T' || val->at(0) == '1'));
}

std::optional<std::string> QueryResult::get_value(int row, const char* col_name) const {
    if (!result_ || row >= num_rows()) return std::nullopt;
    
    int col_num = PQfnumber(result_, col_name);
    if (col_num < 0) return std::nullopt;
    
    const char* value = PQgetvalue(result_, row, col_num);
    if (!value || value[0] == '\0') return std::nullopt;
    
    return std::string(value);
}

bool QueryResult::is_valid() const {
    return result_ != nullptr;
}

bool QueryResult::has_rows() const {
    return is_valid() && num_rows() > 0;
}

// ========== Repository 实现 ==========

Repository::Repository(const Config& config) : config_(config) {
    database_url_ = config.database_url;
    if (config.enable_connection_pool) {
        connection_pool_.resize(config.pool_size);
        for (size_t i = 0; i < connection_pool_.size(); ++i) {
            connection_pool_[i].conn = nullptr;
            connection_pool_[i].in_use = false;
        }
    }
    std::cout << "[Backend][Repository] Initialized with DB URL: " 
              << database_url_ << ", pool_size=" << config.pool_size << std::endl;
}

Repository::~Repository() {
    // 释放连接池
    std::lock_guard<std::mutex> lock(pool_mutex_);
    for (auto& entry : connection_pool_) {
        if (entry.conn) {
            PQfinish(entry.conn);
            entry.conn = nullptr;
        }
    }
}

PGconn* Repository::create_connection() {
    PGconn* conn = PQconnectdb(database_url_.c_str());
    if (!conn || PQstatus(conn) != CONNECTION_OK) {
        std::string error = conn ? PQerrorMessage(conn) : "Failed to create connection";
        std::cerr << "[Backend][Repository] Failed to connect to DB: " << error << std::endl;
        if (conn) PQfinish(conn);
        return nullptr;
    }
    return conn;
}

PGconn* Repository::find_idle_connection() {
    for (auto& entry : connection_pool_) {
        if (!entry.in_use && entry.conn) {
            // 检查连接是否仍然有效
            if (PQstatus(entry.conn) == CONNECTION_OK) {
                entry.in_use = true;
                entry.last_used = std::chrono::system_clock::now();
                return entry.conn;
            } else {
                // 连接已失效，释放
                PQfinish(entry.conn);
                entry.conn = nullptr;
            }
        }
    }
    return nullptr;
}

void Repository::release_connection(PGconn* conn) {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    for (auto& entry : connection_pool_) {
        if (entry.conn == conn) {
            entry.in_use = false;
            entry.last_used = std::chrono::system_clock::now();
            return;
        }
    }
    // 未在池中找到，直接释放
    if (conn) {
        PQfinish(conn);
    }
}

Connection Repository::get_connection() {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    
    PGconn* conn = find_idle_connection();
    if (!conn) {
        // 没有空闲连接，创建新连接
        conn = create_connection();
        if (!conn) {
            throw std::runtime_error("Failed to get DB connection");
        }
    }
    
    return Connection(conn);
}

QueryResult Repository::execute_query(
    const std::string& query,
    const std::vector<const char*>& params,
    const std::vector<int>& param_lengths
) {
    Connection conn = get_connection();
    
    PGresult* result = PQexecParams(
        conn.get(),
        query.c_str(),
        static_cast<int>(params.size()),
        nullptr,
        params.data(),
        param_lengths.empty() ? nullptr : param_lengths.data(),
        0
    );
    
    if (!result || PQresultStatus(result) != PGRES_TUPLES_OK) {
        std::string error = conn.is_valid() ? PQerrorMessage(conn.get()) : "Connection invalid";
        std::cerr << "[Backend][Repository] Query failed: " << error << std::endl;
        std::cerr << "[Backend][Repository] Query: " << query << std::endl;
        throw std::runtime_error("Query failed: " + error);
    }
    
    return QueryResult(result);
}

bool Repository::execute_command(
    const std::string& command,
    const std::vector<const char*>& params,
    const std::vector<int>& param_lengths,
    std::string* error_msg
) {
    Connection conn = get_connection();
    
    PGresult* result = PQexecParams(
        conn.get(),
        command.c_str(),
        static_cast<int>(params.size()),
        nullptr,
        params.data(),
        param_lengths.empty() ? nullptr : param_lengths.data(),
        0
    );
    
    if (!result || (PQresultStatus(result) != PGRES_COMMAND_OK && PQresultStatus(result) != PGRES_TUPLES_OK)) {
        std::string error = conn.is_valid() ? PQerrorMessage(conn.get()) : "Connection invalid";
        std::cerr << "[Backend][Repository] Command failed: " << error << std::endl;
        std::cerr << "[Backend][Repository] Command: " << command << std::endl;
        if (error_msg) {
            *error_msg = error;
        }
        if (result) PQclear(result);
        return false;
    }
    
    if (result) PQclear(result);
    return true;
}

bool Repository::audit_log(
    const std::optional<std::string>& actor_user_id,
    const std::string& action,
    const std::optional<std::string>& vin,
    const std::optional<std::string>& session_id,
    const std::optional<std::string>& ip,
    const std::optional<std::string>& user_agent,
    const std::optional<std::string>& detail_json
) {
    std::string query = R"(
        INSERT INTO audit_logs (actor_user_id, action, vin, session_id, ip, user_agent, detail_json)
        VALUES ($1::uuid, $2, $3, $4::uuid, $5, $6, $7::jsonb)
    )";
    
    std::vector<const char*> params = {
        actor_user_id ? actor_user_id->c_str() : nullptr,
        action.c_str(),
        vin ? vin->c_str() : nullptr,
        session_id ? session_id->c_str() : nullptr,
        ip ? ip->c_str() : nullptr,
        user_agent ? user_agent->c_str() : nullptr,
        detail_json ? detail_json->c_str() : nullptr
    };
    
    std::vector<int> param_lengths = {
        static_cast<int>(actor_user_id ? actor_user_id->size() : 0),
        static_cast<int>(action.size()),
        static_cast<int>(vin ? vin->size() : 0),
        static_cast<int>(session_id ? session_id->size() : 0),
        static_cast<int>(ip ? ip->size() : 0),
        static_cast<int>(user_agent ? user_agent->size() : 0),
        static_cast<int>(detail_json ? detail_json->size() : 0)
    };
    
    std::string error_msg;
    bool success = execute_command(query, params, param_lengths, &error_msg);
    if (!success) {
        std::cerr << "[Backend][Repository] Failed to insert audit log: " << error_msg << std::endl;
        return false;
    }
    
    std::cout << "[Backend][AUDIT] " << action;
    if (actor_user_id) std::cout << " actor=" << *actor_user_id;
    if (vin) std::cout << " vin=" << *vin;
    if (session_id) std::cout << " session=" << *session_id;
    std::cout << std::endl;
    
    return true;
}

bool Repository::check_connection() {
    try {
        Connection conn = get_connection();
        PGresult* result = PQexec(conn.get(), "SELECT 1");
        if (!result || PQresultStatus(result) != PGRES_TUPLES_OK) {
            if (result) PQclear(result);
            return false;
        }
        PQclear(result);
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace teleop::db
