#!/usr/bin/env bash
# 将 client/ 目录恢复为当前用户属主，解决「Cursor/IDE 无法保存、Permission denied」问题。
# 常见原因：在容器内以 root 构建/复制或误用 sudo 编辑后，宿主上文件仍为 root:root。
#
# 用法（仓库根目录）：
#   bash scripts/fix-client-source-ownership.sh
#
# 需要：本机 sudo 密码一次；不会修改仓库外路径。

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TARGET="${ROOT}/client"

if [[ ! -d "${TARGET}" ]]; then
  echo "错误: 未找到 ${TARGET}" >&2
  exit 1
fi

if [[ "$(id -u)" -eq 0 ]]; then
  echo "请勿直接以 root 运行本脚本；请用普通用户执行（脚本内部会调用一次 sudo）。" >&2
  exit 1
fi

U="$(id -un)"
G="$(id -gn)"
echo "即将把 ${TARGET} 的属主改为 ${U}:${G}（需 sudo）…"
sudo chown -R "${U}:${G}" "${TARGET}"
echo "完成。现在应可正常编辑 client 下源码并提交 git。"
