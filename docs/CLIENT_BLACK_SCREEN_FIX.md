# 客户端黑屏问题分析与修复

## Executive Summary

**问题**：客户端启动后窗口黑屏，无法看到登录界面。

**根本原因**：
1. **窗口大小超出屏幕**：默认窗口大小 1920x1080 可能超出屏幕分辨率
2. **窗口位置问题**：窗口可能在屏幕外或不可见区域
3. **Qt 平台插件配置**：未显式设置 `QT_QPA_PLATFORM=xcb`

**解决方案**：
1. ✅ 修改窗口大小为自适应（屏幕 90%，最小 1280x720）
2. ✅ 窗口自动居中显示
3. ✅ 启动脚本添加 Qt 平台插件环境变量
4. ✅ 创建诊断修复脚本

---

## 1. 问题分析

### 1.1 症状

- 客户端启动后窗口黑屏
- 日志显示 QML 已加载，LoginPage visible: true
- 但用户看不到任何界面

### 1.2 可能原因

| 原因 | 可能性 | 说明 |
|------|--------|------|
| 窗口大小超出屏幕 | ⭐⭐⭐⭐⭐ | 默认 1920x1080 可能超出屏幕分辨率 |
| 窗口位置在屏幕外 | ⭐⭐⭐⭐ | 窗口可能被放置在不可见位置 |
| Qt 平台插件问题 | ⭐⭐⭐ | 未设置 QT_QPA_PLATFORM |
| X11 权限问题 | ⭐⭐ | 已检查，权限正常 |
| 渲染问题 | ⭐ | OpenGL/GPU 相关，可能性较低 |

---

## 2. 修复方案

### 2.1 修改窗口大小和位置（已修复）

**文件**：`client/qml/main.qml`

**修改前**：
```qml
ApplicationWindow {
    id: window
    width: 1920
    height: 1080
    visible: true
    ...
}
```

**修改后**：
```qml
ApplicationWindow {
    id: window
    // 自适应窗口大小：使用屏幕尺寸的 90%，最小 1280x720，最大 1920x1080
    width: Math.min(Math.max(1280, Screen.width * 0.9), 1920)
    height: Math.min(Math.max(720, Screen.height * 0.9), 1080)
    minimumWidth: 1280
    minimumHeight: 720
    visible: true
    ...
    x: (Screen.width - width) / 2  // 居中显示
    y: (Screen.height - height) / 2
}
```

**效果**：
- ✅ 窗口大小自适应屏幕
- ✅ 自动居中显示
- ✅ 最小尺寸限制，确保可用性

### 2.2 改进启动脚本（已修复）

**文件**：`scripts/start-full-chain.sh`

**修改**：添加 Qt 平台插件环境变量

```bash
$COMPOSE exec -it \
    -e DISPLAY="$DISPLAY" \
    -e QT_QPA_PLATFORM=xcb \          # 新增：显式设置 Qt 平台插件
    -e QT_LOGGING_RULES="qt.qpa.*=false" \  # 新增：减少 Qt 日志噪音
    -e ZLM_VIDEO_URL="$ZLM_VIDEO_URL" \
    -e MQTT_BROKER_URL="$MQTT_BROKER_URL" \
    -e CLIENT_RESET_LOGIN=1 \
    client-dev bash -c '...'
```

### 2.3 创建诊断修复脚本

**文件**：`scripts/fix-client-black-screen.sh`

**功能**：
- 检查 DISPLAY 环境变量
- 检查 X11 权限
- 检查 X11 socket
- 检查容器内配置
- 检查 Qt 平台插件
- 提供修复建议和自动修复

**使用方法**：
```bash
bash scripts/fix-client-black-screen.sh
```

---

## 3. 验证步骤

### 3.1 重新编译客户端

```bash
# 在容器内重新编译（QML 文件修改需要重新编译）
docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml exec client-dev bash -c 'cd /tmp/client-build && make -j4'
```

### 3.2 启动客户端

