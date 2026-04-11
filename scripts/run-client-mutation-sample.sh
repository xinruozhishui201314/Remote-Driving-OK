#!/usr/bin/env bash
# V1/V2：可选变异测试入口（本机已安装 Mull 时执行；否则跳过并说明）。
# 定期提醒：`.github/workflows/client-mutation-weekly.yml`（周一 06:00 UTC）。
#
# Mull 需要 compile_commands.json 与专用编译标志，详见 https://github.com/mull-project/mull
# 本脚本不修改默认构建；仅在 CI/研发机显式配置 Mull  toolchain 后使用。
#
# 用法：
#   MULL_BUILD_DIR=/path/to/mull-build ./scripts/run-client-mutation-sample.sh
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

if command -v mull-runner-17 >/dev/null 2>&1; then
  MULL_RUNNER=(mull-runner-17)
elif command -v mull-runner-16 >/dev/null 2>&1; then
  MULL_RUNNER=(mull-runner-16)
elif command -v mull-runner >/dev/null 2>&1; then
  MULL_RUNNER=(mull-runner)
else
  echo "[mutation][SKIP] 未找到 mull-runner（未安装 Mull）。安全关键模块建议在本机配置 Mull 后对 core/antireplay、commandsigner 等做小范围变异。"
  echo "  参考: https://github.com/mull-project/mull"
  exit 0
fi

MULL_BUILD_DIR="${MULL_BUILD_DIR:-}"
if [[ -z "$MULL_BUILD_DIR" || ! -d "$MULL_BUILD_DIR" ]]; then
  echo "[mutation][SKIP] 请设置 MULL_BUILD_DIR 为已用 Mull 工具链配置并编译的 build 目录。"
  exit 0
fi

echo "[mutation] 使用 ${MULL_RUNNER[*]} ，build=$MULL_BUILD_DIR"
echo "[mutation] 配置与推荐范围见: $REPO_ROOT/docs/MULL_CLIENT.md"
cd "$MULL_BUILD_DIR"
exec "${MULL_RUNNER[@]}" "$@"
