#!/usr/bin/env bash
# 从 ZLM 拉四路 RTMP，并排 ffplay 预览 + 可选 ffmpeg 落盘到 logs/。
#
# 前置：全链路已推流（如 CARLA Bridge 已收 start_stream），宿主机可访问 RTMP 端口。
# 流名规则与 carla-bridge 一致：rtmp://HOST:PORT/APP/{VIN}_cam_{front,rear,left,right}
#   默认 VIN=carla-sim-001（与 docker-compose.carla.yml 中 CARLA_VIN 一致）。
#
# Compose / 环境变量（与仿真对齐，按需 export 后启动 compose）：
#   CARLA_VIN=carla-sim-001          # 必须与下面 --vin 一致
#   ZLM_RTMP_PORT=1935               # 宿主机映射 1935:1935 时从本机拉流用 127.0.0.1:1935
#   ZLM_APP=teleop                   # 默认 teleop
#
# 用法：
#   bash scripts/zlm-four-cam-preview-record.sh
#   bash scripts/zlm-four-cam-preview-record.sh --record-only --duration 30
#   bash scripts/zlm-four-cam-preview-record.sh --play-only
#   VIN=my-vin bash scripts/zlm-four-cam-preview-record.sh --vin my-vin
#
# 依赖：ffmpeg（含 ffplay），宿主机需图形环境（--play-only / 默认预览时）。
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

# shellcheck disable=SC1091
source "$SCRIPT_DIR/lib/teleop-logs.sh"
teleop_logs_init
teleop_logs_init_run_subdir

VIN="${CARLA_VIN:-carla-sim-001}"
ZLM_HOST="${ZLM_RTMP_HOST:-127.0.0.1}"
ZLM_PORT="${ZLM_RTMP_PORT:-1935}"
ZLM_APP="${ZLM_APP:-teleop}"
DURATION=0
DO_PLAY=1
DO_RECORD=1
SUBDIR="zlm-four-cam"

while [ $# -gt 0 ]; do
  case "$1" in
    --vin) VIN="$2"; shift 2 ;;
    --host) ZLM_HOST="$2"; shift 2 ;;
    --port) ZLM_PORT="$2"; shift 2 ;;
    --app) ZLM_APP="$2"; shift 2 ;;
    --duration) DURATION="$2"; shift 2 ;;
    --play-only) DO_RECORD=0; shift ;;
    --record-only) DO_PLAY=0; shift ;;
    --subdir) SUBDIR="$2"; shift 2 ;;
    -h|--help)
      sed -n '1,35p' "$0"
      exit 0
      ;;
    *) echo "未知参数: $1" >&2; exit 1 ;;
  esac
done

if ! command -v ffmpeg >/dev/null 2>&1; then
  echo "[zlm-four-cam] 需要 ffmpeg（含 ffplay）：sudo apt-get install -y ffmpeg" >&2
  exit 1
fi

PREFIX="${VIN}_"
OUT_BASE="${TELEOP_LOGS_RUN_DIR:-$TELEOP_LOGS_DIR}/${SUBDIR}-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$OUT_BASE"

streams=(cam_front cam_rear cam_left cam_right)
urls=()
for s in "${streams[@]}"; do
  urls+=("rtmp://${ZLM_HOST}:${ZLM_PORT}/${ZLM_APP}/${PREFIX}${s}")
done

echo "[zlm-four-cam] 输出目录: $OUT_BASE"
echo "[zlm-four-cam] VIN 前缀流名: ${PREFIX}cam_* （若 404/失败请检查 CARLA_VIN 与是否已 start_stream）"
for i in 0 1 2 3; do
  echo "  [$i] ${urls[$i]}"
done

pids_play=()
pids_rec=()

