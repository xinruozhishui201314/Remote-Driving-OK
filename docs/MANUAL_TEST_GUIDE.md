# 远驾接管功能手动测试指南

## 快速开始

### 方式 1：使用 start-full-chain.sh manual（推荐）

```bash
bash scripts/start-full-chain.sh manual
```

**功能**：
- ✅ 启动所有服务节点（Postgres/Keycloak/Backend/ZLM/MQTT/车端）
- ✅ 执行基础验证（检查各节点是否就绪）
- ✅ 执行四路流 E2E 验证
- ✅ **跳过 18s 自动连接测试**（manual 模式）
- ✅ **直接启动客户端界面**供手动操作

**与默认模式的区别**：
- 默认模式：会执行 18s 自动连接测试，然后启动客户端
- manual 模式：跳过自动测试，直接启动客户端，让你手动操作

### 方式 2：快速重启（不清理旧容器）

```bash
bash scripts/start-full-chain.sh manual no-cleanup
```

**适用场景**：快速重启，保留之前的容器和数据

### 方式 3：跳过编译（客户端启动时自动编译）

```bash
bash scripts/start-full-chain.sh manual no-build
```

**适用场景**：代码已编译，跳过编译步骤加快启动速度

---

## 测试步骤

### 1. 启动系统

```bash
bash scripts/start-full-chain.sh manual
```

等待脚本完成：
- 所有节点启动
- 基础验证通过
- 客户端界面启动

### 2. 手动操作测试

在客户端界面中：

1. **连接车辆**：
   - 点击「连接车辆」按钮
   - 等待视频流连接（显示「已连接」）
   - 确认四路视频流正常显示

2. **测试远驾接管**：
   - 点击「远驾接管」按钮
   - **观察按钮文本变化**：应该从「远驾接管」变为「远驾已接管」
   - **观察驾驶模式显示**：应该显示「远驾」

3. **测试取消接管**：
   - 再次点击「远驾已接管」按钮
   - **观察按钮文本变化**：应该从「远驾已接管」变为「远驾接管」
   - **观察驾驶模式显示**：应该显示「自驾」

4. **测试视频流断开**：
   - 点击「已连接」按钮断开连接
   - **观察按钮文本变化**：应该从「已连接」变为「连接车辆」
   - **观察远驾接管按钮**：应该变为禁用状态（灰色）

### 3. 验证功能

在另一个终端运行验证脚本：

```bash
bash scripts/verify-remote-control-complete.sh
```

**验证脚本会检查**：
- ✅ 车端是否检测到指令
- ✅ 车端是否处理成功
- ✅ 车端是否发送确认消息
- ✅ 客户端是否收到确认消息
- ✅ 客户端状态是否更新
- ✅ 客户端模式是否更新
- ✅ QML 界面是否更新

---

## 查看实时日志

### 车端日志（过滤关键信息）

```bash
docker logs remote-driving-vehicle-1 -f | grep REMOTE_CONTROL
```

**预期输出**（点击「远驾接管」后）：
```
[REMOTE_CONTROL] ========== 开始处理控制指令 ==========
[REMOTE_CONTROL] ✓✓✓ 确认是 remote_control 指令，enable=true
[REMOTE_CONTROL] ✓✓✓ handle_control_json 返回: true
[REMOTE_CONTROL] ✓✓✓✓✓ 已成功发送远驾接管确认消息
```

### 客户端日志（过滤关键信息）

```bash
docker logs teleop-client-dev -f | grep REMOTE_CONTROL
```

**预期输出**（收到确认后）：
```
[REMOTE_CONTROL] ========== 收到远驾接管确认消息 ==========
[REMOTE_CONTROL] ✓✓✓ [VEHICLE_STATUS] 远驾接管状态变化: false -> true
[REMOTE_CONTROL] ✓✓✓ [VEHICLE_STATUS] 驾驶模式变化: 自驾 -> 远驾
[REMOTE_CONTROL] ========== [QML] 车端远驾接管状态变化 ==========
```

### 完整日志（不过滤）

