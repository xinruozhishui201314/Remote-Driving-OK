#!/bin/bash
set -e
# 与客户端 [Client][WebRTC] 对齐：车端 RTMP 推流到 ZLM 的统一 grep 前缀
vpush_log() { echo "[Vehicle-side][ZLM][Push] $*" >&2; }

SWEEPS_PATH="${SWEEPS_PATH:-$HOME/bigdata/data/nuscenes-mini/sweeps}"
ZLM_HOST="${ZLM_HOST:-127.0.0.1}"
ZLM_RTMP_PORT="${ZLM_RTMP_PORT:-1935}"
APP="${ZLM_APP:-teleop}"
FPS="${NUSCENES_PUSH_FPS:-10}"
# 多车隔离：流名加 VIN 前缀，格式 {VIN}_cam_front；VIN 为空时不加前缀（兼容单车测试）
VIN="${VEHICLE_VIN:-${VIN:-}}"
VIN_PREFIX="${VIN:+${VIN}_}"

# ★ 码率控制（智能码率调整）
# 单路码率：根据图像复杂度动态调整（默认 200k-300k）
BITRATE="${NUSCENES_BITRATE:-250k}"     # 默认 250kbps，可通过环境变量覆盖
MAXRATE="${NUSCENES_MAXRATE:-300k}"     # 峰值码率
BUFSIZE="${NUSCENES_BUFSIZE:-150k}"     # 缓冲区大小（降低延迟）

# ★ 分辨率控制（进一步降低码率）
# 如果原图较大，可以缩放（默认不缩放）
SCALE="${NUSCENES_SCALE:-}"  # 例如 "640:480" 或 "480:360"，空表示不缩放

# ★ 编码器优化参数
PRESET="${NUSCENES_PRESET:-ultrafast}"    # 编码速度预设
TUNE="${NUSCENES_TUNE:-zerolatency}"     # 编码调优
CRF="${NUSCENES_CRF:-28}"               # 恒定质量因子（18-28，值越高质量越好但码率越高）

# ★ PID 文件管理（确保推流进程唯一性，避免重复启动）
PIDFILE_DIR="${PIDFILE_DIR:-/tmp}"
PIDFILE="${PIDFILE_DIR}/push-nuscenes-cameras.pid"
LOCKFILE="${PIDFILE_DIR}/push-nuscenes-cameras.lock"

declare -A CAM_MAP=(
  ["cam_front"]="CAM_FRONT"
  ["cam_rear"]="CAM_BACK"
  ["cam_left"]="CAM_FRONT_LEFT"
  ["cam_right"]="CAM_FRONT_RIGHT"
)

