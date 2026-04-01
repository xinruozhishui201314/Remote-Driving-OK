-- 002_sessions_locks_mvp.sql
-- 为 sessions 表增加锁相关字段（MVP：单持有者锁 + TTL）

ALTER TABLE sessions
    ADD COLUMN IF NOT EXISTS lock_id UUID,
    ADD COLUMN IF NOT EXISTS lock_owner_user_id UUID REFERENCES users(id),
    ADD COLUMN IF NOT EXISTS lock_expires_at TIMESTAMPTZ;

CREATE INDEX IF NOT EXISTS idx_sessions_lock_owner_user_id
    ON sessions(lock_owner_user_id);

