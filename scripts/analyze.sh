#!/bin/bash
# 一键收集关键日志与交互记录，用于问题分析。
# 用法：
#   ./scripts/analyze.sh [OUTPUT_DIR]
# 默认输出到 ./diags/<timestamp>。
#
# 收集内容：
#   - 各模块容器日志（按最近 N 分钟或全部）
#   - recordings/ 下的交互记录（若存在）
#   - docker ps 与 docker inspect（网络/端口等）
#   - 环境变量（脱敏敏感字段）

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd "$PROJECT_ROOT"

OUTPUT="${1:-}"
if [ -z "$OUTPUT" ]; then
    TS=$(date -u +"%Y%m%dT%H%M%SZ")
    OUTPUT="./diags/$TS"
fi

echo "========================================="
echo "收集诊断信息到: $OUTPUT"
echo "========================================="
mkdir -p "$OUTPUT"

# 1. 各模块日志（最近 10 分钟或全部）
LOGS_DIR="$OUTPUT/logs"
mkdir -p "$LOGS_DIR"

collect_logs() {
    local service="$1"
    local file="$LOGS_DIR/${service}.log"
    echo "收集 $service 日志 -> $file"
    docker compose logs --tail=5000 "$service" 2>&1 > "$file" || true
}

collect_logs "backend"
collect_logs "carla-bridge"
collect_logs "mosquitto"
collect_logs "zlmediakit"
collect_logs "client-dev" 2>/dev/null || true  # 可能不存在

echo "✓ 日志收集完成"

# 2. 交互记录（NDJSON）
if [ -d "recordings" ] && [ "$(ls -A recordings)" ]; then
    mkdir -p "$OUTPUT/recordings"
    echo "收集交互记录 -> $OUTPUT/recordings"
    cp -r recordings/. "$OUTPUT/recordings/"
else
    echo "交互记录目录 recordings 不存在或为空"
fi

# 3. Docker 信息
DOCKER_INFO="$OUTPUT/docker-info.txt"
{
    echo "=== docker ps ==="
    docker ps --format "table {{.Names}}\t{{.Status}}\t{{.Ports}}" 2>&1 || true
    echo ""
    echo "=== docker network ls ==="
    docker network ls 2>&1 || true
    echo ""
    echo "=== docker inspect backend ==="
    docker compose ps -q backend | xargs docker inspect 2>&1 || true
    echo ""
    echo "=== docker inspect zlmediakit ==="
    docker compose ps -q zlmediakit | xargs docker inspect 2>&1 || true
} > "$DOCKER_INFO"
echo "✓ Docker 信息已收集"

# 4. 环境变量（脱敏）
ENV_FILE="$OUTPUT/docker-compose.env"
if [ -f ".env" ]; then
    sed 's/\(PASSWORD\|SECRET\|TOKEN\|API_KEY\)=.*/\1=***REDACTED***/g' .env > "$ENV_FILE"
    echo "✓ 环境变量已脱敏导出"
else
    echo "无 .env 文件"
fi

# 5. 版本与 Git 信息
VERSION_INFO="$OUTPUT/version.txt"
{
    echo "=== Git commit (if in repo) ==="
    git rev-parse HEAD 2>/dev/null || echo "not a git repo"
    echo "=== Docker images (backend, carla-bridge, zlmediakit) ==="
    docker images --format "table {{.Repository}}\t{{.Tag}}\t{{.CreatedAt}}" | grep -E 'backend|carla-bridge|zlmediakit' || true
} > "$VERSION_INFO"
echo "✓ 版本信息已收集"

# 6. 自动诊断结果摘要（可选）
if command -v python3 >/dev/null 2>&1; then
    python3 "$SCRIPT_DIR/auto_diagnose.py" --log-dir "$LOGS_DIR" --output "$OUTPUT/diagnosis.txt" || true
    echo "✓ 自动诊断结果 -> $OUTPUT/diagnosis.txt"
else
    echo "python3 未安装，跳过自动诊断"
fi

echo "========================================="
echo "收集完成，目录: $OUTPUT"
echo "可执行："
echo "  grep 'vin=' $OUTPUT/logs/*.log"
echo "  ./scripts/analyze_interaction_log.py --dir $OUTPUT/recordings"
echo "========================================="
