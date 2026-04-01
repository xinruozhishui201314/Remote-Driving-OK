#!/usr/bin/env bash
# Vehicle-side 构建脚本
#
# 构建上下文 = 项目根目录 (.)，Dockerfile 在 Vehicle-side/ 子目录下。
# COPY 路径全部相对于项目根目录，deps/ 和 Vehicle-side/ 都在根目录下。
#
# 用法：
#   ./scripts/build-vehicle.sh dev            # dev 模式（镜像最新则跳过构建）
#   ./scripts/build-vehicle.sh prod           # prod 模式
#   ./scripts/build-vehicle.sh dev --force-rebuild  # 强制重新构建
#   ./scripts/build-vehicle.sh dev --no-cache # 强制全量重建（跳过缓存）

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
CYAN='\033[0;36m'
NC='\033[0m'

MODE="${1:-dev}"
IMAGE_TAG="remote-driving-vehicle-test"

echo -e "${CYAN}========================================${NC}"
echo -e "${CYAN} Vehicle-side Build Script${NC}"
echo -e "${CYAN} Mode: $MODE${NC}"
echo -e "${CYAN} Date: $(date '+%Y-%m-%d %H:%M:%S')${NC}"
echo -e "${CYAN}========================================${NC}"

echo -e "${YELLOW}[信息] 检查环境...${NC}"
echo -e "  - Docker 版本: $(docker --version 2>/dev/null || echo '未安装')${NC}"
echo -e "  - 构建上下文: $PWD（项目根目录）${NC}"
echo -e "  - Dockerfile:  $DOCKERFILE${NC}"
echo -e "  - 镜像标签:    $IMAGE_TAG${NC}"
echo ""

case "$MODE" in
  dev)
    DOCKERFILE="Vehicle-side/Dockerfile.dev"
    echo -e "${YELLOW}[信息] 使用 Dockerfile.dev${NC}"
    ;;
  prod)
    DOCKERFILE="Vehicle-side/Dockerfile.prod"
    echo -e "${YELLOW}[信息] 使用 Dockerfile.prod${NC}"
    ;;
  *)
    echo -e "${RED}[错误] 未知模式: $MODE${NC}"
    exit 1
    ;;
esac

if [ ! -f "$DOCKERFILE" ]; then
  echo -e "${RED}[错误] Dockerfile 不存在: $DOCKERFILE${NC}"
  exit 1
fi

echo -e "${GREEN}[信息] Dockerfile 验证通过 ✓${NC}"
echo ""

# ── 检测是否强制重建 ──────────────────────────────────────────────────────────
FORCE_REBUILD=false
SKIP_REASON=""
if [[ " $@ " =~ " --force-rebuild " ]] || [[ " $@ " =~ " --no-cache " ]]; then
  FORCE_REBUILD=true
  SKIP_REASON="强制重建（--force-rebuild / --no-cache）"
fi

# ── 检测镜像是否存在 ─────────────────────────────────────────────────────────
echo -e "${YELLOW}[镜像检测]${NC}"
IMAGE_EXISTS=false
if docker image inspect "$IMAGE_TAG" > /dev/null 2>&1; then
  IMAGE_EXISTS=true
  IMAGE_ID=$(docker image inspect --format='{{.Id}}' "$IMAGE_TAG")
  IMAGE_CREATED=$(docker image inspect --format='{{.Created}}' "$IMAGE_TAG")
  echo -e "  ✓ 镜像已存在: ${CYAN}$IMAGE_TAG${NC}"
  echo -e "    ID:      $IMAGE_ID"
  echo -e "    构建于:  $IMAGE_CREATED"
else
  echo -e "  ✗ 镜像不存在: ${CYAN}$IMAGE_TAG${NC}"
fi
echo ""

