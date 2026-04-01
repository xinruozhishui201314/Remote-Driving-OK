#!/bin/bash
# 开发模式入口脚本：编译并运行 backend，监控源码变化自动重新编译

cd /app

# 构建输出目录（避免使用可能带 noexec 的挂载卷）
BUILD_DIR="${BUILD_DIR:-/tmp/backend-build}"

# 网络诊断函数
check_network() {
    echo "[$(date +'%H:%M:%S')] === 检查网络连接 ==="
    
    # 显示网络信息
    echo "[$(date +'%H:%M:%S')] DNS 配置："
    cat /etc/resolv.conf 2>/dev/null | grep -E "nameserver|search" || echo "  无法读取 /etc/resolv.conf"
    
    echo "[$(date +'%H:%M:%S')] 代理配置："
    echo "  http_proxy=${http_proxy:-未设置}"
    echo "  https_proxy=${https_proxy:-未设置}"
    echo ""
    
    # 检查 DNS
    if ! nslookup github.com >/dev/null 2>&1; then
        echo "[$(date +'%H:%M:%S')] ❌ DNS 解析失败：无法解析 github.com"
        echo "[$(date +'%H:%M:%S')] 尝试使用 getent..."
        if ! getent hosts github.com >/dev/null 2>&1; then
            echo "[$(date +'%H:%M:%S')] ❌ getent 也失败"
            return 1
        fi
        echo "[$(date +'%H:%M:%S')] ✓ 使用 getent 解析成功"
    else
        echo "[$(date +'%H:%M:%S')] ✓ DNS 解析正常"
    fi
    
    # 检查 GitHub 连接
    echo "[$(date +'%H:%M:%S')] 测试 GitHub 连接（超时 10 秒）..."
    if ! curl -s --connect-timeout 5 --max-time 10 -I https://github.com >/dev/null 2>&1; then
        echo "[$(date +'%H:%M:%S')] ❌ 无法连接到 GitHub"
        echo "[$(date +'%H:%M:%S')] 可能的原因："
        echo "[$(date +'%H:%M:%S')]   1. 网络连接问题"
        echo "[$(date +'%H:%M:%S')]   2. 防火墙阻止"
        echo "[$(date +'%H:%M:%S')]   3. 需要代理（设置 http_proxy/https_proxy）"
        echo "[$(date +'%H:%M:%S')] 将尝试继续，CMake 可能会提供更详细的错误信息"
        return 1
    fi
    echo "[$(date +'%H:%M:%S')] ✓ GitHub 连接正常"
    
    # 检查 Git
    if ! command -v git >/dev/null 2>&1; then
        echo "[$(date +'%H:%M:%S')] ❌ 未找到 git 命令"
        return 1
    fi
    echo "[$(date +'%H:%M:%S')] ✓ Git 可用: $(git --version)"
    
    return 0
}

