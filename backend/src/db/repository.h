/**
 * Database Repository - 通用数据库访问层
 * 
 * 功能：
 * - 连接管理（复用连接，避免频繁创建/销毁）
 * - 事务管理（BEGIN/COMMIT/ROLLBACK）
 * - 参数化查询（防 SQL 注入）
 * - 审计日志记录
 */

#ifndef TELEOP_DB_REPOSITORY_H
#define TELEOP_DB_REPOSITORY_H

#include <string>
#include <vector>
#include <optional>
#include <memory>
#include <functional>
#include <libpq-fe.h>

namespace teleop::db {

/**
 * 数据库连接管理器
 * RAII 模式，析构时自动释放连接
 */
class Connection {
public:
    Connection(PGconn* conn);
    ~Connection();
    
    // 禁止拷贝
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;
    
    // 允许移动
    Connection(Connection&& other) noexcept;
    Connection& operator=(Connection&& other) noexcept;
    
    PGconn* get() const { return conn_; }
    bool is_valid() const { return conn_ != nullptr; }
    
private:
    PGconn* conn_;
    bool owns_connection_;
};

/**
 * 事务管理器
 * RAII 模式，析构时自动提交或回滚
 */
class Transaction {
public:
    enum class RollbackPolicy {
        ON_EXCEPTION,  // 仅异常时回滚
        ALWAYS_ROLLBACK // 始终回滚（测试用）
    };
    
    Transaction(Connection& conn, RollbackPolicy policy = RollbackPolicy::ON_EXCEPTION);
    ~Transaction();
    
    // 手动提交
    void commit();
    
    // 手动回滚
    void rollback();
    
    bool is_committed() const { return committed_; }
    bool is_active() const { return active_; }
    
private:
    Connection& conn_;
    bool committed_;
    bool active_;
    RollbackPolicy policy_;
};

/**
 * 查询结果封装
 */
class QueryResult {
public:
    QueryResult(PGresult* result);
    ~QueryResult();
    
    // 禁止拷贝
    QueryResult(const QueryResult&) = delete;
    QueryResult& operator=(const QueryResult&) = delete;
    
    // 允许移动
    QueryResult(QueryResult&& other) noexcept;
    QueryResult& operator=(QueryResult&& other) noexcept;
    
    int num_rows() const;
    int num_fields() const;
    
    // 获取字段值（索引方式）
    std::optional<std::string> get_value(int row, int col) const;
    std::optional<int> get_int(int row, int col) const;
    std::optional<int64_t> get_int64(int row, int col) const;
    std::optional<double> get_double(int row, int col) const;
    std::optional<bool> get_bool(int row, int col) const;
    
    // 获取字段值（列名方式）
    std::optional<std::string> get_value(int row, const char* col_name) const;
    
    // 检查状态
    bool is_valid() const;
    bool has_rows() const;
    
private:
    PGresult* result_;
};

/**
 * Database Repository 类
 * 提供：
 * - 连接获取
 * - 参数化查询执行
 * - 事务管理
 * - 审计日志
 */
class Repository {
public:
    struct Config {
        std::string database_url;
        bool enable_connection_pool = true;
        int pool_size = 10;
        int connection_timeout = 10;
    };
    
    explicit Repository(const Config& config);
    ~Repository();
    
    // 获取连接（从连接池或新建）
    Connection get_connection();
    
    // 执行参数化查询（返回结果）
    QueryResult execute_query(
        const std::string& query,
        const std::vector<const char*>& params,
        const std::vector<int>& param_lengths = {}
    );
    
    // 执行非查询语句（INSERT/UPDATE/DELETE）
    bool execute_command(
        const std::string& command,
        const std::vector<const char*>& params,
        const std::vector<int>& param_lengths = {},
        std::string* error_msg = nullptr
    );
    
    // 插入审计日志
    bool audit_log(
        const std::optional<std::string>& actor_user_id,
        const std::string& action,
        const std::optional<std::string>& vin,
        const std::optional<std::string>& session_id,
        const std::optional<std::string>& ip,
        const std::optional<std::string>& user_agent,
        const std::optional<std::string>& detail_json
    );
    
    // 检查连接是否有效
    bool check_connection();
    
    // 获取配置
    const Config& get_config() const { return config_; }

private:
    Config config_;
    std::string database_url_;
    
    // 连接池（简化版：暂用 vector 存储空闲连接）
    struct PoolEntry {
        PGconn* conn;
        bool in_use;
        std::chrono::system_clock::time_point last_used;
    };
    std::vector<PoolEntry> connection_pool_;
    std::mutex pool_mutex_;
    
    // 创建新连接
    PGconn* create_connection();
    
    // 释放连接回池
    void release_connection(PGconn* conn);
    
    // 查找空闲连接
    PGconn* find_idle_connection();
};

} // namespace teleop::db

#endif // TELEOP_DB_REPOSITORY_H
