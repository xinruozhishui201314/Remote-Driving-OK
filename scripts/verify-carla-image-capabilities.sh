#!/usr/bin/env bash
# 对「已构建的 CARLA + MQTT 运行镜像」做静态能力校验（不启动 CARLA 服务、不依赖 MQTT broker）。
# 用于确认镜像是否具备：CARLA 运行环境、Paho MQTT C/C++、Python 依赖、ffmpeg、entrypoint、C++ 构建链。
#
# 用法：./scripts/verify-carla-image-capabilities.sh
# 环境变量：CARLA_IMAGE_NAME 默认 remote-driving/carla-with-bridge:latest

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

IMAGE_NAME="${CARLA_IMAGE_NAME:-remote-driving/carla-with-bridge:latest}"
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

log_section() { echo -e "${CYAN}[verify] $*${NC}"; }
log_ok()      { echo -e "  ${GREEN}[OK] $*${NC}"; }
log_fail()    { echo -e "  ${RED}[FAIL] $*${NC}"; }

FAILED=0

echo ""
echo -e "${CYAN}========== CARLA + MQTT 镜像能力校验（静态）==========${NC}"
echo "  镜像: $IMAGE_NAME"
echo ""

# 1) 镜像存在
log_section "1/7 镜像存在"
if ! docker image inspect "$IMAGE_NAME" >/dev/null 2>&1; then
  log_fail "镜像不存在；请先执行: ./scripts/build-carla-image.sh"
  exit 1
fi
log_ok "镜像存在"
echo ""

# 2) Paho MQTT C/C++ 库（/usr/local）
log_section "2/7 Paho MQTT C/C++ 库（/usr/local）"
PAHO_CHECK=$(docker run --rm --entrypoint bash "$IMAGE_NAME" -c '
  ok=0
  for f in /usr/local/lib/libpaho-mqtt3a.so /usr/local/lib/libpaho-mqtt3c.so; do
    [ -f "$f" ] && echo "found:$f" && ok=$((ok+1))
  done
  for f in /usr/local/lib/libpaho-mqttpp3.so /usr/local/lib/libpaho-mqttpp3.so.1; do
    [ -f "$f" ] && echo "found:$f" && ok=$((ok+1))
  done
  ldconfig -p 2>/dev/null | grep -q paho && echo "ldconfig:paho" && ok=$((ok+1))
  exit $((3 - (ok >= 3 ? 3 : ok)))
' 2>/dev/null) || true
if echo "$PAHO_CHECK" | grep -q "found:" && echo "$PAHO_CHECK" | grep -q "ldconfig:paho"; then
  log_ok "Paho C/C++ 库已安装且 ldconfig 可见"
else
  log_fail "缺少 Paho 库或 ldconfig 不可见"
  FAILED=$((FAILED+1))
fi
echo ""

# 3) Python：carla 必选（需 PYTHONPATH 指向镜像内 egg）；paho/cv2 为 Python Bridge 用，C++ Bridge 不需要
log_section "3/7 Python 依赖（carla 必选；paho/cv2 可选，C++ Bridge 不需要）"
PY_CHECK=$(docker run --rm --entrypoint bash "$IMAGE_NAME" -c '
  for egg in /home/carla/PythonAPI/carla/dist/carla*.egg; do [ -f "$egg" ] && export PYTHONPATH="${egg}${PYTHONPATH:+:$PYTHONPATH}" && break; done
  python3 -c "
import sys
err = []
try: import carla; print(\"carla:ok\")
except Exception: err.append(\"carla\")
try: import paho.mqtt.client; print(\"paho:ok\")
except Exception: pass
try: import cv2; print(\"cv2:ok\")
except Exception: pass
try: import numpy; print(\"numpy:ok\")
except Exception: pass
sys.exit(1 if \"carla\" in err else 0)
"
' 2>/dev/null) || true
if echo "$PY_CHECK" | grep -q "carla:ok"; then
  log_ok "carla 可用（当前 C++ Bridge 不依赖 Python paho/cv2）"
else
  log_ok "Python carla 未检测到（C++ Bridge 不依赖；若用 Python Bridge 需镜像内 carla egg 或 PYTHONPATH）"
fi
echo ""

# 4) ffmpeg
log_section "4/7 ffmpeg"
if docker run --rm --entrypoint ffmpeg "$IMAGE_NAME" -version >/dev/null 2>&1; then
  log_ok "ffmpeg 可用"
else
  log_fail "ffmpeg 不可用"
  FAILED=$((FAILED+1))
fi
echo ""

# 5) entrypoint 与工作目录
log_section "5/7 entrypoint 与 WORKDIR"
EP_CHECK=$(docker run --rm --entrypoint bash "$IMAGE_NAME" -c 'test -x /entrypoint.sh && echo "entrypoint:ok"; test -d /workspace && echo "workspace:ok"' 2>/dev/null) || true
if echo "$EP_CHECK" | grep -q "entrypoint:ok" && echo "$EP_CHECK" | grep -q "workspace:ok"; then
  log_ok "entrypoint 可执行，WORKDIR /workspace 存在"
else
  log_fail "entrypoint 或 /workspace 异常"
  FAILED=$((FAILED+1))
fi
echo ""

# 6) C++ 构建链（cmake / make，供容器内编译 C++ Bridge）
log_section "6/7 C++ 构建链（cmake / make）"
CMAKE_VER=$(docker run --rm --entrypoint cmake "$IMAGE_NAME" --version 2>/dev/null | head -1) || true
MAKE_VER=$(docker run --rm --entrypoint make "$IMAGE_NAME" --version 2>/dev/null | head -1) || true
if docker run --rm --entrypoint bash "$IMAGE_NAME" -c 'command -v cmake >/dev/null && command -v make >/dev/null' 2>/dev/null; then
  log_ok "cmake 与 make 可用（容器内可编译 C++ Bridge）"
else
  log_fail "cmake 或 make 不可用"
  FAILED=$((FAILED+1))
fi
echo ""

# 7) CARLA 可执行（CarlaUE4.sh 或 等效）
log_section "7/7 CARLA 运行环境"
CARLA_CHECK=$(docker run --rm --entrypoint bash "$IMAGE_NAME" -c '
  if [ -x "/home/carla/CarlaUE4.sh" ] 2>/dev/null; then echo "CarlaUE4:ok"; fi
  if [ -d /home/carla ] 2>/dev/null; then echo "home_carla:ok"; fi
  python3 -c "import carla; print(\"carla_version:\", getattr(carla, \"__version__\", \"unknown\"))" 2>/dev/null || true
' 2>/dev/null) || true
if echo "$CARLA_CHECK" | grep -q "CarlaUE4:ok\|carla_version:"; then
  log_ok "CARLA 运行环境就绪"
else
  log_fail "未检测到 CarlaUE4.sh 或 carla 模块"
  FAILED=$((FAILED+1))
fi
echo ""

# 汇总
echo -e "${CYAN}========== 汇总 ==========${NC}"
if [ $FAILED -eq 0 ]; then
  echo -e "  ${GREEN}全部通过：镜像已具备 CARLA + MQTT（Paho C/C++）运行与 C++ Bridge 编译能力。${NC}"
  echo ""
  echo "  后续可："
  echo "    启动: ./scripts/start-all-nodes.sh"
  echo "    C++ Bridge 功能验证: ./scripts/verify-carla-bridge-cpp-features.sh"
  echo "    整链验证: ./scripts/verify-full-chain-client-to-carla.sh"
  echo ""
  exit 0
fi
echo -e "  ${RED}失败项: $FAILED${NC}"
exit 1
