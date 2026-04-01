#!/usr/bin/env bash
# 验证 /admin/add-vehicle 管理页是否可访问，并依据 backend 日志给出结论
# 用法：./scripts/verify-admin-add-vehicle-page.sh
# 若使用多文件：export COMPOSE_FILES="-f docker-compose.yml -f docker-compose.vehicle.dev.yml"

set -e
set -o pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

COMPOSE_FILES="${COMPOSE_FILES:--f docker-compose.yml -f docker-compose.vehicle.dev.yml -f docker-compose.dev.yml}"
COMPOSE_CMD="docker compose ${COMPOSE_FILES}"
BACKEND_URL="${BACKEND_URL:-http://localhost:8081}"

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

echo -e "${CYAN}========== 验证 /admin/add-vehicle 管理页 ==========${NC}"
echo ""

# 1) 请求管理页
echo -e "${CYAN}[1] 请求 GET ${BACKEND_URL}/admin/add-vehicle${NC}"
HTTP_CODE=$(curl -s -o /tmp/verify-admin-page-body.html -w "%{http_code}" "${BACKEND_URL}/admin/add-vehicle" || echo "000")
echo "    HTTP 状态码: ${HTTP_CODE}"

# 2) 拉取 backend 最近日志（便于依据日志分析）
echo ""
echo -e "${CYAN}[2] Backend 最近日志（依据日志判断根因）${NC}"
BACKEND_LOGS=$($COMPOSE_CMD logs --tail=60 backend 2>/dev/null || docker logs teleop-backend --tail=60 2>/dev/null || true)
if [ -n "$BACKEND_LOGS" ]; then
  echo "$BACKEND_LOGS" | while read -r line; do echo "    $line"; done
else
  echo "    (无法获取日志，请手动执行: docker compose logs backend --tail=50 或 docker logs teleop-backend --tail=50)"
fi

echo ""
echo -e "${CYAN}[3] 结论与处理${NC}"
if [ "$HTTP_CODE" = "200" ]; then
  if grep -q "增加车辆" /tmp/verify-admin-page-body.html 2>/dev/null; then
    echo -e "  ${GREEN}通过：管理页返回 200，且 body 含「增加车辆」，功能正常。${NC}"
    echo "  可在浏览器打开: ${BACKEND_URL}/admin/add-vehicle"
    exit 0
  else
    echo -e "  ${YELLOW}状态 200 但 body 未含「增加车辆」，请检查返回内容。${NC}"
    exit 1
  fi
fi

# 404 或其它
echo -e "  ${RED}未通过：HTTP ${HTTP_CODE}${NC}"
if [ "$HTTP_CODE" = "404" ]; then
  if echo "$BACKEND_LOGS" | grep -q "\[Backend\]\[未匹配\].*GET.*admin/add-vehicle"; then
    echo -e "  ${CYAN}[依据日志] 请求已到达 backend 但无匹配路由，当前二进制未包含 GET /admin/add-vehicle。${NC}"
    echo -e "  ${YELLOW}处理：重新构建 backend 并重启：${NC}"
    echo "    docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml build backend"
    echo "    docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml up -d backend"
  elif echo "$BACKEND_LOGS" | grep -q "\[Backend\]\[GET /admin/add-vehicle\] 404 无法打开文件"; then
    echo -e "  ${CYAN}[依据日志] 路由存在但无法打开静态文件（errno 见上方日志）。${NC}"
    echo -e "  ${YELLOW}处理：确认 compose 中 backend 已挂载 ./backend/static:/app/static:ro，并重启 backend。${NC}"
  elif echo "$BACKEND_LOGS" | grep -q "\[Backend\]\[启动\] add-vehicle.html 可读=否"; then
    echo -e "  ${CYAN}[依据日志] 启动时 add-vehicle.html 不可读。${NC}"
    echo -e "  ${YELLOW}处理：确认宿主机存在 backend/static/add-vehicle.html，且 compose 挂载 backend/static，再重启。${NC}"
  else
    echo -e "  ${YELLOW}请根据上方日志判断：若见 [Backend][未匹配] 则需重建 backend；若见 404 无法打开文件 则需挂载 static 或重建镜像。${NC}"
    echo -e "  ${YELLOW}若无法获取日志，多数为当前 backend 镜像较旧、未包含 GET /admin/add-vehicle，请先执行：${NC}"
    echo "    docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml build backend"
    echo "    docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml up -d backend"
  fi
fi
exit 1
