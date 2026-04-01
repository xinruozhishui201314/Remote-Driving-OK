#!/bin/bash
# client-dev 容器入口：启动时自动编译 client（宿主机挂载 ./client 后，修改即会在下次启动时触发编译）
# 用法：entrypoint 执行本脚本，command 为 sleep infinity 等保持容器运行

echo "[entrypoint-client-dev] ========== 启动时编译 client =========="
echo "[entrypoint-client-dev] 源码挂载: /workspace/client"

mkdir -p /tmp/client-build
cd /tmp/client-build
if [ ! -f CMakeCache.txt ]; then
  echo "[entrypoint-client-dev] 首次配置 CMake..."
  cmake /workspace/client \
    -DCMAKE_PREFIX_PATH=/opt/Qt/6.8.0/gcc_64:/opt/libdatachannel \
    -DCMAKE_BUILD_TYPE=Debug || true
fi
echo "[entrypoint-client-dev] 编译 client（增量编译，无修改时很快）..."
if make -j4; then
  echo "[entrypoint-client-dev] ✓ 编译完成: /tmp/client-build/RemoteDrivingClient"
else
  echo "[entrypoint-client-dev] ⚠ 编译失败，可手动执行: cd /tmp/client-build && make -j4"
fi

echo "[entrypoint-client-dev] 执行: $*"
exec "$@"
