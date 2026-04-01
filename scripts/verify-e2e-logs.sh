#!/bin/bash
# 全链路问题排查：查看车端与客户端相关日志，便于分析「服务器错误」等
# 用法：bash scripts/verify-e2e-logs.sh [vehicle|client|both]

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

COMPOSE="docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml"

case "${1:-both}" in
  vehicle)
    echo "=== 车端日志（推流脚本、ZLM 连接等）==="
    $COMPOSE logs --tail=200 vehicle 2>&1
    ;;
  client)
    echo "=== 客户端需在终端运行并查看 stdout，关键标签：[WebRTC] 请求拉流 URL / ZLM 返回错误 / 请求失败 ==="
    echo "  运行客户端: bash scripts/run.sh client --reset-login"
    echo "  点击连接车端后，终端会打印每个 stream 的请求 URL、HTTP 状态、以及 ZLM 返回的 code/msg 或完整 body"
    ;;
  both)
    echo "=== 车端最近 200 行 ==="
    $COMPOSE logs --tail=200 vehicle 2>&1
    echo ""
    echo "=== 查看车端实时日志: $COMPOSE logs -f vehicle ==="
    echo "=== 客户端在运行时的终端输出会包含 [WebRTC] 请求 URL、ZLM 返回错误 code/msg 等 ==="
    ;;
  *)
    echo "用法: $0 {vehicle|client|both}"
    echo "  vehicle - 打印车端容器最近日志"
    echo "  client  - 说明客户端日志位置与关键字"
    echo "  both    - 车端日志 + 说明（默认）"
    exit 1
    ;;
esac
