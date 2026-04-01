#!/bin/bash
# Vehicle-side 开发模式入口脚本：在容器内编译并运行 VehicleSide，可监控源码变化自动重新编译

set -e

echo "========================================"
echo " Vehicle-side 启动脚本"
echo " 容器启动时间: $(date '+%Y-%m-%d %H:%M:%S')"
echo "========================================"

cd /app

# 环境变量检查
echo "[启动] 环境变量检查:"
echo "  - WORKDIR: /app"
echo "  - BUILD_DIR: ${BUILD_DIR:-/tmp/vehicle-build}"
echo "  - VEHICLE_APP_PORT: ${VEHICLE_APP_PORT:-9000}"
echo "  - LD_LIBRARY_PATH: ${LD_LIBRARY_PATH:-未设置}"
echo "  - 依赖库路径: /app/deps/install/lib"
ls -la /app/deps/install/lib/ 2>/dev/null || echo "  - 警告: 依赖库目录不存在"
echo ""

# 构建输出目录（避免使用可能带 noexec 的挂载卷）
BUILD_DIR="${BUILD_DIR:-/tmp/vehicle-build}"

build_vehicle() {
    echo "========================================"
    echo "[$(date +'%H:%M:%S')] 开始编译 VehicleSide"
    echo "  BUILD_DIR=${BUILD_DIR}"
    echo "  源码目录: /app/src"
    echo "  CMakeLists: /app/CMakeLists.txt"
    echo "  依赖路径: /app/deps/install"
    echo "========================================"

    mkdir -p "${BUILD_DIR}"
    cd "${BUILD_DIR}"

    # 检查 CMakeLists.txt 是否存在
    if [ ! -f /app/CMakeLists.txt ]; then
        echo "[错误] CMakeLists.txt 不存在!"
        ls -la /app/
        return 1
    fi

    # 检查源码目录
    echo "[$(date +'%H:%M:%S')] 检查源码目录..."
    if [ ! -d /app/src ]; then
        echo "[错误] 源码目录 /app/src 不存在!"
        ls -la /app/
        return 1
    fi
    echo "  源码文件: $(find /app/src -name "*.cpp" | wc -l) 个 .cpp, $(find /app/src -name "*.h" | wc -l) 个 .h"

    if [ ! -f CMakeCache.txt ] || [ /app/CMakeLists.txt -nt CMakeCache.txt ]; then
        echo "[$(date +'%H:%M:%S')] 执行 CMake 配置..."
        cmake /app \
            -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_PREFIX_PATH=/app/deps/install || {
            echo "[$(date +'%H:%M:%S')] CMake 配置失败，清理后重试..."
            cd /
            rm -rf "${BUILD_DIR}"
            mkdir -p "${BUILD_DIR}" && cd "${BUILD_DIR}"
            cmake /app \
                -DCMAKE_BUILD_TYPE=Release \
                -DCMAKE_PREFIX_PATH=/app/deps/install
        }
        echo "[$(date +'%H:%M:%S')] CMake 配置完成"
    else
        echo "[$(date +'%H:%M:%S')] CMake 配置未变更，跳过"
    fi

    echo "[$(date +'%H:%M:%S')] 执行编译..."
    cmake --build . --target VehicleSide -j$(nproc) || {
        echo "[$(date +'%H:%M:%S')] 编译失败!"
        cd /app
        return 1
    }

    # 验证生成的可执行文件
    if [ -f "${BUILD_DIR}/VehicleSide" ]; then
        echo "[$(date +'%H:%M:%S')] 编译成功，可执行文件:"
        ls -lh "${BUILD_DIR}/VehicleSide"
        file "${BUILD_DIR}/VehicleSide"
        # 检查动态链接库依赖
        echo "  依赖库检查:"
        ldd "${BUILD_DIR}/VehicleSide" 2>&1 | head -10 || true
    else
        echo "[$(date +'%H:%M:%S')] 错误: 可执行文件未生成!"
        ls -la "${BUILD_DIR}/"
        return 1
    fi

    cd /app
    echo "[$(date +'%H:%M:%S')] 编译完成 ✓"
    return 0
}

