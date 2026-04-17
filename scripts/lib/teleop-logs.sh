#!/usr/bin/env bash
# 统一日志目录：仓库根目录 logs/。
# - 默认：日志直接在 logs/ 下，文件名含日期 YYYY-MM-DD 或会话时间戳。
# - 全链路脚本可调用 teleop_logs_init_run_subdir，在本机生成 logs/<运行ID>/，
#   本会话内 teleop_log_path_* 与客户端 CLIENT_LOG_FILE 均落在该子目录。
#
# 使用前请已设置 PROJECT_ROOT 并 cd 到项目根（或任意目录但 PROJECT_ROOT 正确）。
#
# shellcheck shell=bash
# shellcheck disable=SC2034  # TELEOP_* 供调用方使用

teleop_logs_base_dir() {
  if [ -n "${TELEOP_LOGS_RUN_DIR:-}" ]; then
    printf '%s' "$TELEOP_LOGS_RUN_DIR"
  else
    printf '%s' "$TELEOP_LOGS_DIR"
  fi
}

teleop_logs_init() {
  TELEOP_LOG_DATE="${TELEOP_LOG_DATE:-$(date +%Y-%m-%d)}"
  if [ -z "${PROJECT_ROOT:-}" ]; then
    echo "[teleop-logs] ERROR: PROJECT_ROOT is not set" >&2
    return 1
  fi
  TELEOP_LOGS_DIR="${PROJECT_ROOT}/logs"
  mkdir -p "$TELEOP_LOGS_DIR"
  if [ -n "${TELEOP_LOGS_RUN_DIR:-}" ]; then
    mkdir -p "$TELEOP_LOGS_RUN_DIR"
  fi
}

# 创建 logs/<运行ID>/（默认 运行ID = YYYY-MM-DD-HHMMSS），供单次 start-full-chain 全链路归档。
# 若环境已导出 TELEOP_LOGS_RUN_DIR（子脚本由父进程启动），则不再覆盖。
teleop_logs_init_run_subdir() {
  teleop_logs_init
  if [ -n "${TELEOP_LOGS_RUN_DIR:-}" ]; then
    mkdir -p "$TELEOP_LOGS_RUN_DIR"
    export TELEOP_RUN_ID TELEOP_LOGS_RUN_DIR
    return 0
  fi
  TELEOP_RUN_ID="${TELEOP_RUN_ID:-$(date +%Y-%m-%d-%H%M%S)}"
  TELEOP_LOGS_RUN_DIR="${TELEOP_LOGS_DIR}/${TELEOP_RUN_ID}"
  mkdir -p "$TELEOP_LOGS_RUN_DIR"
  export TELEOP_RUN_ID TELEOP_LOGS_RUN_DIR
}

# 客户端容器内 CLIENT_LOG_FILE 路径（挂载为 宿主机 ./logs → /workspace/logs）
teleop_client_log_container_path() {
  teleop_logs_init
  local rel="client-${TELEOP_LOG_DATE}.log"
  if [ -n "${TELEOP_LOGS_RUN_DIR:-}" ]; then
    local rid="${TELEOP_RUN_ID:-}"
    [ -n "$rid" ] || rid="$(basename "$TELEOP_LOGS_RUN_DIR")"
    mkdir -p "$TELEOP_LOGS_RUN_DIR"
    printf '/workspace/logs/%s/%s' "$rid" "$rel"
  else
    printf '/workspace/logs/%s' "$rel"
  fi
}

# 按自然日一个文件：name-YYYY-MM-DD.log（适合客户端、长期追加）
teleop_log_path_daily() {
  local name="$1"
  echo "$(teleop_logs_base_dir)/${name}-${TELEOP_LOG_DATE}.log"
}

# 按会话唯一：name-YYYY-MM-DD-HHMMSS.log（适合单次验证、全链路子任务）
teleop_log_path_session() {
  local name="$1"
  local ts
  ts=$(date +%Y-%m-%d-%H%M%S)
  echo "$(teleop_logs_base_dir)/${name}-${ts}.log"
}
