# 客户端自动编译改进方案

## 当前状态

`start-full-chain.sh manual` 脚本：
- ✅ **启动时自动编译**：如果客户端未编译，会自动执行 `cmake` + `make`
- ✅ **增量编译**：如果已编译，会执行 `make -j4` 增量编译
- ❌ **不监控源码变化**：启动后修改源码不会自动重新编译

## 改进方案

### 方案 1：在启动客户端前添加增量编译（推荐）

修改 `start_client()` 函数，在启动前总是执行增量编译：

```bash
start_client() {
    # ... 现有代码 ...
    $COMPOSE exec -it \
        -e DISPLAY="$DISPLAY" \
        -e QT_QPA_PLATFORM=xcb \
        -e QT_LOGGING_RULES="qt.qpa.*=false" \
        -e ZLM_VIDEO_URL="$ZLM_VIDEO_URL" \
        -e MQTT_BROKER_URL="$MQTT_BROKER_URL" \
        -e CLIENT_RESET_LOGIN=1 \
        client-dev bash -c '
        mkdir -p /tmp/client-build && cd /tmp/client-build
        # 总是执行增量编译（即使可执行文件已存在）
        if [ -f CMakeCache.txt ]; then
            echo "执行增量编译..."
            make -j4 || { echo "编译失败，使用现有可执行文件"; }
        elif [ ! -x ./RemoteDrivingClient ]; then
            echo "首次编译：配置并编译客户端..."
            cmake /workspace/client -DCMAKE_PREFIX_PATH=/opt/Qt/6.8.0/gcc_64:/opt/libdatachannel -DCMAKE_BUILD_TYPE=Debug && make -j4
        fi
        [ -x ./RemoteDrivingClient ] && exec ./RemoteDrivingClient --reset-login || { echo "请先编译客户端"; exit 1; }
    '
}
```

**优点**：
- 简单，只需修改一行
- 启动时总是使用最新代码

**缺点**：
- 启动时间稍长（需要编译）
- 运行中修改源码不会自动重新编译

### 方案 2：添加源码监控自动重新编译（类似车端）

创建一个类似 `docker-entrypoint-dev.sh` 的客户端启动脚本：

```bash
#!/bin/bash
# client-dev 容器内：监控源码变化并自动重新编译客户端

BUILD_DIR="/tmp/client-build"
CLIENT_EXEC="${BUILD_DIR}/RemoteDrivingClient"

build_client() {
    echo "[$(date +'%H:%M:%S')] === 编译客户端 ==="
    mkdir -p "${BUILD_DIR}"
    cd "${BUILD_DIR}"
    
    if [ ! -f CMakeCache.txt ]; then
        cmake /workspace/client -DCMAKE_PREFIX_PATH=/opt/Qt/6.8.0/gcc_64:/opt/libdatachannel -DCMAKE_BUILD_TYPE=Debug
    fi
    
    make -j4 || return 1
    return 0
}

run_client() {
    if [ -x "${CLIENT_EXEC}" ]; then
        echo "[$(date +'%H:%M:%S')] === 启动客户端 ==="
        exec "${CLIENT_EXEC}" --reset-login
    else
        echo "[$(date +'%H:%M:%S')] === 错误：找不到可执行文件 ==="
        return 1
    fi
}

# 首次编译和启动
if build_client; then
    run_client &
    CLIENT_PID=$!
else
    echo "[$(date +'%H:%M:%S')] === 首次编译失败，退出 ==="
    exit 1
fi

# 监控源码变化
echo "[$(date +'%H:%M:%S')] === 开始监控源码变化 ==="
LAST_BUILD=0
while true; do
    if inotifywait -r -e modify,create,delete,move --timeout 5 \
        --include '\.(cpp|h|qml|qrc)$' \
        --include 'CMakeLists\.txt$' \
        /workspace/client/src /workspace/client/qml /workspace/client/CMakeLists.txt 2>/dev/null; then
        NOW=$(date +%s)
        if [ $((NOW - LAST_BUILD)) -ge 3 ]; then
            echo "[$(date +'%H:%M:%S')] === 检测到客户端源码变化，重新编译... ==="
            kill $CLIENT_PID 2>/dev/null || true
            wait $CLIENT_PID 2>/dev/null || true
            if build_client; then
                run_client &
                CLIENT_PID=$!
                LAST_BUILD=$(date +%s)
            else
                echo "[$(date +'%H:%M:%S')] === 编译失败，保持当前状态 ==="
            fi
        fi
    fi
done
```

**优点**：
- 完全自动化，修改源码后自动重新编译并重启
- 与车端行为一致

**缺点**：
- 需要创建新脚本
- 需要修改 `start-full-chain.sh` 使用新脚本
- 客户端重启会丢失当前状态

### 方案 3：使用环境变量控制（最简单）

在 `start_client()` 中添加环境变量，控制是否在启动前编译：

```bash
CLIENT_AUTO_BUILD="${CLIENT_AUTO_BUILD:-1}"  # 默认启用

start_client() {
    # ... 现有代码 ...
    $COMPOSE exec -it \
        -e CLIENT_AUTO_BUILD="$CLIENT_AUTO_BUILD" \
        # ... 其他环境变量 ...
        client-dev bash -c '
        mkdir -p /tmp/client-build && cd /tmp/client-build
        if [ "$CLIENT_AUTO_BUILD" = "1" ] && [ -f CMakeCache.txt ]; then
            echo "执行增量编译..."
            make -j4 2>/dev/null || true
        fi
        # ... 启动逻辑 ...
    '
}
```

**使用方法**：
```bash
# 启用自动编译（默认）
bash scripts/start-full-chain.sh manual

# 禁用自动编译
CLIENT_AUTO_BUILD=0 bash scripts/start-full-chain.sh manual
```

## 推荐实施

**短期**：实施方案 1（启动前增量编译）
- 简单快速
- 确保启动时使用最新代码

**长期**：实施方案 2（源码监控）
- 完全自动化
- 开发体验最佳

## 当前使用建议

如果需要修改客户端代码后重新编译：

1. **手动重新编译**：
   ```bash
   docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml exec client-dev bash -c 'cd /tmp/client-build && make -j4'
   ```

2. **重启客户端容器**：
   ```bash
   docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml restart client-dev
   ```

3. **重新运行脚本**：
   ```bash
   bash scripts/start-full-chain.sh manual
   ```
