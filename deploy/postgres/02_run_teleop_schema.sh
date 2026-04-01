#!/bin/sh
# 在 teleop_db 上执行 backend 迁移（仅首次 init 时运行）
# 使用 POSTGRES_USER（compose 中为 teleop_user），否则 psql 默认可能用 postgres 导致 role 不存在
set -e
psql -v ON_ERROR_STOP=1 -d teleop_db -U "${POSTGRES_USER:-teleop_user}" -f /migrations/001_initial_schema.sql
psql -v ON_ERROR_STOP=1 -d teleop_db -U "${POSTGRES_USER:-teleop_user}" -f /migrations/002_sessions_locks_mvp.sql
psql -v ON_ERROR_STOP=1 -d teleop_db -U "${POSTGRES_USER:-teleop_user}" -f /migrations/003_sessions_control_secret.sql
