# 客户端登录状态管理

## Executive Summary

客户端使用 Qt 的 `QSettings` 来持久化登录状态，包括用户名、密码（token）、服务器地址等。本文档说明如何管理登录状态，包括保留和清除登录状态的方法。

**设计原则**：
- **默认行为**：保留登录状态，提升用户体验（避免每次都要重新登录）
- **开发/测试场景**：提供选项清除登录状态，确保测试一致性
- **灵活性**：支持命令行参数和环境变量控制

---

## 1. 登录状态持久化机制

### 1.1 存储位置

客户端使用 `QSettings` 存储登录信息，配置文件位置：

**Linux 容器内**：
- `~/.config/<org>/<app>.conf`
- 或 `~/.config/RemoteDriving/RemoteDrivingClient.conf`

**实际路径示例**：
```bash
/home/user/.config/RemoteDriving/RemoteDrivingClient.conf
```

### 1.2 存储内容

`AuthManager` 在登录成功后会保存以下信息：

```cpp
settings.setValue("auth/username", m_username);
settings.setValue("auth/token", m_authToken);
settings.setValue("auth/loggedIn", m_isLoggedIn);
settings.setValue("auth/serverUrl", m_serverUrl);
```

### 1.3 启动时恢复

`AuthManager` 构造函数中调用 `loadCredentials()`：

```cpp
AuthManager::AuthManager(QObject *parent)
    : QObject(parent)
{
    loadCredentials();  // 从 QSettings 恢复登录状态
}
```

如果之前已登录，启动时会自动恢复登录状态，`isLoggedIn` 为 `true`。

---

## 2. 登录状态管理方法

### 2.1 保留登录状态（默认行为）

**适用场景**：
- 生产环境使用
- 日常开发测试
- 用户体验优先的场景

**启动方式**：
```bash
# 方式 1：直接启动（默认保留登录状态）
bash scripts/run-client-ui.sh

# 方式 2：明确指定保留（可选）
bash scripts/run-client-ui.sh --keep
```

**行为**：
- 如果之前已登录，启动时直接显示车辆选择对话框（如果未选择车辆）或主界面（如果已选择车辆）
- 如果之前未登录，显示登录界面

**优点**：
- ✅ 用户体验好，无需每次重新登录
- ✅ 适合生产环境
- ✅ 开发时快速进入主界面

**缺点**：
- ⚠️ 测试时可能受上次状态影响
- ⚠️ 调试登录流程时需要手动清除

---

### 2.2 清除登录状态后启动

**适用场景**：
- UI 功能验证（确保从登录界面开始）
- 登录流程测试
- 确保测试环境一致性
- 演示和演示准备

**启动方式**：
```bash
# 方式 1：使用 --reset 参数
bash scripts/run-client-ui.sh --reset

# 方式 2：使用 --clear 参数（同 --reset）
bash scripts/run-client-ui.sh --clear

# 方式 3：使用 -r 简写
bash scripts/run-client-ui.sh -r

# 方式 4：使用环境变量
CLIENT_RESET_LOGIN=1 bash scripts/run-client-ui.sh
```

**行为**：
- 启动前自动清除 QSettings 中的登录信息
- 启动时强制显示登录界面
- 确保测试环境的一致性

**优点**：
- ✅ 测试环境一致
- ✅ 便于验证登录流程
- ✅ 避免上次状态干扰

**缺点**：
- ⚠️ 每次都需要重新登录
- ⚠️ 不适合日常开发使用

---

### 2.3 手动清除登录状态

**适用场景**：
- 需要清除登录状态但不立即启动客户端
- 批量清除多个容器的登录状态

**方法**：
```bash
# 使用专用脚本
bash scripts/clear-client-login.sh

# 或手动执行
docker compose exec client-dev bash -c "
    find ~/.config -name '*RemoteDriving*' -type f 2>/dev/null | xargs rm -f || true
    find ~/.config -name '*remote-driving*' -type f 2>/dev/null | xargs rm -f || true
    echo '登录状态已清除'
"
```

---

## 3. 启动脚本参数说明

### 3.1 `run-client-ui.sh` 参数

| 参数 | 简写 | 说明 | 示例 |
|------|------|------|------|
| `--reset` | - | 清除登录状态后启动 | `bash scripts/run-client-ui.sh --reset` |
| `--clear` | - | 清除登录状态后启动（同 --reset） | `bash scripts/run-client-ui.sh --clear` |
| `-r` | - | 清除登录状态后启动（简写） | `bash scripts/run-client-ui.sh -r` |
| 无参数 | - | 保留登录状态（默认） | `bash scripts/run-client-ui.sh` |

### 3.2 环境变量

| 环境变量 | 值 | 说明 | 示例 |
|----------|-----|------|------|
| `CLIENT_RESET_LOGIN` | `1` 或 `true` | 清除登录状态后启动 | `CLIENT_RESET_LOGIN=1 bash scripts/run-client-ui.sh` |

---

## 4. 使用场景推荐

### 4.1 日常开发

**推荐**：保留登录状态（默认）

