#!/bin/bash
# CARLA相机诊断脚本 - 排查真实画面问题
# 用法: ./scripts/diagnose-carla-camera.sh

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}  CARLA 相机诊断工具 v1.0${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""

# 1. 检查CARLA容器状态
echo -e "${YELLOW}[1/7] 检查CARLA容器状态${NC}"
CONTAINER_NAME="${CONTAINER_NAME:-carla-server}"
if docker ps --format '{{.Names}}' | grep -q "^${CONTAINER_NAME}$"; then
    echo -e "${GREEN}✓${NC} 容器 ${CONTAINER_NAME} 运行中"
else
    echo -e "${RED}✗${NC} 容器 ${CONTAINER_NAME} 未运行"
    exit 1
fi

# 2. 检查GPU可见性
echo ""
echo -e "${YELLOW}[2/7] 检查GPU可见性${NC}"
GPU_CHECK=$(docker exec ${CONTAINER_NAME} nvidia-smi 2>/dev/null)
if [ $? -eq 0 ]; then
    echo -e "${GREEN}✓${NC} NVIDIA GPU 可见"
    echo "$GPU_CHECK" | head -5
else
    echo -e "${RED}✗${NC} NVIDIA GPU 不可见 (nvidia-smi 失败)"
    echo -e "${YELLOW}提示:${NC} 检查 docker-compose.yml 中是否配置 'runtime: nvidia'"
fi

# 3. 检查渲染模式
echo ""
echo -e "${YELLOW}[3/7] 检查渲染模式${NC}"
RENDER_MODE=$(docker logs ${CONTAINER_NAME} 2>&1 | grep -E "渲染模式|opengl|RenderOffScreen" | tail -3)
if [ -n "$RENDER_MODE" ]; then
    echo -e "${GREEN}✓${NC} 渲染模式:"
    echo "$RENDER_MODE" | sed 's/^/   /'
else
    echo -e "${YELLOW}?${NC} 未检测到明确的渲染模式信息"
fi

# 4. 检查EGL
echo ""
echo -e "${YELLOW}[4/7] 检查EGL配置${NC}"
EGL_CHECK=$(docker exec ${CONTAINER_NAME} bash -c "ls -la /usr/local/lib/libEGL* /usr/local/lib/libGLES* 2>/dev/null || echo 'EGL libraries not found'" 2>/dev/null || true)
if echo "$EGL_CHECK" | grep -q "libEGL"; then
    echo -e "${GREEN}✓${NC} EGL 库存在"
else
    echo -e "${RED}✗${NC} EGL 库可能缺失"
fi

# 5. 检查相机Spawn状态
echo ""
echo -e "${YELLOW}[5/7] 检查相机Spawn状态${NC}"
CAMERA_SPAWN=$(docker logs ${CONTAINER_NAME} 2>&1 | grep -E "spawn.*camera|Spawn.*camera|cam_front|cam_rear|spawn_actor camera" | tail -10)
if [ -n "$CAMERA_SPAWN" ]; then
    echo -e "${GREEN}✓${NC} 相机Spawn日志:"
    echo "$CAMERA_SPAWN" | sed 's/^/   /'
else
    echo -e "${YELLOW}?${NC} 未检测到明确的相机Spawn日志"
fi

# 6. 检查帧到达率
echo ""
echo -e "${YELLOW}[6/7] 检查帧到达率${NC}"
FPS_CHECK=$(docker logs ${CONTAINER_NAME} 2>&1 | grep -E "fps|帧到达|arrival" | tail -10)
if [ -n "$FPS_CHECK" ]; then
    echo -e "${GREEN}✓${NC} 帧率日志:"
    echo "$FPS_CHECK" | sed 's/^/   /'
else
    echo -e "${YELLOW}?${NC} 未检测到帧率日志"
fi

# 7. 检查推流状态
echo ""
echo -e "${YELLOW}[7/7] 检查推流状态${NC}"
STREAM_CHECK=$(docker logs ${CONTAINER_NAME} 2>&1 | grep -E "testsrc|CARLA 相机|推流|rtmp|ffmpeg" | tail -10)
if [ -n "$STREAM_CHECK" ]; then
    echo -e "${GREEN}✓${NC} 推流日志:"
    echo "$STREAM_CHECK" | sed 's/^/   /'
else
    echo -e "${YELLOW}?${NC} 未检测到推流日志"
fi

# 检查testsrc降级
echo ""
echo -e "${YELLOW}[额外检查] 检查是否降级到testsrc${NC}"
TESTSRC_CHECK=$(docker logs ${CONTAINER_NAME} 2>&1 | grep -E "testsrc|降级|FALLBACK" | tail -5)
if echo "$TESTSRC_CHECK" | grep -qi "testsrc"; then
    echo -e "${RED}✗${NC} 检测到testsrc降级 - CARLA相机可能有问题:"
    echo "$TESTSRC_CHECK" | sed 's/^/   /'
else
    echo -e "${GREEN}✓${NC} 未检测到testsrc降级"
fi

# 诊断总结
echo ""
echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}  诊断总结${NC}"
echo -e "${BLUE}========================================${NC}"

# GPU问题
if ! docker exec ${CONTAINER_NAME} nvidia-smi >/dev/null 2>&1; then
    echo -e "${RED}❌ GPU不可见${NC}"
    echo "   建议: 检查 NVIDIA Container Toolkit 安装和 docker-compose 配置"
    echo "   尝试: 确保 docker-compose.carla.yml 中有 'runtime: nvidia'"
fi

# EGL问题
if ! docker exec ${CONTAINER_NAME} bash -c "ldconfig -p | grep -q libEGL" 2>/dev/null; then
    echo -e "${RED}❌ EGL库缺失${NC}"
    echo "   建议: 在 Dockerfile 中添加 EGL 库安装"
fi

# 相机spawn问题
if ! docker logs ${CONTAINER_NAME} 2>&1 | grep -qE "spawn.*camera|Spawn.*camera"; then
    echo -e "${RED}❌ 相机未Spawn${NC}"
    echo "   建议: 检查 USE_PYTHON_BRIDGE=1 和 CARLA_PY3_OK=1"
fi

# testsrc降级
if docker logs ${CONTAINER_NAME} 2>&1 | grep -qE "testsrc|降级"; then
    echo -e "${RED}❌ 使用了testsrc降级${NC}"
    echo "   原因: CARLA相机渲染失败"
    echo "   建议: "
    echo "   1. 确保 GPU 正确配置 (nvidia-smi 可用)"
    echo "   2. 设置 USE_TESTSRC_FALLBACK=0 查看真实错误"
    echo "   3. 检查 CARLA_GPU_RENDER_MODE=offscreen"
fi

echo ""
echo -e "${GREEN}诊断完成${NC}"
echo "查看完整日志: docker logs ${CONTAINER_NAME} 2>&1 | tail -100"
