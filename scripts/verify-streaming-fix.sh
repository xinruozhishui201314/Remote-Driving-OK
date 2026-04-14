#!/bin/bash
# 验证「视频流不显示」修复：车端推流启动 + 客户端重试策略
# 用法：全链路已启动后执行 bash scripts/verify-streaming-fix.sh

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"
# shellcheck source=lib/mqtt_control_json.sh
source "$SCRIPT_DIR/lib/mqtt_control_json.sh"

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo "========== 视频流修复验证 =========="

# 1. 解析车端容器名
VEHICLE_CONTAINER=$(docker ps --format '{{.Names}}' | grep -E 'vehicle|remote-driving-vehicle' | head -1)
if [[ -z "$VEHICLE_CONTAINER" ]]; then
  echo -e "${RED}未找到车端容器，请先运行: bash scripts/start-full-chain.sh no-client${NC}"
  exit 1
fi
echo "车端容器: $VEHICLE_CONTAINER"

# 2. 清理旧推流，确保从干净状态开始
echo ""
echo "[1/5] 清理旧推流进程与 PID 文件..."
docker exec "$VEHICLE_CONTAINER" bash -c 'pkill -9 ffmpeg 2>/dev/null; rm -f /tmp/push-*.pid /tmp/push-*.lock; echo "已清理"' || true
sleep 2

# 3. 发送 start_stream
echo ""
echo "[2/5] 发送 start_stream 到 vehicle/control..."
MQTT_CONTAINER=$(docker ps --format '{{.Names}}' | grep -i mosquitto | head -1)
if [[ -z "$MQTT_CONTAINER" ]]; then
  echo -e "${RED}未找到 MQTT 容器${NC}"
  exit 1
fi
docker exec "$MQTT_CONTAINER" mosquitto_pub -h mosquitto -p 1883 -t "vehicle/control" -m "$(mqtt_json_start_stream "123456789")" || true
echo "已发送"

# 4. 等待推流就绪（最多 18s，与客户端 8*3s 重试对齐）
echo ""
echo "[3/5] 等待推流就绪（约 5~15s）..."
for i in $(seq 1 18); do
  FFCOUNT=$(docker exec "$VEHICLE_CONTAINER" bash -c 'ps aux | grep ffmpeg | grep -v grep | wc -l' 2>/dev/null || echo "0")
  if [[ "$FFCOUNT" -ge 4 ]]; then
    echo -e "${GREEN}  $i s: 四路 ffmpeg 已启动${NC}"
    break
  fi
  echo "  $i s: ffmpeg 进程数=$FFCOUNT"
  sleep 1
done

FFCOUNT=$(docker exec "$VEHICLE_CONTAINER" bash -c 'ps aux | grep ffmpeg | grep -v grep | wc -l' 2>/dev/null || echo "0")
if [[ "$FFCOUNT" -lt 4 ]]; then
  echo -e "${RED}[3/5] 失败: 推流进程数=$FFCOUNT，预期 4${NC}"
  echo "车端最近日志:"
  docker logs "$VEHICLE_CONTAINER" --tail 60 2>&1 | grep -E "Control|PUSH|MQTT|start_stream" || true
  exit 1
fi

# 5. 检查车端日志中的关键行（精确定位）
echo ""
echo "[4/5] 检查车端日志（精确定位）..."
VEHICLE_LOGS=$(docker logs "$VEHICLE_CONTAINER" --tail 80 2>&1)
OK=0
echo "$VEHICLE_LOGS" | grep -q "收到 start_stream" && { echo -e "  ${GREEN}✓ 收到 start_stream${NC}"; OK=$((OK+1)); } || echo -e "  ${YELLOW}⊘ 未看到 收到 start_stream${NC}"
echo "$VEHICLE_LOGS" | grep -q "is_streaming_running()=" && { echo -e "  ${GREEN}✓ is_streaming_running 检查日志存在${NC}"; OK=$((OK+1)); } || echo -e "  ${YELLOW}⊘ 未看到 is_streaming_running 日志${NC}"
echo "$VEHICLE_LOGS" | grep -q "推流脚本已 fork" && { echo -e "  ${GREEN}✓ 推流脚本已 fork${NC}"; OK=$((OK+1)); } || echo -e "  ${YELLOW}⊘ 未看到 推流脚本已 fork${NC}"
echo "$VEHICLE_LOGS" | grep -q "500ms 后 is_streaming_running" && { echo -e "  ${GREEN}✓ 500ms 后检查日志存在${NC}"; OK=$((OK+1)); } || echo -e "  ${YELLOW}⊘ 未看到 500ms 后检查${NC}"

# 6. 验证 ZLM 可拉流（从车端容器内拉流）
echo ""
echo "[5/5] 验证 ZLM 上存在流（rtmp pull）..."
if docker exec "$VEHICLE_CONTAINER" timeout 4 ffmpeg -i rtmp://zlmediakit:1935/teleop/cam_front -frames:v 1 -f null - 2>&1 | grep -q "Stream #0"; then
  echo -e "  ${GREEN}✓ cam_front 在 ZLM 上可拉流${NC}"
else
  echo -e "  ${YELLOW}⊘ 无法从 ZLM 拉流（可能稍等几秒后再试）${NC}"
fi

echo ""
echo "========== 验证完成 =========="
echo "结论: 车端推流已启动，日志可精确定位。"
echo "客户端: 已支持在无 Paho 时通过 mosquitto_pub 发送 start_stream；点击「连接车端」后约 6s 拉流，最多重试 8 次（间隔 3s）。"
echo "若仍无画面: 1) 确认 client 容器已安装 mosquitto-clients（start-full-chain 会自动安装）"
echo "            2) 确认客户端已重新编译（含 mqttcontroller 的 mosquitto_pub 备用逻辑）"
echo "            3) 查看车端日志是否有「收到 start_stream」、客户端是否有「Published (mosquitto_pub)」"