# 编译函数
build_backend() {
    echo "[$(date +'%H:%M:%S')] === 编译 Backend (BUILD_DIR=${BUILD_DIR}) ==="
    mkdir -p "${BUILD_DIR}"
    cd "${BUILD_DIR}"
    
    # 如果 CMakeCache.txt 不存在或 CMakeLists.txt 更新了，重新配置
    if [ ! -f CMakeCache.txt ] || [ /app/CMakeLists.txt -nt CMakeCache.txt ]; then
        echo "[$(date +'%H:%M:%S')] === 配置 CMake ==="
        echo "[$(date +'%H:%M:%S')] 这将下载以下依赖项："
        echo "[$(date +'%H:%M:%S')]   - cpp-httplib (v0.14.3) from https://github.com/yhirose/cpp-httplib"
        echo "[$(date +'%H:%M:%S')]   - nlohmann_json (v3.11.3) from https://github.com/nlohmann/json"
        echo "[$(date +'%H:%M:%S')] 如果下载缓慢，请耐心等待..."
        echo ""
        
        # 检查网络（警告但不阻止）
        if ! check_network; then
            echo "[$(date +'%H:%M:%S')] ⚠️  网络检查失败，但将继续尝试"
            echo "[$(date +'%H:%M:%S')] 提示：如果在中国大陆，可能需要配置代理或使用镜像源"
            echo "[$(date +'%H:%M:%S')] 如果下载失败，请检查："
            echo "[$(date +'%H:%M:%S')]   1. 容器网络配置：docker network inspect teleop-network"
            echo "[$(date +'%H:%M:%S')]   2. DNS 配置：cat /etc/resolv.conf"
            echo "[$(date +'%H:%M:%S')]   3. 代理设置：echo \$http_proxy \$https_proxy"
            echo ""
        fi
        
        echo ""
        echo "[$(date +'%H:%M:%S')] 开始 CMake 配置（这可能需要几分钟下载依赖项）..."
        
        # 使用详细输出运行 CMake
        if ! cmake /app -DCMAKE_BUILD_TYPE=Release --no-warn-unused-cli 2>&1 | tee /tmp/cmake-config.log; then
            echo ""
            echo "[$(date +'%H:%M:%S')] ❌ CMake 配置失败"
            echo "[$(date +'%H:%M:%S')] === 错误详情 ==="
            tail -50 /tmp/cmake-config.log | grep -E "error|Error|ERROR|failed|Failed|FAILED|timeout|Timeout|TIMEOUT" || tail -20 /tmp/cmake-config.log
            echo ""
            echo "[$(date +'%H:%M:%S')] === 常见问题排查 ==="
            echo "[$(date +'%H:%M:%S')] 1. 检查网络连接：curl -I https://github.com"
            echo "[$(date +'%H:%M:%S')] 2. 检查 Git 配置：git config --global --list"
            echo "[$(date +'%H:%M:%S')] 3. 如果使用代理，设置：git config --global http.proxy <proxy_url>"
            echo "[$(date +'%H:%M:%S')] 4. 查看完整日志：cat /tmp/cmake-config.log"
            echo ""
            echo "[$(date +'%H:%M:%S')] 清理构建目录后重试..."
            cd /
            rm -rf "${BUILD_DIR}"
            mkdir -p "${BUILD_DIR}" && cd "${BUILD_DIR}"
            
            echo "[$(date +'%H:%M:%S')] 再次尝试 CMake 配置..."
            if ! cmake /app -DCMAKE_BUILD_TYPE=Release --no-warn-unused-cli 2>&1 | tee /tmp/cmake-config-retry.log; then
                echo "[$(date +'%H:%M:%S')] ❌ 重试仍然失败"
                tail -30 /tmp/cmake-config-retry.log
                cd /app
                return 1
            fi
        fi
        echo "[$(date +'%H:%M:%S')] ✓ CMake 配置成功"
    else
        echo "[$(date +'%H:%M:%S')] ✓ 使用缓存的 CMake 配置"
    fi
    
    echo ""
    echo "[$(date +'%H:%M:%S')] === 构建 Backend ==="
    if ! cmake --build . --target backend --verbose 2>&1 | tee /tmp/cmake-build.log; then
        echo ""
        echo "[$(date +'%H:%M:%S')] ❌ 构建失败"
        echo "[$(date +'%H:%M:%S')] === 错误详情 ==="
        tail -50 /tmp/cmake-build.log | grep -E "error|Error|ERROR|failed|Failed|FAILED" || tail -20 /tmp/cmake-build.log
        cd /app
        return 1
    fi
    
    cd /app
    echo "[$(date +'%H:%M:%S')] ✓ 编译完成"
    return 0
}

# 启动 backend 函数（CMake 默认将可执行文件生成在构建目录根，名为 target 名 backend）
run_backend() {
    if [ -f "${BUILD_DIR}/backend" ]; then
        echo "[$(date +'%H:%M:%S')] === 启动 Backend ==="
        "${BUILD_DIR}/backend" &
        BACKEND_PID=$!
        echo "[$(date +'%H:%M:%S')] === Backend PID: $BACKEND_PID ==="
    else
        echo "[$(date +'%H:%M:%S')] === 错误：找不到可执行文件 ==="
    fi
}

# 停止 backend 函数
stop_backend() {
    if [ ! -z "$BACKEND_PID" ] && kill -0 $BACKEND_PID 2>/dev/null; then
        echo "[$(date +'%H:%M:%S')] === 停止 Backend (PID: $BACKEND_PID) ==="
        kill $BACKEND_PID 2>/dev/null || true
        wait $BACKEND_PID 2>/dev/null || true
    fi
}

# 清理函数
cleanup() {
    stop_backend
    exit 0
}

trap cleanup SIGTERM SIGINT

# 首次编译和启动
if build_backend; then
    run_backend
else
    echo "[$(date +'%H:%M:%S')] === 首次编译失败，退出 ==="
    exit 1
fi

# 监控源码目录变化（.cpp, .h, CMakeLists.txt）
echo "[$(date +'%H:%M:%S')] === 开始监控源码变化（src/, CMakeLists.txt）==="
LAST_BUILD=0
while true; do
    # 使用 inotifywait 监控文件变化（超时 5 秒，避免阻塞）
    if inotifywait -r -e modify,create,delete,move --timeout 5 \
        --include '\.(cpp|h)$' \
        --include 'CMakeLists\.txt$' \
        src/ CMakeLists.txt 2>/dev/null; then
        # 防抖：距离上次编译至少 3 秒
        NOW=$(date +%s)
        if [ $((NOW - LAST_BUILD)) -ge 3 ]; then
            echo "[$(date +'%H:%M:%S')] === 检测到文件变化，重新编译... ==="
            stop_backend
            if build_backend; then
                run_backend
                LAST_BUILD=$(date +%s)
            else
                echo "[$(date +'%H:%M:%S')] === 编译失败，保持当前状态 ==="
            fi
        fi
    fi
done
