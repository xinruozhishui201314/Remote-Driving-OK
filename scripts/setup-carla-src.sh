#!/usr/bin/env bash
# 将 CARLA 官方仓库（GitHub 最新代码）克隆到工程目录 carla-src/，供容器挂载使用。
# 宿主机需已安装 git；容器启动时会挂载 ./carla-src 到 /opt/carla-src，entrypoint 优先使用其中 PythonAPI。
#
# 用法：
#   ./scripts/setup-carla-src.sh              # 克隆到项目根目录下的 carla-src/
#   CARLA_SRC_BRANCH=0.9.15 ./scripts/setup-carla-src.sh   # 指定分支或 tag

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

CARLA_REPO="${CARLA_REPO:-https://github.com/carla-simulator/carla.git}"
CARLA_SRC_DIR="${CARLA_SRC_DIR:-$PROJECT_ROOT/carla-src}"
# 与 carlasim/carla 镜像（UE 4.26）匹配时建议用 0.9.13；ue5-dev 需 UE5 且 Ubuntu 22.04
CARLA_SRC_BRANCH="${CARLA_SRC_BRANCH:-0.9.13}"

echo "========== 克隆 CARLA 源码到工程目录（供容器挂载，使用 GPU 加速）=========="
echo "  目标目录: $CARLA_SRC_DIR"
echo "  分支/tag: $CARLA_SRC_BRANCH"
echo ""

if [ -d "$CARLA_SRC_DIR/.git" ]; then
  echo "  carla-src 已存在，执行 git fetch + pull 更新为最新..."
  echo "  [$(date '+%Y-%m-%d %H:%M:%S')] 开始拉取..."
  (cd "$CARLA_SRC_DIR" && git fetch --progress origin 2>&1 && (git checkout "$CARLA_SRC_BRANCH" 2>/dev/null || git checkout main 2>/dev/null || true) && (git pull --rebase 2>&1 || git pull 2>&1 || true))
  echo "  [$(date '+%Y-%m-%d %H:%M:%S')] 更新完成。"
else
  if [ -d "$CARLA_SRC_DIR" ] && [ ! -w "$CARLA_SRC_DIR" ]; then
    echo "  错误: 目录已存在但当前用户无写权限（可能由 root/Docker 创建）。"
    echo "  请执行以下之一后重试："
    echo "    sudo chown -R \$(whoami):\$(whoami) $CARLA_SRC_DIR"
    echo "    或: sudo rm -rf $CARLA_SRC_DIR"
    exit 1
  fi
  echo "  克隆 CARLA 仓库（可能较久）..."
  echo "  [$(date '+%Y-%m-%d %H:%M:%S')] 开始克隆 (--depth 1 --branch $CARLA_SRC_BRANCH)..."
  # 立即显示进度；不重定向 stderr，让 git 进度直接输出到终端
  export GIT_PROGRESS_DELAY=0
  run_first_clone() {
    git clone --progress --depth 1 --branch "$CARLA_SRC_BRANCH" "$CARLA_REPO" "$CARLA_SRC_DIR"
  }
  clone_ok=0
  if [ -t 2 ]; then
    run_first_clone; r=$?
    [ $r -eq 0 ] && clone_ok=1
  else
    # 非 TTY 时 git 常不输出进度，用后台任务按目录大小打印“已下载约 X MiB”
    (while true; do
      sleep 3
      [ -d "$CARLA_SRC_DIR/.git" ] && break
      sz=$(du -sm "$CARLA_SRC_DIR" 2>/dev/null | cut -f1)
      [ -n "$sz" ] && echo "  [$(date '+%H:%M:%S')] 已下载约 ${sz} MiB ..."
    done) &
    prog_pid=$!
    run_first_clone; r=$?
    [ $r -eq 0 ] && clone_ok=1
    kill $prog_pid 2>/dev/null || true
    wait $prog_pid 2>/dev/null || true
  fi
  if [ "$clone_ok" -ne 1 ]; then
    echo "  [$(date '+%Y-%m-%d %H:%M:%S')] 按分支克隆失败，尝试仅克隆默认分支再 checkout..."
    if ! git clone --progress --depth 1 "$CARLA_REPO" "$CARLA_SRC_DIR"; then
      echo "  错误: git clone 失败，请检查网络与权限。"
      exit 1
    fi
    echo "  [$(date '+%Y-%m-%d %H:%M:%S')] 拉取分支 $CARLA_SRC_BRANCH ..."
    (cd "$CARLA_SRC_DIR" && git fetch --progress --depth 1 origin "$CARLA_SRC_BRANCH"; git checkout "$CARLA_SRC_BRANCH" 2>/dev/null || true)
  fi
  echo "  [$(date '+%Y-%m-%d %H:%M:%S')] 克隆完成。"
fi

if [ -d "$CARLA_SRC_DIR/PythonAPI/carla" ]; then
  echo ""
  echo "  PythonAPI 路径: $CARLA_SRC_DIR/PythonAPI/carla（容器内将挂载到 /opt/carla-src，entrypoint 会优先使用）"
else
  echo ""
  echo "  注意: 未发现 PythonAPI/carla，请确认分支或手动拉取完整仓库（去掉 --depth 1 再 clone）。"
fi
echo ""
echo "  启动容器时已配置挂载: ./carla-src -> /opt/carla-src（docker-compose.carla.yml）"
echo "  使用 GPU: 宿主机需 nvidia-smi 与 nvidia-container-toolkit，Compose 已配置 runtime: nvidia。"
echo ""
