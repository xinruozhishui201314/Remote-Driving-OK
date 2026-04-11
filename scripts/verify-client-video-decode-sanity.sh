#!/usr/bin/env bash
# 客户端视频解码/显示策略冒烟：运行 test_h264decoder（含 ClientVideoStreamHealth 契约断言）。
# 用法：./scripts/verify-client-video-decode-sanity.sh
# 可选：CLIENT_BUILD_DIR=/path/to/client/build（默认仓库内 client/build）

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="${CLIENT_BUILD_DIR:-$PROJECT_ROOT/client/build}"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
NC='\033[0m'

if [[ ! -d "$BUILD_DIR" ]]; then
  echo -e "${YELLOW}[SKIP]${NC} client build 目录不存在: $BUILD_DIR（设 CLIENT_BUILD_DIR 或先 cmake --build client/build）"
  exit 0
fi

EXE="$BUILD_DIR/test_h264decoder"
if [[ ! -x "$EXE" ]]; then
  echo -e "${YELLOW}[SKIP]${NC} 未构建 test_h264decoder（需 ENABLE_FFMPEG）：$EXE"
  exit 0
fi

export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-offscreen}"
if "$EXE" -silent; then
  echo -e "${GREEN}[OK]${NC} test_h264decoder（含 video_stream_health_* 显示契约）"
  exit 0
fi
echo -e "${RED}[FAIL]${NC} test_h264decoder"
exit 1
