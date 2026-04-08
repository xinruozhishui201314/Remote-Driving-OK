#!/usr/bin/env bash
# Phase 6：将已编译的 RemoteDrivingClient 可执行文件打成 tar 包（需先完成 client CMake install 或手动指定二进制路径）
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
VERSION="${CLIENT_PACKAGE_VERSION:-$(grep -m1 'project(RemoteDrivingClient' "$ROOT/client/CMakeLists.txt" | sed -n 's/.*VERSION \([0-9.]*\).*/\1/p')}"
OUT="${ROOT}/dist/remote-driving-client-linux-${VERSION}.tar.gz"
BIN="${CLIENT_BINARY_PATH:-}"

if [ -z "$BIN" ]; then
  for c in "$ROOT/client/build/RemoteDrivingClient" "$ROOT/build/client/RemoteDrivingClient"; do
    if [ -x "$c" ]; then BIN="$c"; break; fi
  done
fi
if [ -z "$BIN" ] || [ ! -x "$BIN" ]; then
  echo "用法: CLIENT_BINARY_PATH=/path/to/RemoteDrivingClient $0"
  echo "或在 client/build 或 build/client 下先编译客户端。"
  exit 1
fi

mkdir -p "$ROOT/dist"
STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT
install -m0755 "$BIN" "$STAGE/RemoteDrivingClient"
tar -czf "$OUT" -C "$STAGE" RemoteDrivingClient
echo "打包完成: $OUT"
