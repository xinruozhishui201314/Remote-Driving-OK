#!/usr/bin/env bash
# 在宿主机上安装 NVIDIA Container Toolkit，使 Docker 容器可使用 GPU（CARLA 等依赖 GPU 的镜像才能正常启动）。
# 官方文档: https://docs.nvidia.com/datacenter/cloud-native/container-toolkit/install-guide.html
#
# 用法：sudo ./scripts/install-nvidia-container-toolkit.sh
# 或：bash scripts/install-nvidia-container-toolkit.sh（脚本内会提示 sudo）

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# 需 root 执行
if [ "$(id -u)" -ne 0 ]; then
  echo "请使用 root 执行（或 sudo）: sudo $0"
  exit 1
fi

echo "========== 安装 NVIDIA Container Toolkit（宿主机）=========="
echo ""

# 1. 检查 NVIDIA 驱动
if ! command -v nvidia-smi >/dev/null 2>&1; then
  echo "[FAIL] 未检测到 nvidia-smi，请先安装 NVIDIA 驱动。"
  echo "  Ubuntu: sudo apt install nvidia-driver-XXX 或从 NVIDIA 官网下载安装"
  exit 1
fi
echo "[OK] NVIDIA 驱动: $(nvidia-smi --query-gpu=driver_version --format=csv,noheader 2>/dev/null | head -1)"
echo ""

# 2. 检查 Docker
if ! command -v docker >/dev/null 2>&1; then
  echo "[FAIL] 未检测到 Docker，请先安装 Docker。"
  exit 1
fi
echo "[OK] Docker 已安装"
echo ""

# 3. 若已安装 nvidia-container-toolkit，仅配置并重启
if command -v nvidia-ctk >/dev/null 2>&1 || dpkg -l nvidia-container-toolkit 2>/dev/null | grep -q '^ii'; then
  echo "[OK] nvidia-container-toolkit 已安装，正在配置 Docker 并重启..."
  nvidia-ctk runtime configure --runtime=docker 2>/dev/null || true
  systemctl restart docker 2>/dev/null || true
  echo ""
  echo "验证: docker run --rm --runtime=nvidia nvidia/cuda:12.0-base-ubuntu22.04 nvidia-smi"
  exit 0
fi

# 4. 添加仓库并安装（Ubuntu/Debian）
echo "添加 NVIDIA Container Toolkit 仓库并安装..."
mkdir -p /usr/share/keyrings
curl -fsSL https://nvidia.github.io/libnvidia-container/gpgkey | gpg --dearmor -o /usr/share/keyrings/nvidia-container-toolkit-keyring.gpg
curl -s -L https://nvidia.github.io/libnvidia-container/stable/deb/nvidia-container-toolkit.list | \
  sed 's#deb https://#deb [signed-by=/usr/share/keyrings/nvidia-container-toolkit-keyring.gpg] https://#g' | \
  tee /etc/apt/sources.list.d/nvidia-container-toolkit.list

apt-get update -qq
apt-get install -y nvidia-container-toolkit

# 5. 配置 Docker 使用 nvidia runtime
echo ""
echo "配置 Docker runtime..."
nvidia-ctk runtime configure --runtime=docker

# 6. 重启 Docker
echo "重启 Docker..."
systemctl restart docker

echo ""
echo "========== 安装完成 =========="
echo "请运行以下命令验证（应能看到 GPU 信息）："
echo "  docker run --rm --runtime=nvidia nvidia/cuda:12.0-base-ubuntu22.04 nvidia-smi"
echo ""
echo "验证通过后，再执行 ./scripts/start-all-nodes.sh 启动 CARLA 容器。"
echo ""
