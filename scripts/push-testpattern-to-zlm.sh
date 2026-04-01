#!/bin/bash
# 无数据集时用测试图案向 ZLMediaKit 推四路流，便于 E2E 验证（app=teleop, stream=cam_front/cam_rear/cam_left/cam_right）
# 不依赖 SWEEPS_PATH，仅需 ffmpeg

set -e
# 与客户端 [Client][WebRTC] 对齐：车端 RTMP 推流到 ZLM 的统一 grep 前缀
vpush_log() { echo "[Vehicle-side][ZLM][Push] $*" >&2; }

ZLM_HOST="${ZLM_HOST:-127.0.0.1}"
ZLM_RTMP_PORT="${ZLM_RTMP_PORT:-1935}"
APP="${ZLM_APP:-teleop}"
FPS="${TESTPATTERN_FPS:-10}"
SIZE="${TESTPATTERN_SIZE:-640x480}"
# 多车隔离：流名加 VIN 前缀，格式 {VIN}_cam_front；VIN 为空时不加前缀（兼容单车测试）
VIN="${VEHICLE_VIN:-${VIN:-}}"
VIN_PREFIX="${VIN:+${VIN}_}"

# ★ PID 文件管理（确保推流进程唯一性，避免重复启动）
PIDFILE_DIR="${PIDFILE_DIR:-/tmp}"
PIDFILE="${PIDFILE_DIR}/push-testpattern.pid"
LOCKFILE="${PIDFILE_DIR}/push-testpattern.lock"

RTMP_BASE="rtmp://${ZLM_HOST}:${ZLM_RTMP_PORT}/${APP}"
vpush_log "=== 四路测试图案推流到 ZLMediaKit (RTMP) ==="
vpush_log "RTMP_BASE=$RTMP_BASE FPS=$FPS SIZE=$SIZE VIN=${VIN:-(无前缀)}"

if ! command -v ffmpeg &>/dev/null; then
  vpush_log "ERROR: ffmpeg not found. Install with: apt install ffmpeg"
  exit 1
fi

# ★ 检查是否已有推流进程在运行
check_existing_process() {
  if [[ -f "$PIDFILE" ]]; then
    local old_pid=$(cat "$PIDFILE" 2>/dev/null || echo "")
    if [[ -n "$old_pid" ]] && kill -0 "$old_pid" 2>/dev/null; then
      # 检查进程是否真的是我们的推流脚本
      local cmdline=$(cat "/proc/$old_pid/cmdline" 2>/dev/null | tr '\0' ' ' || echo "")
      if echo "$cmdline" | grep -q "push-testpattern-to-zlm.sh"; then
        vpush_log "INFO: 推流进程已在运行 (PID: $old_pid)，跳过启动"
        vpush_log "如需重启: kill $old_pid 或删除 $PIDFILE"
        exit 0
      fi
    fi
    # PID 文件存在但进程不存在，清理过期的 PID 文件
    rm -f "$PIDFILE" "$LOCKFILE"
  fi
  
  # 检查是否有 ffmpeg 进程正在推流到相同的 RTMP 地址
  local rtmp_base="rtmp://${ZLM_HOST}:${ZLM_RTMP_PORT}/${APP}"
  local check_stream="${VIN_PREFIX}cam_front"
  if pgrep -f "ffmpeg.*${rtmp_base}/${check_stream}" >/dev/null 2>&1; then
    vpush_log "WARN: 检测到已有 ffmpeg 正在推流到 ${rtmp_base}"
    vpush_log "如需重启请先停止现有进程"
    exit 0
  fi
}

# ★ 创建锁文件（防止并发启动）
acquire_lock() {
  local max_attempts=10
  local attempt=0
  while [[ $attempt -lt $max_attempts ]]; do
    if (set -C; echo $$ > "$LOCKFILE") 2>/dev/null; then
      trap "rm -f '$LOCKFILE' '$PIDFILE'" EXIT INT TERM
      return 0
    fi
    sleep 0.1
    attempt=$((attempt + 1))
  done
  vpush_log "ERROR: 无法获取锁文件 $LOCKFILE，可能已有其他实例在运行"
  exit 1
}

# ★ 检查现有进程并获取锁
check_existing_process
acquire_lock

# ★ 保存主进程 PID
echo $$ > "$PIDFILE"

PIDS=()
cleanup() {
  vpush_log "Stopping all test pushers..."
  for pid in "${PIDS[@]}"; do
    kill -TERM "$pid" 2>/dev/null || true
  done
  wait 2>/dev/null || true
  rm -f "$PIDFILE" "$LOCKFILE"
  exit 0
}
trap cleanup SIGINT SIGTERM

# 四路 stream，流名加 VIN 前缀实现多车隔离：{VIN}_cam_front 等；VIN 为空时退化为 cam_front
for label in "FRONT" "REAR" "LEFT" "RIGHT"; do
  case "$label" in
    FRONT) base_stream="cam_front";;
    REAR)  base_stream="cam_rear";;
    LEFT)  base_stream="cam_left";;
    RIGHT) base_stream="cam_right";;
    *)     base_stream="cam_${label,,}";;
  esac
  stream_id="${VIN_PREFIX}${base_stream}"
  ffmpeg -re -f lavfi -i "testsrc=size=${SIZE}:rate=${FPS}" \
    -vf "drawtext=text='%{pts\:gmtime\:0\:%H\\:%M\\:%S} ${label}':fontsize=24:fontcolor=white:x=10:y=10" \
    -c:v libx264 -preset ultrafast -tune zerolatency -pix_fmt yuv420p \
    -g ${FPS} -keyint_min ${FPS} -bf 0 \
    -f flv "${RTMP_BASE}/${stream_id}" \
    -loglevel warning -y &
  PIDS+=($!)
  vpush_log "started $stream_id -> ${RTMP_BASE}/${stream_id} (PID $!)"
done

vpush_log "All test pushers running. Press Ctrl+C to stop."
wait
