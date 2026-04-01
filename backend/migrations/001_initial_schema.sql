-- 数据库迁移脚本：初始表结构
-- 基于 project_spec.md §4.3 数据模型

-- 扩展
CREATE EXTENSION IF NOT EXISTS "uuid-ossp";
CREATE EXTENSION IF NOT EXISTS "pg_trgm";  -- 用于文本搜索

-- 账号表
CREATE TABLE accounts (
    id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    name VARCHAR(255) NOT NULL UNIQUE,
    created_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP
);

-- 用户表（关联 Keycloak）
CREATE TABLE users (
    id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    keycloak_sub VARCHAR(255) NOT NULL UNIQUE,  -- Keycloak Subject ID
    account_id UUID NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
    email VARCHAR(255),
    username VARCHAR(255),
    created_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX idx_users_keycloak_sub ON users(keycloak_sub);
CREATE INDEX idx_users_account_id ON users(account_id);

-- 车辆表
CREATE TABLE vehicles (
    vin VARCHAR(17) PRIMARY KEY,  -- VIN 码（17位）
    model VARCHAR(100),
    capabilities JSONB,  -- 车辆能力（摄像头列表、功能等）
    safety_profile JSONB,  -- 安全配置（限速、超时等）
    created_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP
);

-- 账号-车辆绑定表
CREATE TABLE account_vehicles (
    account_id UUID NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
    vin VARCHAR(17) NOT NULL REFERENCES vehicles(vin) ON DELETE CASCADE,
    bind_code VARCHAR(64),  -- 绑定码（用于绑定验证）
    bind_time TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP,
    status VARCHAR(20) DEFAULT 'active',  -- active, inactive, suspended
    PRIMARY KEY (account_id, vin)
);

CREATE INDEX idx_account_vehicles_account_id ON account_vehicles(account_id);
CREATE INDEX idx_account_vehicles_vin ON account_vehicles(vin);

-- VIN 授权表
CREATE TABLE vin_grants (
    id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    vin VARCHAR(17) NOT NULL REFERENCES vehicles(vin) ON DELETE CASCADE,
    grantee_user_id UUID NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    permissions TEXT[] NOT NULL,  -- ['vin.view', 'vin.control', 'vin.maintain']
    expires_at TIMESTAMP WITH TIME ZONE,
    created_by UUID NOT NULL REFERENCES users(id),
    created_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP,
    UNIQUE(vin, grantee_user_id)
);

CREATE INDEX idx_vin_grants_vin ON vin_grants(vin);
CREATE INDEX idx_vin_grants_grantee_user_id ON vin_grants(grantee_user_id);
CREATE INDEX idx_vin_grants_expires_at ON vin_grants(expires_at);

-- 会话表
CREATE TABLE sessions (
    session_id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    vin VARCHAR(17) NOT NULL REFERENCES vehicles(vin),
    controller_user_id UUID REFERENCES users(id),  -- NULL 表示观察者会话
    state VARCHAR(20) NOT NULL DEFAULT 'REQUESTED',  -- REQUESTED, ACTIVE, ENDING, ENDED, FAILED
    started_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP,
    ended_at TIMESTAMP WITH TIME ZONE,
    last_heartbeat_at TIMESTAMP WITH TIME ZONE,
    session_secret VARCHAR(64),  -- 用于消息签名（短期密钥）
    metadata JSONB,  -- 会话元数据
    created_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX idx_sessions_vin ON sessions(vin);
CREATE INDEX idx_sessions_controller_user_id ON sessions(controller_user_id);
CREATE INDEX idx_sessions_state ON sessions(state);
CREATE INDEX idx_sessions_started_at ON sessions(started_at);

-- 会话参与者表
CREATE TABLE session_participants (
    session_id UUID NOT NULL REFERENCES sessions(session_id) ON DELETE CASCADE,
    user_id UUID NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    role_in_session VARCHAR(20) NOT NULL,  -- controller, observer
    joined_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP,
    left_at TIMESTAMP WITH TIME ZONE,
    PRIMARY KEY (session_id, user_id)
);

CREATE INDEX idx_session_participants_session_id ON session_participants(session_id);
CREATE INDEX idx_session_participants_user_id ON session_participants(user_id);

-- 故障事件表
CREATE TABLE fault_events (
    id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    vin VARCHAR(17) NOT NULL REFERENCES vehicles(vin),
    session_id UUID REFERENCES sessions(session_id),
    code VARCHAR(20) NOT NULL,  -- 如 TEL-1001, SEC-7001
    severity VARCHAR(20) NOT NULL,  -- INFO, WARN, ERROR, CRITICAL
    domain VARCHAR(50) NOT NULL,  -- TELEOP, NETWORK, VEHICLE_CTRL, CAMERA, POWER, SWEEPER, SECURITY
    latch BOOLEAN DEFAULT FALSE,  -- 是否锁存
    message TEXT NOT NULL,
    recommended_action TEXT,
    payload JSONB,  -- 结构化数据
    ts TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP,
    cleared_at TIMESTAMP WITH TIME ZONE,
    cleared_by UUID REFERENCES users(id)
);

CREATE INDEX idx_fault_events_vin ON fault_events(vin);
CREATE INDEX idx_fault_events_session_id ON fault_events(session_id);
CREATE INDEX idx_fault_events_severity ON fault_events(severity);
CREATE INDEX idx_fault_events_ts ON fault_events(ts);
CREATE INDEX idx_fault_events_latch ON fault_events(latch) WHERE latch = TRUE;

-- 审计日志表
CREATE TABLE audit_logs (
    id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    actor_user_id UUID REFERENCES users(id),
    action VARCHAR(100) NOT NULL,  -- login, session_start, session_end, vin_grant, vin_revoke, estop, etc.
    vin VARCHAR(17) REFERENCES vehicles(vin),
    session_id UUID REFERENCES sessions(session_id),
    ip INET,
    user_agent TEXT,
    detail_json JSONB,  -- 详细操作信息
    ts TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX idx_audit_logs_actor_user_id ON audit_logs(actor_user_id);
CREATE INDEX idx_audit_logs_vin ON audit_logs(vin);
CREATE INDEX idx_audit_logs_session_id ON audit_logs(session_id);
CREATE INDEX idx_audit_logs_action ON audit_logs(action);
CREATE INDEX idx_audit_logs_ts ON audit_logs(ts);

-- 更新时间触发器函数
CREATE OR REPLACE FUNCTION update_updated_at_column()
RETURNS TRIGGER AS $$
BEGIN
    NEW.updated_at = CURRENT_TIMESTAMP;
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

-- 为需要 updated_at 的表创建触发器
CREATE TRIGGER update_accounts_updated_at BEFORE UPDATE ON accounts
    FOR EACH ROW EXECUTE FUNCTION update_updated_at_column();

CREATE TRIGGER update_users_updated_at BEFORE UPDATE ON users
    FOR EACH ROW EXECUTE FUNCTION update_updated_at_column();

CREATE TRIGGER update_vehicles_updated_at BEFORE UPDATE ON vehicles
    FOR EACH ROW EXECUTE FUNCTION update_updated_at_column();

CREATE TRIGGER update_vin_grants_updated_at BEFORE UPDATE ON vin_grants
    FOR EACH ROW EXECUTE FUNCTION update_updated_at_column();

CREATE TRIGGER update_sessions_updated_at BEFORE UPDATE ON sessions
    FOR EACH ROW EXECUTE FUNCTION update_updated_at_column();

-- 注释
COMMENT ON TABLE accounts IS '账号表';
COMMENT ON TABLE users IS '用户表（关联 Keycloak）';
COMMENT ON TABLE vehicles IS '车辆表（VIN）';
COMMENT ON TABLE account_vehicles IS '账号-车辆绑定表';
COMMENT ON TABLE vin_grants IS 'VIN 授权表';
COMMENT ON TABLE sessions IS '会话表';
COMMENT ON TABLE session_participants IS '会话参与者表';
COMMENT ON TABLE fault_events IS '故障事件表';
COMMENT ON TABLE audit_logs IS '审计日志表';