cleanup() {
  local p
  for p in "${pids_play[@]+"${pids_play[@]}"}"; do kill "$p" 2>/dev/null || true; done
  for p in "${pids_rec[@]+"${pids_rec[@]}"}"; do kill "$p" 2>/dev/null || true; done
  for p in "${pids_rec[@]+"${pids_rec[@]}"}"; do wait "$p" 2>/dev/null || true; done
}
trap cleanup EXIT INT TERM

# 2x2 粗略排布（640x360 窗口，可按分辨率改 -x/-y）
geom=(
  "0:0:640:360"
  "640:0:640:360"
  "0:380:640:360"
  "640:380:640:360"
)

titles=(front rear left right)

if [ "$DO_PLAY" -eq 1 ]; then
  if [ -z "${DISPLAY:-}" ] && [ -z "${WAYLAND_DISPLAY:-}" ]; then
    echo "[zlm-four-cam] 警告: 无 DISPLAY/WAYLAND，跳过 ffplay（仅用 --record-only 可离屏录屏）" >&2
    DO_PLAY=0
  fi
fi

for i in 0 1 2 3; do
  IFS=':' read -r gl gt gw gh <<< "${geom[$i]}"
  sname="${streams[$i]}"
  tname="${titles[$i]}"
  if [ "$DO_PLAY" -eq 1 ]; then
    ffplay -loglevel error \
      -window_title "ZLM ${tname} (${PREFIX}${sname})" \
      -left "$gl" -top "$gt" -x "$gw" -y "$gh" \
      -fflags nobuffer -flags low_delay \
      "${urls[$i]}" &
    pids_play+=($!)
  fi

  if [ "$DO_RECORD" -eq 1 ]; then
    out="${OUT_BASE}/${sname}.mkv"
    if [ "$DURATION" -gt 0 ]; then
      ffmpeg -loglevel warning -rw_timeout 15000000 \
        -i "${urls[$i]}" -t "$DURATION" -c copy -y "$out" &
    else
      ffmpeg -loglevel warning -rw_timeout 15000000 \
        -i "${urls[$i]}" -c copy -y "$out" &
    fi
    pids_rec+=($!)
  fi
done

if [ "$DO_RECORD" -eq 1 ]; then
  meta="${OUT_BASE}/README.txt"
  {
    echo "captured_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "vin=$VIN"
    echo "rtmp_host=$ZLM_HOST rtmp_port=$ZLM_PORT app=$ZLM_APP"
    for i in 0 1 2 3; do
      echo "stream_${streams[$i]}=${urls[$i]}"
    done
    if [ "$DURATION" -gt 0 ]; then
      echo "duration_sec=$DURATION"
    else
      echo "duration_sec=unlimited(Ctrl+C 结束)"
    fi
  } >"$meta"
  echo "[zlm-four-cam] 正在录制 → $OUT_BASE/*.mkv"
fi

if [ "$DO_PLAY" -eq 1 ]; then
  echo "[zlm-four-cam] ffplay 已启动；关闭四个预览窗口或 Ctrl+C 结束。"
fi

if [ "$DURATION" -gt 0 ] && [ "$DO_RECORD" -eq 1 ]; then
  echo "[zlm-four-cam] 等待录制 ${DURATION}s …"
  wait "${pids_rec[@]}"
  for p in "${pids_play[@]+"${pids_play[@]}"}"; do kill "$p" 2>/dev/null || true; done
  echo "[zlm-four-cam] 录制完成: $OUT_BASE"
  trap - EXIT INT TERM
  exit 0
fi

# 无限录制或仅播放：等待用户中断（关窗口或 Ctrl+C）
if [ "$DO_PLAY" -eq 1 ] && [ ${#pids_play[@]} -gt 0 ]; then
  wait "${pids_play[@]}"
elif [ "$DO_RECORD" -eq 1 ] && [ "$DURATION" -eq 0 ] && [ ${#pids_rec[@]} -gt 0 ]; then
  echo "[zlm-four-cam] 仅录制模式：Ctrl+C 结束"
  wait "${pids_rec[@]}"
fi