# ★ 检查是否已有推流进程在运行
check_existing_process() {
  if [[ -f "$PIDFILE" ]]; then
    local old_pid=$(cat "$PIDFILE" 2>/dev/null || echo "")
    # Plan 6.1: Robust check for stale PID file
    if [[ -n "$old_pid" ]]; then
      if kill -0 "$old_pid" 2>/dev/null; then
        # Process exists, check if it's ours
        local cmdline=$(cat "/proc/$old_pid/cmdline" 2>/dev/null | tr '\0' ' ' || echo "")
        if echo "$cmdline" | grep -q "push-nuscenes-cameras-to-zlm.sh"; then
          vpush_log "INFO: 推流进程已在运行 (PID: $old_pid)，跳过启动"
          vpush_log "如需重启: kill $old_pid 或删除 $PIDFILE"
          exit 0
        else
          # PID exists but is NOT our script. Clean up the stale lockfile and continue.
          vpush_log "WARN: PID $old_pid 存在但不属于本脚本，清理锁文件并继续"
          rm -f "$PIDFILE" "$LOCKFILE"
        fi
      else
        # Process does not exist (stale lockfile)
        vpush_log "INFO: PID $old_pid 不存在 (锁文件过期)，清理并继续"
        rm -f "$PIDFILE" "$LOCKFILE"
      fi
    fi
  fi
  
  # 检查是否有 ffmpeg 进程正在推流到相同的 RTMP 地址
  local rtmp_base="rtmp://${ZLM_HOST}:${ZLM_RTMP_PORT}/${APP}"
  local check_stream="${VIN_PREFIX}cam_front"
  if pgrep -f "ffmpeg.*${rtmp_base}/${check_stream}" >/dev/null 2>&1; then
    vpush_log "WARN: 检测到已有 ffmpeg 正在推流到 ${rtmp_base}/${check_stream}"
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

if [[ ! -d "$SWEEPS_PATH" ]]; then
  vpush_log "ERROR: SWEEPS_PATH not found: $SWEEPS_PATH"
  exit 1
fi

# ★ 检查现有进程并获取锁
check_existing_process
acquire_lock

RTMP_BASE="rtmp://${ZLM_HOST}:${ZLM_RTMP_PORT}/${APP}"
vpush_log "=== 四路相机推流到 ZLMediaKit（低码率优化）==="
vpush_log "FPS=$FPS BITRATE=$BITRATE MAXRATE=$MAXRATE BUFSIZE=$BUFSIZE 四路总码率约 $(( ${BITRATE%k} * 4 ))kbps VIN=${VIN:-(无前缀)}"
if [[ -n "$SCALE" ]]; then
  vpush_log "分辨率缩放: $SCALE"
fi

# ★ 保存主进程 PID
echo $$ > "$PIDFILE"
vpush_log "推流脚本已启动 PID=$$，正在启动四路 ffmpeg（ZLM 流约 5~15s 后可用）"

PIDS=()
cleanup() {
  vpush_log "Stopping all FFmpeg..."
  for pid in "${PIDS[@]}"; do
    kill -TERM "$pid" 2>/dev/null || true
  done
  wait 2>/dev/null || true
  rm -f "$PIDFILE" "$LOCKFILE"
  exit 0
}
trap cleanup SIGINT SIGTERM

DELAY=0
STREAMS_STARTED=0
for base_stream in cam_front cam_rear cam_left cam_right; do
  stream_id="${VIN_PREFIX}${base_stream}"
  cam_dir="${CAM_MAP[$base_stream]}"
  cam_path="${SWEEPS_PATH}/${cam_dir}"
  
  if [[ ! -d "$cam_path" ]]; then
    vpush_log "WARN: skip $stream_id (目录不存在: $cam_path)"
    continue
  fi
  
  # 检查目录中是否有图片文件
  if ! ls "${cam_path}"/*.jpg >/dev/null 2>&1; then
    vpush_log "WARN: skip $stream_id (目录中没有 .jpg 文件: $cam_path)"
    continue
  fi

  # ★ 错开启动
  if [ $DELAY -gt 0 ]; then
    sleep 0.3
  fi

  # ★ 构建 FFmpeg 命令（智能编码优化）
  FFMPEG_CMD=(
    -re
    -stream_loop -1
    -framerate "$FPS"
    -pattern_type glob
    -i "${cam_path}/*.jpg"
    -c:v libx264
    -preset "$PRESET"          # 编码速度预设（ultrafast/superfast/veryfast/faster/fast/medium/slow/slower/veryslow/superverylossless）
    -tune "$TUNE"              # 编码调优（zerolatency/fastdecode/etc）
    -pix_fmt yuv420p
    -b:v "$BITRATE"            # 目标码率
    -maxrate "$MAXRATE"        # 最大码率
    -bufsize "$BUFSIZE"        # 缓冲区大小
    -g "$FPS"                  # GOP = FPS（1秒一个IDR，快速恢复）
    -keyint_min "$FPS"         # 最小关键帧间隔
    -bf 0                      # 禁用B帧（降低延迟和复杂度）
    -profile:v baseline        # Baseline profile（兼容性最好，编码效率高）
    -level 3.0                 # Level 3.0（适合640x480@10fps）
    -crf "$CRF"                # 恒定质量因子（与 -b:v 互斥，优先使用 -b:v）
    -x264-params "no-mbtree:weightp=0:subme=1:me=dia:trellis=0:8x8dct=0:fast-pskip=1" # x264优化参数
  )
  
  # ★ 分辨率缩放（如果指定）
  if [[ -n "$SCALE" ]]; then
    FFMPEG_CMD+=(-vf "scale=$SCALE:force_original_aspect_ratio=decrease,pad=$(echo $SCALE | cut -d: -f1):$(echo $SCALE | cut -d: -f2):'(ow-iw)/2':'(ih-ih)/2':black")
  fi

  # ★ 分辨率缩放（如果指定）
  if [[ -n "$SCALE" ]]; then
    FFMPEG_CMD+=(-vf "scale=$SCALE")
  fi

  # ★ x264 高级参数（低码率优化）
  # - crf: 恒定质量模式（与 -b:v 冲突，这里用 CBR）
  # - qp: 量化参数（值越大质量越低，码率越小）
  # - me: 运动估计算法（dia最快，hex质量更好但慢）
  # - subme: 亚像素运动估计（1最快，质量较低但码率小）
  # - trellis: Trellis量化（0关闭，降低复杂度）
  # - 8x8dct: 禁用8x8 DCT（降低复杂度）
  # - fast-pskip: 快速P帧跳过（降低码率）
  # - no-mbtree: 禁用宏块树（降低复杂度）
  # - weightp: 加权预测（0关闭，降低复杂度）
  # - no-cabac: 禁用CABAC（使用CAVLC，降低复杂度但码率略高）
  # - nal-hrd: NAL HRD（cbr恒定码率模式）
  # - vbv: 视频缓冲验证器（控制码率波动）
  
  # 提取数值（去除单位）
  BUFSIZE_NUM="${BUFSIZE%k}"
  MAXRATE_NUM="${MAXRATE%k}"
  
  X264_PARAMS=(
    "slices=1"                    # 单slice（降低复杂度）
    "nal-hrd=cbr"                 # CBR模式
    "force-cfr=1"                 # 强制恒定帧率
    "vbv-bufsize=$BUFSIZE_NUM"    # VBV缓冲区大小（数值，单位k）
    "vbv-maxrate=$MAXRATE_NUM"    # VBV最大码率（数值，单位k）
    "me=dia"                      # 运动估计：dia（最快）
    "subme=1"                     # 亚像素运动估计：1（最快，质量较低）
    "trellis=0"                   # 禁用Trellis量化
    "8x8dct=0"                    # 禁用8x8 DCT
    "fast-pskip=1"                # 快速P帧跳过
    "no-mbtree=1"                 # 禁用宏块树
    "weightp=0"                   # 禁用加权预测
    "no-cabac=0"                  # 启用CABAC（虽然复杂但码率更低）
    "qpmin=28"                    # 最小量化参数（28-32适合低码率）
    "qpmax=40"                    # 最大量化参数
    "qpstep=4"                    # 量化参数步长
  )
  
  # 使用 IFS 构建参数字符串（冒号分隔）
  X264_PARAMS_STR=$(IFS=:; echo "${X264_PARAMS[*]}")
  FFMPEG_CMD+=(-x264-params "$X264_PARAMS_STR")
  FFMPEG_CMD+=(-f flv "${RTMP_BASE}/${stream_id}")
  FFMPEG_CMD+=(-loglevel warning -y)

  # 执行推流
  ffmpeg "${FFMPEG_CMD[@]}" &
  
  PIDS+=($!)
  STREAMS_STARTED=$((STREAMS_STARTED + 1))
  vpush_log "[$stream_id] PID=$! delay=${DELAY}s bitrate=$BITRATE"
  DELAY=1
done

# ★ 检查是否至少启动了一个流
if [ $STREAMS_STARTED -eq 0 ]; then
  vpush_log "ERROR: 没有启动任何推流（数据集目录不存在或为空）"
  vpush_log "检查: SWEEPS_PATH=$SWEEPS_PATH；目录 CAM_FRONT/CAM_BACK/CAM_FRONT_LEFT/CAM_FRONT_RIGHT/*.jpg；或改用 push-testpattern-to-zlm.sh"
  exit 1
fi

vpush_log "四路 ffmpeg 已全部启动（共 $STREAMS_STARTED 路），ZLM 流即将可用"
vpush_log "All running ($STREAMS_STARTED streams). Ctrl+C to stop."
wait
