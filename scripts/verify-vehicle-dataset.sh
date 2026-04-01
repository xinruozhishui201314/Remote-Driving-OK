#!/bin/bash
# 验证车端「边推流边读数据集」所需的数据集目录：结构与 push-nuscenes-cameras-to-zlm.sh 一致
#
# 推荐：先在 Vehicle-side 运行的容器中进行本地校验（与推流时环境一致）：
#   make verify-vehicle-dataset-local
# 需先启动 vehicle 并挂载数据集卷（如 docker-compose.vehicle.dev.yml 中 - /path/to/sweeps:/data/sweeps:ro）。
#
# 宿主机校验：SWEEPS_PATH=/path/to/nuscenes-mini/sweeps make verify-vehicle-dataset
# 容器内直接执行：SWEEPS_PATH=/data/sweeps bash /app/scripts/verify-vehicle-dataset.sh

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

SWEEPS_PATH="${SWEEPS_PATH:-$HOME/bigdata/data/nuscenes-mini/sweeps}"
# 与 push-nuscenes-cameras-to-zlm.sh 一致
declare -A CAM_MAP=(
  ["cam_front"]="CAM_FRONT"
  ["cam_rear"]="CAM_BACK"
  ["cam_left"]="CAM_FRONT_LEFT"
  ["cam_right"]="CAM_FRONT_RIGHT"
)

echo "=== 车端数据集读取验证（边推流边读数据集）==="
echo "  SWEEPS_PATH=$SWEEPS_PATH"
echo ""

FAIL=0

if [[ ! -d "$SWEEPS_PATH" ]]; then
  echo "VERIFY_FAIL: SWEEPS_PATH 不存在或非目录: $SWEEPS_PATH"
  echo "  请挂载 nuscenes-mini/sweeps 并设置 SWEEPS_PATH（如 /data/sweeps）"
  exit 1
fi
echo "[1/3] SWEEPS_PATH 目录存在 ✓"

for stream_id in cam_front cam_rear cam_left cam_right; do
  cam_dir="${CAM_MAP[$stream_id]}"
  cam_path="${SWEEPS_PATH}/${cam_dir}"
  if [[ ! -d "$cam_path" ]]; then
    echo "VERIFY_FAIL: 缺少相机目录: $cam_path"
    FAIL=1
    continue
  fi
  # 至少有一张图（jpg/jpeg/png）便于推流脚本可读
  count=$(find "$cam_path" -maxdepth 1 -type f \( -iname '*.jpg' -o -iname '*.jpeg' -o -iname '*.png' \) 2>/dev/null | wc -l)
  if [[ "$count" -lt 1 ]]; then
    echo "VERIFY_FAIL: 相机目录下无图片: $cam_path (需至少 1 张 .jpg/.png)"
    FAIL=1
  else
    echo "  $stream_id ($cam_dir): $count 张图片 ✓"
  fi
done

if [[ $FAIL -ne 0 ]]; then
  echo ""
  echo "VERIFY_FAIL: 数据集结构不完整，无法用于 push-nuscenes-cameras-to-zlm.sh"
  echo "  预期结构: SWEEPS_PATH/{CAM_FRONT,CAM_BACK,CAM_FRONT_LEFT,CAM_FRONT_RIGHT}/*.jpg"
  exit 1
fi

echo ""
echo "[2/3] 四路相机目录及图片检查通过 ✓"

if ! command -v ffmpeg &>/dev/null; then
  echo "[3/3] VERIFY_WARN: 未找到 ffmpeg，推流脚本将无法执行（车端镜像已装可忽略）"
else
  echo "[3/3] ffmpeg 可用 ✓"
fi

echo ""
echo "VERIFY_OK: 车端数据集读取验证通过，可配置 VEHICLE_PUSH_SCRIPT=push-nuscenes-cameras-to-zlm.sh 边读边推"
exit 0