```bash
# 方式 1：使用修复后的启动脚本
bash scripts/start-full-chain.sh manual

# 方式 2：使用诊断修复脚本
bash scripts/fix-client-black-screen.sh

# 方式 3：手动启动（带环境变量）
export DISPLAY=:0
export QT_QPA_PLATFORM=xcb
xhost +local:docker
docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml exec -it \
  -e DISPLAY=$DISPLAY \
  -e QT_QPA_PLATFORM=xcb \
  client-dev bash -c 'cd /tmp/client-build && ./RemoteDrivingClient --reset-login'
```

### 3.3 验证结果

**预期结果**：
- ✅ 客户端窗口正常显示
- ✅ 窗口大小适合屏幕
- ✅ 窗口居中显示
- ✅ 登录界面可见

---

## 4. 故障排查

### 4.1 窗口仍然黑屏

**检查项**：

1. **窗口是否在屏幕外**
   ```bash
   # 尝试移动窗口（Alt+F7 或 Alt+鼠标拖动）
   # 或检查窗口管理器
   ```

2. **检查窗口大小**
   ```bash
   # 查看屏幕分辨率
   xrandr | grep "*"
   
   # 如果屏幕分辨率小于 1280x720，需要进一步降低窗口大小
   ```

3. **查看详细日志**
   ```bash
   docker logs teleop-client-dev 2>&1 | tail -50
   ```

4. **尝试最小窗口**
   ```qml
   // 临时修改 main.qml
   width: 1280
   height: 720
   ```

### 4.2 窗口显示但内容空白

**可能原因**：
- QML 文件路径问题
- 字体问题
- 渲染问题

**排查**：
```bash
# 检查 QML 文件路径
docker exec teleop-client-dev bash -c 'ls -la /workspace/client/qml/main.qml'

# 检查字体
docker exec teleop-client-dev bash -c 'fc-list | grep -i "wenquan\|noto"'
```

### 4.3 X11 连接问题

**错误信息**：
```
QXcbConnection: Could not connect to display
```

**解决**：
```bash
# 检查 DISPLAY
echo $DISPLAY

# 检查 X11 socket
ls -la /tmp/.X11-unix/

# 设置权限
xhost +local:docker

# 检查容器内访问
docker exec teleop-client-dev bash -c 'ls -la /tmp/.X11-unix/'
```

---

## 5. 预防措施

### 5.1 窗口大小最佳实践

- ✅ 使用自适应大小（基于屏幕尺寸）
- ✅ 设置最小/最大尺寸限制
- ✅ 窗口自动居中
- ✅ 避免硬编码大尺寸（如 1920x1080）

### 5.2 启动脚本最佳实践

- ✅ 显式设置 `QT_QPA_PLATFORM=xcb`
- ✅ 设置 `QT_LOGGING_RULES` 减少日志噪音
- ✅ 检查 DISPLAY 环境变量
- ✅ 设置 X11 权限

### 5.3 测试建议

- ✅ 在不同分辨率下测试（1280x720, 1920x1080, 2560x1440）
- ✅ 测试窗口最小化/最大化
- ✅ 测试多显示器环境

---

## 6. 相关文件

- `client/qml/main.qml` - 主窗口配置（已修复）
- `scripts/start-full-chain.sh` - 启动脚本（已修复）
- `scripts/fix-client-black-screen.sh` - 诊断修复脚本（新增）
- `docs/CLIENT_BLACK_SCREEN_FIX.md` - 本文档

---

## 7. 总结

**已修复**：
1. ✅ 窗口大小自适应屏幕
2. ✅ 窗口自动居中
3. ✅ 启动脚本添加 Qt 平台插件配置
4. ✅ 创建诊断修复脚本

**下一步**：
1. 重新编译客户端
2. 使用修复后的启动脚本启动
3. 验证窗口正常显示

**如果问题仍然存在**：
- 运行诊断脚本：`bash scripts/fix-client-black-screen.sh`
- 查看详细日志
- 检查屏幕分辨率和窗口管理器设置
