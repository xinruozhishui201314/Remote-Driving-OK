#!/usr/bin/env bash
# 安装 Git LFS、将 deploy/carla/deps/cmake.tar.gz 纳入 LFS 并推送（需本机执行一次，会提示 sudo 密码）
set -e
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

echo "[LFS] 检查/安装 Git LFS..."
if ! command -v git-lfs &>/dev/null; then
  sudo apt-get update -qq
  sudo apt-get install -y git-lfs
fi
git lfs install

echo "[LFS] 将 deploy/carla/deps/cmake.tar.gz 纳入 LFS 并提交..."
git add deploy/carla/deps/cmake.tar.gz
git commit -m "chore: 将 cmake.tar.gz 纳入 Git LFS" || true
git push origin master

echo "[LFS] 完成。"
