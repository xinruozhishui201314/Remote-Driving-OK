#!/usr/bin/env bash
# 在宿主机上拉取 Paho MQTT C/C++ 源码与 CMake 预编译包到 deploy/carla/deps/，供 Docker 构建时 COPY 进镜像（无需在容器内拉取）。
# 用法：./clone-deps.sh  或  bash deploy/carla/clone-deps.sh

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEPS_DIR="${SCRIPT_DIR}/deps"
mkdir -p "$DEPS_DIR"
cd "$DEPS_DIR"

echo "[clone-deps] 准备构建依赖到 $DEPS_DIR"

if [ ! -d "paho.mqtt.c" ]; then
  git clone --depth 1 -b v1.3.13 https://github.com/eclipse/paho.mqtt.c.git
  echo "[clone-deps] paho.mqtt.c 已克隆"
else
  echo "[clone-deps] paho.mqtt.c 已存在，跳过"
fi

if [ ! -d "paho.mqtt.cpp" ]; then
  git clone --depth 1 -b v1.2.0 https://github.com/eclipse/paho.mqtt.cpp.git
  echo "[clone-deps] paho.mqtt.cpp 已克隆"
else
  echo "[clone-deps] paho.mqtt.cpp 已存在，跳过"
fi

# CMake 预编译包：无则从 GitHub 下载，有则跳过（也可手动放到 deps/cmake.tar.gz）
if [ ! -f "cmake.tar.gz" ]; then
  echo "[clone-deps] 下载 CMake 3.28.1 预编译包到 deps/cmake.tar.gz"
  wget -q -O cmake.tar.gz "https://github.com/Kitware/CMake/releases/download/v3.28.1/cmake-3.28.1-linux-x86_64.tar.gz"
  echo "[clone-deps] cmake.tar.gz 已下载"
else
  echo "[clone-deps] cmake.tar.gz 已存在，跳过"
fi

echo "[clone-deps] 完成。构建镜像: docker compose ... build carla 或 ./scripts/build-carla-image.sh"
