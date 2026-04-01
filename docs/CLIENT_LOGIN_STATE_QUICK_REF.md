# 客户端登录状态管理 - 快速参考

> **注意**：登录状态清除功能已在**代码层面实现**，不再在脚本中处理。

## 使用方式

### 保留登录状态（默认，推荐日常使用）

```bash
bash scripts/run-client-ui.sh
```

**行为**：
- ✅ 如果之前已登录，直接进入主界面或车辆选择界面
- ✅ 如果之前未登录，显示登录界面
- ✅ 用户体验好，无需每次重新登录

---

### 清除登录状态后启动（推荐测试/验证使用）

```bash
# 方式 1：使用 --reset-login 参数（推荐）
bash scripts/run-client-ui.sh --reset-login

# 方式 2：使用 --clear-login 参数（同 --reset-login）
bash scripts/run-client-ui.sh --clear-login

# 方式 3：使用 -r 简写
bash scripts/run-client-ui.sh -r

# 方式 4：使用环境变量
CLIENT_RESET_LOGIN=1 bash scripts/run-client-ui.sh

# 方式 5：直接运行客户端程序
docker compose exec client-dev bash -c "cd /tmp/client-build && ./RemoteDrivingClient --reset-login"
```

**行为**：
- ✅ 代码层面清除登录状态（调用 `AuthManager::clearCredentials()`）
- ✅ 强制显示登录界面
- ✅ 确保测试环境一致性

**实现原理**：
- 客户端代码检查命令行参数 `--reset-login` 或环境变量 `CLIENT_RESET_LOGIN`
- 如果检测到，创建 `AuthManager` 时不加载保存的凭据
- 调用 `clearCredentials()` 清除内存和 QSettings 中的登录信息

---

### 仅清除登录状态（不启动）

```bash
# 方式 1：使用专用脚本（仍可用，但推荐使用代码方式）
bash scripts/clear-client-login.sh

# 方式 2：直接运行客户端清除（推荐）
docker compose exec client-dev bash -c "cd /tmp/client-build && ./RemoteDrivingClient --reset-login" &
# 然后立即关闭，登录状态已被清除
```

---

## 使用场景推荐

| 场景 | 推荐方式 | 命令 |
|------|---------|------|
| 日常开发 | 保留登录状态 | `bash scripts/run-client-ui.sh` |
| UI 功能验证 | 清除登录状态 | `bash scripts/run-client-ui.sh --reset` |
| 自动化测试 | 清除登录状态 | `CLIENT_RESET_LOGIN=1 bash scripts/run-client-ui.sh` |
| 演示准备 | 清除登录状态 | `bash scripts/run-client-ui.sh --reset` |

---

## 参数说明

| 参数 | 说明 | 示例 |
|------|------|------|
| 无参数 | 保留登录状态（默认） | `bash scripts/run-client-ui.sh` |
| `--reset-login` | 清除登录状态后启动（代码实现） | `bash scripts/run-client-ui.sh --reset-login` |
| `--clear-login` | 清除登录状态后启动（同 --reset-login） | `bash scripts/run-client-ui.sh --clear-login` |
| `-r` | 清除登录状态后启动（简写） | `bash scripts/run-client-ui.sh -r` |

**注意**：参数会传递给客户端程序，由代码处理清除逻辑。

---

## 环境变量

| 环境变量 | 值 | 说明 |
|----------|-----|------|
| `CLIENT_RESET_LOGIN` | `1` 或 `true` | 清除登录状态后启动 |

---

## 相关文档

- 详细文档：`docs/CLIENT_LOGIN_STATE_MANAGEMENT.md`
- UI 验证指南：`docs/CLIENT_UI_VERIFICATION_GUIDE.md`