```bash
# 车端完整日志
docker logs remote-driving-vehicle-1 -f

# 客户端完整日志
docker logs teleop-client-dev -f
```

---

## 视频日志控制

### 默认行为

- **默认不输出视频帧日志**（减少干扰）
- 只输出关键的状态切换和指令日志

### 启用视频日志（调试视频流问题时）

**方式 1：环境变量**
```bash
export ENABLE_VIDEO_FRAME_LOG=1
bash scripts/start-full-chain.sh manual
```

**方式 2：修改 docker-compose 配置**
在 `docker-compose.yml` 中为 `client-dev` 服务添加：
```yaml
environment:
  - ENABLE_VIDEO_FRAME_LOG=1
```

---

## 常见问题排查

### 问题 1：按钮点击后没有反应

**检查**：
1. 视频流是否已连接（按钮是否显示「已连接」）
2. MQTT 是否连接（查看客户端日志）
3. 车端是否收到指令（查看车端日志）

**命令**：
```bash
# 检查车端是否收到指令
docker logs remote-driving-vehicle-1 --tail 100 | grep REMOTE_CONTROL

# 检查客户端是否发送指令
docker logs teleop-client-dev --tail 100 | grep REMOTE_CONTROL
```

### 问题 2：按钮状态未更新

**检查**：
1. 车端是否发送确认消息（查找"已成功发送远驾接管确认消息"）
2. 客户端是否收到确认消息（查找"收到远驾接管确认消息"）
3. QML 是否更新（查找"车端远驾接管状态变化"）

**命令**：
```bash
# 运行验证脚本
bash scripts/verify-remote-control-complete.sh
```

### 问题 3：驾驶模式未更新

**检查**：
1. 车端确认消息中是否包含 `driving_mode` 字段
2. 客户端是否正确解析 `driving_mode`
3. QML 是否正确绑定 `vehicleStatus.drivingMode`

**命令**：
```bash
# 查看车端发送的确认消息内容
docker logs remote-driving-vehicle-1 --tail 200 | grep -A 2 "确认消息 JSON"

# 查看客户端收到的确认消息
docker logs teleop-client-dev --tail 200 | grep -A 2 "收到远驾接管确认消息"
```

---

## 测试检查清单

- [ ] 系统启动成功（所有节点就绪）
- [ ] 客户端界面正常显示
- [ ] 视频流连接正常（四路视频流显示）
- [ ] 点击「远驾接管」后按钮文本变为「远驾已接管」
- [ ] 点击「远驾接管」后驾驶模式显示变为「远驾」
- [ ] 点击「远驾已接管」后按钮文本变为「远驾接管」
- [ ] 点击「远驾已接管」后驾驶模式显示变为「自驾」
- [ ] 断开视频流后按钮变为禁用状态
- [ ] 验证脚本通过所有检查项

---

## 快速命令参考

```bash
# 启动系统（手动模式）
bash scripts/start-full-chain.sh manual

# 快速重启（不清理）
bash scripts/start-full-chain.sh manual no-cleanup

# 跳过编译（加快启动）
bash scripts/start-full-chain.sh manual no-build

# 查看车端日志（过滤关键信息）
docker logs remote-driving-vehicle-1 -f | grep REMOTE_CONTROL

# 查看客户端日志（过滤关键信息）
docker logs teleop-client-dev -f | grep REMOTE_CONTROL

# 运行验证脚本
bash scripts/verify-remote-control-complete.sh

# 停止所有服务
docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml down
```

---

## 总结

`bash scripts/start-full-chain.sh manual` 是进行手动测试的最佳方式：

1. ✅ **一键启动**：自动启动所有服务节点
2. ✅ **跳过自动测试**：直接进入客户端界面
3. ✅ **手动操作**：可以手动测试各种交互功能
4. ✅ **便于调试**：可以实时查看日志

**推荐流程**：
1. 运行 `bash scripts/start-full-chain.sh manual`
2. 在客户端界面手动操作测试
3. 在另一个终端运行 `bash scripts/verify-remote-control-complete.sh` 验证
4. 查看实时日志确认功能正常
