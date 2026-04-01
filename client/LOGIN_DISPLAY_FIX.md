# 登录界面不显示修复说明

## 现象

启动客户端后没有显示登录界面。

## 原因

1. **启动延迟过长**：原先用两个定时器（500ms + 200ms）共 700ms 后才打开对话框，容易让人误以为卡住。
2. **保存的登录状态**：若上次用 123/123 登录并保存了凭据，`AuthManager.loadCredentials()` 会恢复 `isLoggedIn=true`，启动时逻辑会直接打开**车辆选择**界面而不是登录界面，因此看不到“登录界面”。

## 修改

### 1. 启动时始终先显示登录界面

- **main.qml**：`showLoginOrVehicleTimer` 触发后**固定先打开登录对话框**，不再根据 `authManager.isLoggedIn` 决定是登录框还是选车框。
- 流程统一为：**启动 → 登录界面 → 登录成功 → 车辆选择 → 确认并进入驾驶 → 主页面**。

### 2. 缩短启动延迟

- 将原先 500ms + 200ms 合并为**单次 150ms** 定时器 `showLoginOrVehicleTimer`，窗口就绪后尽快弹出登录框。

### 3. 备用逻辑

- 使用 **onVisibilityChanged**：当窗口变为可见且当前未登录且登录框未打开时，再次调用 `loginDialog.open()`，避免因时序问题导致登录框未显示。

### 4. QML 警告修复

- `onVisibilityChanged` 使用带形参形式：`onVisibilityChanged: function(visibility) { ... }`，消除“Parameter is not declared”类警告。

## 验证

```bash
cd /workspaces/Remote-Driving/client
bash run.sh
```

- 约 0.15 秒内应弹出**登录界面**（账户/密码/服务器地址）。
- 若环境有 X11 且已挂载，登录框会正常显示；若提示“X11 socket 未挂载”，则仅逻辑正确，需在宿主机挂载 `/tmp/.X11-unix` 后才能看到 GUI。
