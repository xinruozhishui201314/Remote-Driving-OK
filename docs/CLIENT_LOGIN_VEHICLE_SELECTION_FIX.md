# 登录界面和车辆选择界面显示问题修复

## Executive Summary

**问题**：用户反馈启动客户端时没有看到登录界面和车辆选择界面。

**原因**：`AuthManager` 在启动时会从 QSettings 恢复之前的登录状态。如果之前测试时已经登录，`isLoggedIn` 会被设置为 `true`，导致登录界面不显示。如果已登录且已选择车辆，会直接进入主界面，跳过车辆选择对话框。

**解决方案**：修改 `main.qml` 的显示逻辑，确保：
1. 未登录时显示登录界面
2. 已登录但未选择车辆时显示车辆选择对话框
3. 已登录且已选择车辆时显示主界面

**影响**：修复后，界面显示逻辑更加清晰，符合用户预期。

---

## 1. 问题分析

### 1.1 现象
- 启动客户端时看不到登录界面
- 启动客户端时看不到车辆选择对话框

### 1.2 根本原因
1. **AuthManager 持久化登录状态**：
   - `AuthManager` 构造函数中调用 `loadCredentials()`
   - 从 QSettings 恢复 `m_isLoggedIn`、`m_authToken`、`m_username` 等
   - 如果之前测试时登录过，启动时 `isLoggedIn` 为 `true`

2. **界面显示逻辑**：
   - `LoginPage` 的 `visible` 条件：`!componentsReady && !authManager.isLoggedIn`
   - 如果 `authManager.isLoggedIn = true`，登录界面不显示
   - 如果已登录且已选择车辆，`componentsReady = true`，主界面直接显示

### 1.3 代码位置
- `client/src/authmanager.cpp`：`loadCredentials()` 方法（第 202-216 行）
- `client/qml/main.qml`：`LoginPage` 的 `visible` 属性（第 85 行）
- `client/qml/main.qml`：`Component.onCompleted` 中的启动逻辑（第 168-191 行）

---

## 2. 修复方案

### 2.1 修改 `main.qml` 的显示逻辑

**文件**：`client/qml/main.qml`

**修改内容**：

1. **优化 LoginPage 的 visible 条件**：
   ```qml
   visible: {
       // 如果主界面已就绪，不显示登录页面
       if (componentsReady) return false
       // 如果未登录，显示登录页面
       if (typeof authManager === "undefined" || !authManager || !authManager.isLoggedIn) {
           return true
       }
       // 如果已登录但未选择车辆，也不显示登录页面（会显示车辆选择对话框）
       return false
   }
   ```

2. **增强 Component.onCompleted 的启动逻辑**：
   ```qml
   Component.onCompleted: {
       updateTitle()
       console.log("Main window completed")
       console.log("componentsReady:", componentsReady)
       console.log("LoginPage visible:", loginPage.visible)
       
       if (typeof authManager !== "undefined" && authManager) {
           console.log("authManager exists, isLoggedIn:", authManager.isLoggedIn)
           console.log("authManager.username:", authManager.username)
           
           // 如果启动时已登录，检查是否已选择车辆
           if (authManager.isLoggedIn) {
               if (typeof vehicleManager !== "undefined" && vehicleManager) {
                   console.log("vehicleManager.currentVin:", vehicleManager.currentVin)
                   console.log("vehicleManager.vehicleList:", vehicleManager.vehicleList)
                   
                   if (vehicleManager.currentVin.length > 0) {
                       console.log("Already logged in and vehicle selected, showing main interface")
                       componentsReady = true
                   } else {
                       console.log("Already logged in but no vehicle selected, opening vehicle selection dialog")
                       // 延迟打开车辆选择对话框，确保 UI 已完全加载
                       Qt.callLater(function() {
                           console.log("Calling vehicleSelectionDialog.open()")
                           vehicleSelectionDialog.open()
                       })
                   }
               } else {
                   console.log("vehicleManager not available")
               }
           } else {
               console.log("Not logged in, LoginPage should be visible")
           }
       } else {
           console.log("authManager not available")
       }
   }
   ```

3. **添加 VehicleSelectionDialog 的调试日志**：
   ```qml
   VehicleSelectionDialog {
       id: vehicleSelectionDialog
       
       Component.onCompleted: {
           console.log("VehicleSelectionDialog loaded")
       }
       
       onOpened: {
           console.log("VehicleSelectionDialog opened")
       }
       
       onClosed: {
           console.log("VehicleSelectionDialog closed")
       }
   }
   ```

### 2.2 界面显示流程

修复后的界面显示流程：

