-- 003_sessions_control_secret.sql
-- 为 sessions 增加控制通道相关字段（控制密钥 + 初始 seq）

ALTER TABLE sessions
    ADD COLUMN IF NOT EXISTS control_secret BYTEA,
    ADD COLUMN IF NOT EXISTS control_seq_start BIGINT DEFAULT 1;

-- 可选索引，便于按 session_id 查控制参数（主键已覆盖，这里不再额外创建索引）