```bash
bash scripts/run-client-ui.sh
```

**原因**：
- 快速进入主界面进行功能开发
- 无需每次重新登录
- 提升开发效率

---

### 4.2 UI 功能验证

**推荐**：清除登录状态

```bash
bash scripts/run-client-ui.sh --reset
```

**原因**：
- 确保从登录界面开始验证
- 验证完整的用户流程
- 避免上次状态干扰

**验证步骤**：
1. 清除登录状态：`bash scripts/run-client-ui.sh --reset`
2. 启动客户端
3. 验证登录界面显示
4. 执行登录操作
5. 验证车辆选择界面
6. 验证主界面

---

### 4.3 自动化测试

**推荐**：在测试脚本中清除登录状态

```bash
#!/bin/bash
# 测试脚本示例

# 清除登录状态
bash scripts/clear-client-login.sh

# 启动客户端（应该显示登录界面）
bash scripts/run-client-ui.sh

# 执行测试...
```

**原因**：
- 确保测试环境一致
- 可重复执行
- 避免状态污染

---

### 4.4 演示和演示准备

**推荐**：清除登录状态

```bash
bash scripts/run-client-ui.sh --reset
```

**原因**：
- 展示完整的用户流程
- 从登录界面开始演示
- 给观众完整的体验

---

## 5. 实现细节

### 5.1 启动脚本逻辑

`scripts/run-client-ui.sh` 的处理流程：

```bash
# 1. 解析参数
RESET_LOGIN=false
if [ "$1" == "--reset" ] || [ "$1" == "--clear" ] || [ "$1" == "-r" ]; then
    RESET_LOGIN=true
elif [ "$CLIENT_RESET_LOGIN" == "1" ]; then
    RESET_LOGIN=true
fi

# 2. 清除登录状态（如果指定）
if [ "$RESET_LOGIN" == "true" ]; then
    # 清除 QSettings 配置文件
    docker compose exec client-dev bash -c "
        find ~/.config -name '*RemoteDriving*' -type f | xargs rm -f || true
    "
fi

# 3. 编译客户端（如需要）

# 4. 启动客户端
docker compose exec client-dev bash -c "./RemoteDrivingClient"
```

### 5.2 清除登录状态脚本

`scripts/clear-client-login.sh` 的实现：

```bash
docker compose exec client-dev bash -c "
    find ~/.config -name '*RemoteDriving*' -type f 2>/dev/null | xargs rm -f || true
    find ~/.config -name '*remote-driving*' -type f 2>/dev/null | xargs rm -f || true
    echo '登录状态已清除'
"
```

---

## 6. 最佳实践

### 6.1 开发阶段

1. **首次启动**：使用 `--reset` 确保从登录界面开始
2. **后续开发**：使用默认方式（保留登录状态）快速进入主界面
3. **功能验证**：使用 `--reset` 验证完整流程

### 6.2 测试阶段

1. **单元测试**：每个测试用例前清除登录状态
2. **集成测试**：测试脚本开始时清除登录状态
3. **UI 验证**：使用 `--reset` 确保一致性

### 6.3 生产部署

1. **默认行为**：保留登录状态（用户体验优先）
2. **提供选项**：允许用户手动退出登录
3. **安全考虑**：考虑 token 过期时间，自动清除过期状态

---

## 7. 故障排查

### 7.1 问题：启动时没有显示登录界面

**可能原因**：
- 之前已登录，登录状态被保留

**解决方法**：
```bash
# 清除登录状态
bash scripts/run-client-ui.sh --reset
```

### 7.2 问题：每次启动都要重新登录

**可能原因**：
- QSettings 配置文件权限问题
- 配置文件被意外删除

**检查方法**：
```bash
# 检查配置文件是否存在
docker compose exec client-dev bash -c "ls -la ~/.config/*RemoteDriving* 2>/dev/null || echo '配置文件不存在'"
```

### 7.3 问题：清除登录状态后仍然显示主界面

**可能原因**：
- 配置文件清除失败
- 有其他配置文件位置

**解决方法**：
```bash
# 手动清除所有可能的配置文件
docker compose exec client-dev bash -c "
    find ~/.config -name '*RemoteDriving*' -type f -delete
    find ~/.config -name '*remote-driving*' -type f -delete
    rm -rf ~/.config/RemoteDriving
"
```

---

## 8. 相关文档

- `scripts/run-client-ui.sh`：客户端启动脚本
- `scripts/clear-client-login.sh`：清除登录状态脚本
- `docs/CLIENT_UI_VERIFICATION_GUIDE.md`：UI 验证指南
- `client/src/authmanager.cpp`：AuthManager 实现

---

## 9. 总结

**默认行为**：保留登录状态，提升用户体验

**清除方法**：
- 命令行参数：`--reset`、`--clear`、`-r`
- 环境变量：`CLIENT_RESET_LOGIN=1`
- 专用脚本：`bash scripts/clear-client-login.sh`

**使用建议**：
- 日常开发：保留登录状态（默认）
- 功能验证：清除登录状态（`--reset`）
- 自动化测试：测试前清除登录状态
