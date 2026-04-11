#!/usr/bin/env bash
# 静态门禁：CPU 视频热路径禁止 BGR/RGB32 等历史格式（全链路 RGBA8888 契约）。
# 用法（仓库根）: ./scripts/verify-client-video-rgba-contract.sh
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m'

export PYTHONHASHSEED=0
if ! python3 - "$PROJECT_ROOT" <<'PY'
import re
import sys
from pathlib import Path

root = Path(sys.argv[1]).resolve()
# 热路径：解码输出、WebRTC 呈现、RemoteVideoSurface、HW bridge（勿把诊断/测试文件塞进来以免误伤）
FILES = [
    root / "client/src/h264decoder.cpp",
    root / "client/src/ui/RemoteVideoSurface.cpp",
    root / "client/src/webrtcclient.cpp",
    root / "client/src/media/H264WebRtcHwBridge.cpp",
]

# 仅扫描每行「//」之前的代码段，减少注释/文档误报
def code_part(line: str) -> str:
    i = line.find("//")
    if i < 0:
        return line
    return line[:i]

PATTERNS = [
    (re.compile(r"\bAV_PIX_FMT_BGR0\b"), "AV_PIX_FMT_BGR0"),
    (re.compile(r"\bAV_PIX_FMT_BGRA\b"), "AV_PIX_FMT_BGRA"),
    (re.compile(r"\bAV_PIX_FMT_BGR24\b"), "AV_PIX_FMT_BGR24"),
    (re.compile(r"\bQImage::Format_RGB32\b"), "QImage::Format_RGB32"),
    (re.compile(r"\bQImage::Format_ARGB32\b"), "QImage::Format_ARGB32"),
    (re.compile(r"\bQImage::Format_BGR888\b"), "QImage::Format_BGR888"),
]

failed = False
for path in FILES:
    if not path.is_file():
        print(f"FAIL missing required file {path}", file=sys.stderr)
        failed = True
        continue
    text = path.read_text(encoding="utf-8", errors="replace")
    for lineno, line in enumerate(text.splitlines(), 1):
        code = code_part(line)
        for rx, name in PATTERNS:
            if rx.search(code):
                print(f"{path}:{lineno}: forbidden in CPU video hot path: {name}")
                print(f"  {line.strip()[:160]}")
                failed = True

if failed:
    sys.exit(1)
print("OK: verify-client-video-rgba-contract (hot path scan)")
PY
then
  echo -e "${RED}[FAIL] verify-client-video-rgba-contract${NC}" >&2
  exit 1
fi

echo -e "${GREEN}[OK] verify-client-video-rgba-contract${NC}"