# ── 检测源码/依赖是否变化（仅在镜像存在时有意义）────────────────────────────
SOURCE_STALE=""
if [ "$IMAGE_EXISTS" = true ] && [ "$FORCE_REBUILD" = false ]; then
  echo -e "${YELLOW}[源码变更检测]${NC}"

  # 比对镜像构建时间 vs Dockerfile / 关键源码修改时间
  IMAGE_MTIME=$(docker image inspect --format='{{.Created}}' "$IMAGE_TAG" | xargs -I{} date -d "{}" +%s 2>/dev/null || echo 0)

  DEPS_MTIME=$(find deps/ Vehicle-side/src Vehicle-side/CMakeLists.txt -type f -newer /dev/null 2>/dev/null | head -1 | xargs -I{} stat -L --format='%Y' {} 2>/dev/null || echo 0)

  # 找最晚修改的源码文件
  NEWEST_SOURCE=$(find deps/ Vehicle-side/src Vehicle-side/CMakeLists.txt -type f 2>/dev/null | \
    xargs -I{} stat -L --format='%Y {}' {} 2>/dev/null | sort -rn | head -1 | awk '{print $1}')

  if [ -n "$NEWEST_SOURCE" ] && [ "$NEWEST_SOURCE" -gt "$IMAGE_MTIME" ] 2>/dev/null; then
    SOURCE_STALE="源码/依赖于 $(date -d "@$NEWEST_SOURCE" '+%Y-%m-%d %H:%M' 2>/dev/null || echo "未知时间") 更新，镜像构建于 $(date -d "@$IMAGE_MTIME" '+%Y-%m-%d %H:%M' 2>/dev/null || echo "未知时间")"
    echo -e "  ✗ 源码已过期: $SOURCE_STALE"
  else
    echo -e "  ✓ 源码未变化"
  fi
  echo ""
fi

# ── 判断是否跳过构建 ─────────────────────────────────────────────────────────
if [ "$FORCE_REBUILD" = true ]; then
  echo -e "${YELLOW}[跳过构建] $SKIP_REASON${NC}"
elif [ "$IMAGE_EXISTS" = true ] && [ -z "$SOURCE_STALE" ]; then
  echo -e "${GREEN}========================================${NC}"
  echo -e "${GREEN}  镜像已是最新，跳过构建 ✓${NC}"
  echo -e "${GREEN}  镜像: $IMAGE_TAG${NC}"
  echo -e "${GREEN}  如需强制重建，请加 --force-rebuild${NC}"
  echo -e "${GREEN}========================================${NC}"
  exit 0
else
  echo -e "${YELLOW}[继续构建]${NC}"
  if [ "$IMAGE_EXISTS" = false ]; then
    echo -e "  原因: 镜像不存在"
  else
    echo -e "  原因: $SOURCE_STALE"
  fi
fi
echo ""

# ── 透传参数 ─────────────────────────────────────────────────────────────────
EXTRA_ARGS=()
for arg in "$@"; do
  [[ "$arg" != "dev" && "$arg" != "prod" && "$arg" != "--force-rebuild" ]] && EXTRA_ARGS+=("$arg")
done

echo -e "${CYAN}[开始构建]${NC}"
echo -e "${CYAN}========================================${NC}"

# context=.（项目根目录），与 docker-compose.vehicle.dev.yml 保持一致
DOCKER_BUILDKIT=1 docker build \
    --progress=plain \
    -f "$DOCKERFILE" \
    -t "$IMAGE_TAG" \
    . \
    "${EXTRA_ARGS[@]}"

BUILD_RESULT=$?

if [ $BUILD_RESULT -eq 0 ]; then
    echo ""
    echo -e "${GREEN}========================================${NC}"
    echo -e "${GREEN} 构建成功! ✓${NC}"
    echo -e "${GREEN} 镜像: $IMAGE_TAG${NC}"
    echo -e "${GREEN} 运行命令: docker run $IMAGE_TAG${NC}"
    echo -e "${GREEN}========================================${NC}"
else
    echo ""
    echo -e "${RED}========================================${NC}"
    echo -e "${RED} 构建失败! ✗ (退出码: $BUILD_RESULT)${NC}"
    echo -e "${RED} 请查看上方日志定位问题${NC}"
    echo -e "${RED}========================================${NC}"
    exit $BUILD_RESULT
fi
