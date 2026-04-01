#!/usr/bin/env bash
# 在已运行的 Postgres 上执行：迁移 001/002/003（若未执行）+ 种子数据 03_seed_test_data.sql。
# 用于修复「创建会话 503」「user not found or no account」：users 表不存在或无 e2e 用户、或 sessions 表缺 control_secret 等。
# 使用场景：DB 卷在加入迁移或 03_seed 之前已初始化时，无需删卷重建，直接执行本脚本即可。
# 默认连接 teleop 库（与 docker-compose.yml 中 POSTGRES_DB=teleop、POSTGRES_USER=postgres 一致）。
set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

POSTGRES_SVC="${POSTGRES_SERVICE:-teleop-postgres}"
USER="${POSTGRES_USER:-postgres}"
DB="${POSTGRES_DB:-teleop}"
MIGRATIONS_DIR="${PROJECT_ROOT}/backend/migrations"
SEED_FILE="${PROJECT_ROOT}/deploy/postgres/03_seed_test_data.sql"
COMPOSE_FILES="${COMPOSE_FILES:--f docker-compose.yml}"

# 使用 docker compose exec（按服务名），兼容项目前缀的容器名
if ! docker compose $COMPOSE_FILES ps "$POSTGRES_SVC" 2>/dev/null | grep -q "Up"; then
  echo "错误: Postgres 服务未运行: $POSTGRES_SVC"
  echo "请先启动: docker compose -f docker-compose.yml up -d teleop-postgres"
  exit 1
fi

run_sql() {
  local file="$1"
  if [ ! -f "$file" ]; then return 1; fi
  docker compose $COMPOSE_FILES exec -T "$POSTGRES_SVC" psql -v ON_ERROR_STOP=1 -U "$USER" -d "$DB" < "$file"
}

# 先执行 001（创建 accounts、users、vehicles 等表），再 002、003（幂等：ADD COLUMN IF NOT EXISTS）
for f in 001_initial_schema.sql 002_sessions_locks_mvp.sql 003_sessions_control_secret.sql; do
  if [ -f "${MIGRATIONS_DIR}/$f" ]; then
    echo "执行迁移: $f"
    run_sql "${MIGRATIONS_DIR}/$f" || true
  fi
done

if [ ! -f "$SEED_FILE" ]; then
  echo "警告: 种子文件不存在，跳过: $SEED_FILE"
  exit 0
fi

echo "执行种子数据: $SEED_FILE -> $POSTGRES_SVC ($USER@$DB)"
run_sql "$SEED_FILE"
echo "完成。可重试客户端「确认并进入驾驶」或运行: ./scripts/verify-backend-dev.sh"
