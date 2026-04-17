#!/usr/bin/env bash
# 将运行中的 Docker 容器 stdout/stderr 追加写入 TELEOP_LOGS_RUN_DIR/docker-<name>.log
# 与 start-full-chain 配合：会话目录由 teleop_logs_init_run_subdir 创建。
#
# shellcheck shell=bash
# 请先 source teleop-logs.sh 并 teleop_logs_init；全链路需先 teleop_logs_init_run_subdir。

teleop_start_docker_log_followers() {
  [ -n "${TELEOP_LOGS_RUN_DIR:-}" ] || return 0
  local pidfile="${TELEOP_LOGS_RUN_DIR}/.docker-log-follower-pids"
  : >"$pidfile"
  local names=(
    teleop-postgres
    teleop-keycloak
    teleop-zlmediakit
    teleop-backend
    teleop-mosquitto
    teleop-client-dev
    teleop-vehicle
    carla-server
  )
  local name
  for name in "${names[@]}"; do
    if docker ps --format '{{.Names}}' | grep -qx "$name"; then
      : >"${TELEOP_LOGS_RUN_DIR}/docker-${name}.log"
      # 先落盘已有输出，再 follow，避免丢失启动阶段日志
      docker logs "$name" >>"${TELEOP_LOGS_RUN_DIR}/docker-${name}.log" 2>&1 || true
      docker logs -f "$name" >>"${TELEOP_LOGS_RUN_DIR}/docker-${name}.log" 2>&1 &
      echo $! >>"$pidfile"
    fi
  done
  if [ ! -s "$pidfile" ]; then
    rm -f "$pidfile"
  fi
}

teleop_stop_docker_log_followers() {
  [ -n "${TELEOP_LOGS_RUN_DIR:-}" ] || return 0
  local pidfile="${TELEOP_LOGS_RUN_DIR}/.docker-log-follower-pids"
  [ -f "$pidfile" ] || return 0
  local pid
  while read -r pid; do
    [ -n "$pid" ] || continue
    kill "$pid" 2>/dev/null || true
  done <"$pidfile"
  rm -f "$pidfile"
}
