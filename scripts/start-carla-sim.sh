#!/usr/bin/env bash
# 启动「CARLA 仿真 + 远驾客户端看流」所需的基础栈（不启动真实车端），便于手动点击「连接车端」后看到仿真推流。
#
# 用法：
#   ./scripts/start-carla-sim.sh
#
# 说明：
#   - 仅使用 docker-compose.yml，不加载 docker-compose.vehicle.dev.yml，故不会启动 vehicle 容器。
#   - 启动后需在宿主机手动：1) 启动 CARLA 2) 启动 carla-bridge 3) 启动客户端并选车 carla-sim-001、点击「连接车端」。

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd "$PROJECT_ROOT"

# 仅主编排，不包含车端
COMPOSE="docker compose -f docker-compose.yml"
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

echo -e "${CYAN}========== CARLA 仿真 + 客户端看流（仅基础栈，不启动车端）==========${NC}"
echo ""

echo -e "${CYAN}[1/3] 启动 Postgres / Keycloak / ZLM / Backend / Mosquitto / Coturn ...${NC}"
$COMPOSE up -d postgres keycloak coturn zlmediakit backend mosquitto 2>/dev/null || $COMPOSE up -d
echo ""

echo -e "${CYAN}[2/3] 等待服务就绪（约 60s）...${NC}"
for i in 1 2 3 4 5 6 7 8 9 10; do
  if curl -sf http://127.0.0.1:8081/health >/dev/null 2>&1 && \
     curl -sf http://127.0.0.1:8080/health/ready >/dev/null 2>&1 && \
     curl -sf http://127.0.0.1:80/index/api/getServerConfig >/dev/null 2>&1; then
    echo -e "${GREEN}✓ 基础服务已就绪${NC}"
    break
  fi
  if [ $i -eq 10 ]; then
    echo -e "${YELLOW}⊘ 部分服务未就绪，请稍后重试或检查日志${NC}"
  else
    sleep 6
  fi
done
echo ""

echo -e "${CYAN}[3/3] 后续步骤（手动）${NC}"
echo ""
echo -e "${GREEN}1) 启动 CARLA 服务器（若未启动）：${NC}"
echo "   docker run -d --name carla-server -p 2000-2002:2000-2002 --runtime=nvidia -e NVIDIA_VISIBLE_DEVICES=0 carlasim/carla:latest"
echo ""
echo -e "${GREEN}2) 启动 CARLA Bridge（宿主机）：${NC}"
echo "   cd carla-bridge"
echo "   pip install -r requirements.txt"
echo "   export CARLA_HOST=127.0.0.1 MQTT_BROKER=127.0.0.1 ZLM_HOST=127.0.0.1"
echo "   python3 carla_bridge.py"
echo ""
echo -e "${GREEN}3) 启动客户端：${NC}"
echo "   - 本机有图形界面时，可运行："
echo "     docker compose -f docker-compose.yml run --rm -e DISPLAY=\$DISPLAY -v /tmp/.X11-unix:/tmp/.X11-unix client-dev bash -c 'cd /tmp/client-build 2>/dev/null || (mkdir -p /tmp/client-build && cd /tmp/client-build && cmake /workspace/client -DCMAKE_PREFIX_PATH=/opt/Qt/6.8.0/gcc_64:/opt/libdatachannel -DCMAKE_BUILD_TYPE=Debug && make -j4) && ./RemoteDrivingClient'"
echo "   - 或使用项目全链路脚本中的客户端启动方式（但不启动车端）。"
echo ""
echo -e "${GREEN}4) 在客户端内：${NC}"
echo "   登录（如 e2e-test / 123）→ 选车「carla-sim-001」→ 确认进入驾驶 → 点击「连接车端」→ 约 6s 后四路仿真画面应出现。"
echo ""
echo -e "详细说明见: ${CYAN}docs/CARLA_CLIENT_STREAM_GUIDE.md${NC}"