run_vehicle() {
    if [ -f "${BUILD_DIR}/VehicleSide" ]; then
        echo "========================================"
        echo "[$(date +'%H:%M:%S')] 启动 VehicleSide"
        echo "  可执行文件: ${BUILD_DIR}/VehicleSide"
        echo "  监听端口: ${VEHICLE_APP_PORT:-9000}"
        echo "  LD_LIBRARY_PATH: /app/deps/install/lib:$LD_LIBRARY_PATH"
        echo "========================================"

        # 设置库路径并启动
        export LD_LIBRARY_PATH="/app/deps/install/lib:$LD_LIBRARY_PATH"

        "${BUILD_DIR}/VehicleSide" &
        VEHICLE_PID=$!
        echo "[$(date +'%H:%M:%S')] VehicleSide 已启动"
        echo "  PID: $VEHICLE_PID"

        # 等待进程启动
        sleep 1

        # 检查进程状态
        if kill -0 $VEHICLE_PID 2>/dev/null; then
            echo "[$(date +'%H:%M:%S')] VehicleSide 运行中 (PID: $VEHICLE_PID) ✓"
        else
            echo "[$(date +'%H:%M:%S')] VehicleSide 启动后立即退出!"
            echo "  检查日志或运行: ${BUILD_DIR}/VehicleSide"
            return 1
        fi
    else
        echo "[$(date +'%H:%M:%S')] 错误：找不到可执行文件 VehicleSide"
        echo "  BUILD_DIR: ${BUILD_DIR}"
        find /tmp -name "VehicleSide" -type f 2>/dev/null
        return 1
    fi
}

stop_vehicle() {
    if [ ! -z "$VEHICLE_PID" ] && kill -0 $VEHICLE_PID 2>/dev/null; then
        echo "[$(date +'%H:%M:%S')] 停止 VehicleSide (PID: $VEHICLE_PID)..."
        kill $VEHICLE_PID 2>/dev/null || true
        wait $VEHICLE_PID 2>/dev/null || true
        echo "[$(date +'%H:%M:%S')] VehicleSide 已停止 ✓"
    fi
}

cleanup() {
    echo "[$(date +'%H:%M:%S')] 收到退出信号，开始清理..."
    stop_vehicle
    echo "[$(date +'%H:%M:%S')] 退出"
    exit 0
}

trap cleanup SIGTERM SIGINT

# 首次编译和启动
if build_vehicle; then
    run_vehicle
else
    echo "[$(date +'%H:%M:%S')] 首次编译失败，退出"
    echo "  请检查上方日志定位问题"
    exit 1
fi

# 监控源码目录变化（.cpp, .h, CMakeLists.txt）
echo "========================================"
echo "[$(date +'%H:%M:%S')] 开始监控源码变化"
echo "  监控目录: /app/src, /app/CMakeLists.txt"
echo "  触发间隔: 3秒"
echo "========================================"
LAST_BUILD=0
while true; do
    if inotifywait -r -e modify,create,delete,move --timeout 5 \
        --include '\.(cpp|h)$' \
        --include 'CMakeLists\.txt$' \
        src/ CMakeLists.txt 2>/dev/null; then
        NOW=$(date +%s)
        if [ $((NOW - LAST_BUILD)) -ge 3 ]; then
            echo "[$(date +'%H:%M:%S')] 检测到源码变化，开始重新编译..."
            stop_vehicle
            if build_vehicle; then
                run_vehicle
                LAST_BUILD=$(date +%s)
                echo "[$(date +'%H:%M:%S')] 重新编译完成，继续监控..."
            else
                echo "[$(date +'%H:%M:%S')] 编译失败，保持当前状态"
            fi
        fi
    fi
done