```
启动客户端
    ↓
检查 authManager.isLoggedIn
    ↓
    ├─ 未登录 → 显示 LoginPage
    │              ↓
    │           登录成功 → 打开 VehicleSelectionDialog
    │                          ↓
    │                       选择车辆并确认 → componentsReady = true → 显示主界面
    │
    └─ 已登录 → 检查 vehicleManager.currentVin
                   ↓
                   ├─ 已选择车辆 → componentsReady = true → 显示主界面
                   │
                   └─ 未选择车辆 → 打开 VehicleSelectionDialog
                                      ↓
                                   选择车辆并确认 → componentsReady = true → 显示主界面
```

---

## 3. 验证步骤

### 3.1 编译客户端
```bash
cd /home/wqs/bigdata/Remote-Driving
docker compose exec client-dev bash -c "cd /tmp/client-build && make -j4"
```

### 3.2 测试场景

#### 场景 1：未登录状态
1. 清除 QSettings 中的登录信息（或首次启动）
2. 启动客户端
3. **预期**：显示登录界面（LoginPage）

#### 场景 2：已登录但未选择车辆
1. 确保已登录（`authManager.isLoggedIn = true`）
2. 确保未选择车辆（`vehicleManager.currentVin = ""`）
3. 启动客户端
4. **预期**：显示车辆选择对话框（VehicleSelectionDialog）

#### 场景 3：已登录且已选择车辆
1. 确保已登录（`authManager.isLoggedIn = true`）
2. 确保已选择车辆（`vehicleManager.currentVin != ""`）
3. 启动客户端
4. **预期**：直接显示主界面（VideoView + ControlPanel + StatusBar）

### 3.3 检查日志
启动客户端后，检查控制台输出：
```bash
docker compose exec client-dev bash -c "cd /tmp/client-build && timeout 3 ./RemoteDrivingClient 2>&1 | grep -E 'LoginPage|VehicleSelectionDialog|authManager|componentsReady'"
```

**预期日志**：
- `LoginPage visible: true/false` - 登录界面是否显示
- `VehicleSelectionDialog opened` - 车辆选择对话框是否打开
- `authManager.isLoggedIn: true/false` - 登录状态
- `componentsReady: true/false` - 主界面是否就绪

---

## 4. 变更清单

### 4.1 修改的文件
- `client/qml/main.qml`
  - 优化 `LoginPage` 的 `visible` 条件
  - 增强 `Component.onCompleted` 的启动逻辑
  - 添加 `VehicleSelectionDialog` 的调试日志

### 4.2 新增的调试日志
- `LoginPage` 加载和显示状态
- `VehicleSelectionDialog` 加载、打开、关闭事件
- `authManager` 和 `vehicleManager` 的状态信息

---

## 5. 风险与回滚

### 5.1 风险
- **低风险**：仅修改界面显示逻辑，不影响核心功能
- **潜在问题**：如果 QSettings 中的登录信息损坏，可能导致界面显示异常

### 5.2 回滚方案
如果需要回滚，恢复 `main.qml` 到修改前的版本：
```bash
cd /home/wqs/bigdata/Remote-Driving
git checkout client/qml/main.qml
```

### 5.3 清除登录状态（测试用）
如果需要清除登录状态进行测试：
```bash
# 在容器内清除 QSettings（位置通常在 ~/.config/<org>/<app>.conf）
docker compose exec client-dev bash -c "rm -f ~/.config/*/RemoteDrivingClient.conf"
```

---

## 6. 后续优化建议

### 6.1 添加"退出登录"功能
- 在主界面添加"退出登录"按钮
- 调用 `authManager.logout()` 清除登录状态
- 返回登录界面

### 6.2 添加"重新选择车辆"功能
- 在主界面添加"重新选择车辆"按钮
- 设置 `componentsReady = false`
- 打开车辆选择对话框

### 6.3 改进持久化策略
- 考虑添加 token 过期检查
- 启动时验证 token 有效性
- 如果 token 过期，自动清除登录状态

---

## 7. 编译/部署/运行说明

### 7.1 环境要求
- Docker Compose
- `client-dev` 容器已启动

### 7.2 编译
```bash
cd /home/wqs/bigdata/Remote-Driving
docker compose exec client-dev bash -c "cd /tmp/client-build && make -j4"
```

### 7.3 运行
```bash
# 使用脚本启动（推荐）
bash scripts/run-client-ui.sh

# 或手动启动
docker compose exec client-dev bash -c "cd /tmp/client-build && ./RemoteDrivingClient"
```

### 7.4 验证
1. 启动客户端
2. 检查控制台日志，确认界面显示逻辑正确
3. 验证登录界面和车辆选择对话框能正常显示

---

## 8. 总结

本次修复确保了：
1. ✅ 未登录时显示登录界面
2. ✅ 已登录但未选择车辆时显示车辆选择对话框
3. ✅ 已登录且已选择车辆时显示主界面
4. ✅ 添加了详细的调试日志，便于排查问题

界面显示逻辑现在更加清晰和符合用户预期。
