# 客户端登录状态管理 - 代码实现

## Executive Summary

登录状态清除功能已**在代码层面实现**，而不是在启动脚本中。这样更符合软件设计原则，逻辑更清晰，维护更方便。

**实现方式**：
- 在 `AuthManager` 中添加 `clearCredentials()` 方法
- 在 `main.cpp` 中检查环境变量或命令行参数
- 根据检查结果决定是否清除登录状态

---

## 1. 代码实现

### 1.1 AuthManager 修改

**文件**：`client/src/authmanager.h`

添加了新的构造函数和清除方法：

```cpp
public:
    explicit AuthManager(QObject *parent = nullptr);
    /** 构造函数，可选择是否加载保存的凭据 */
    explicit AuthManager(QObject *parent, bool loadSavedCredentials);
    
public slots:
    void login(const QString &username, const QString &password, const QString &serverUrl);
    void logout();
    void refreshToken();
    /** 清除所有保存的登录凭据（用于测试/重置） */
    void clearCredentials();
```

**文件**：`client/src/authmanager.cpp`

实现：

```cpp
AuthManager::AuthManager(QObject *parent)
    : AuthManager(parent, true)  // 默认加载保存的凭据
{
}

AuthManager::AuthManager(QObject *parent, bool loadSavedCredentials)
    : QObject(parent)
    , m_networkManager(new QNetworkAccessManager(this))
{
    if (loadSavedCredentials) {
        loadCredentials();
    } else {
        qDebug() << "AuthManager: Skipping loadCredentials (reset mode)";
    }
}

void AuthManager::clearCredentials()
{
    qDebug() << "AuthManager: Clearing saved credentials";
    
    // 清除内存中的状态
    m_username.clear();
    m_authToken.clear();
    m_serverUrl.clear();
    m_isLoggedIn = false;
    
    // 清除 QSettings 中的保存值
    QSettings settings;
    settings.remove("auth/username");
    settings.remove("auth/token");
    settings.remove("auth/loggedIn");
    settings.remove("auth/serverUrl");
    
    // 发射信号通知状态变化
    emit usernameChanged(m_username);
    emit authTokenChanged(m_authToken);
    emit serverUrlChanged(m_serverUrl);
    emit loginStatusChanged(false);
    
    qDebug() << "AuthManager: Credentials cleared";
}
```

### 1.2 main.cpp 修改

**文件**：`client/src/main.cpp`

添加了环境变量和命令行参数检查：

```cpp
#include <QProcessEnvironment>
#include <QCoreApplication>

// 在创建 AuthManager 之前
// 检查是否需要清除登录状态
bool resetLogin = false;

// 检查环境变量
QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
QString resetLoginEnv = env.value("CLIENT_RESET_LOGIN", "").toLower();
if (resetLoginEnv == "1" || resetLoginEnv == "true" || resetLoginEnv == "yes") {
    resetLogin = true;
    qDebug() << "CLIENT_RESET_LOGIN environment variable set, will reset login state";
}

// 检查命令行参数
QStringList args = QCoreApplication::arguments();
if (args.contains("--reset-login") || args.contains("--clear-login") || args.contains("-r")) {
    resetLogin = true;
    qDebug() << "Command line argument detected, will reset login state";
}

// 创建 AuthManager，根据 resetLogin 决定是否加载保存的凭据
auto authManager = std::make_unique<AuthManager>(&app, !resetLogin);

// 如果指定清除登录状态，调用 clearCredentials 确保完全清除
if (resetLogin) {
    qDebug() << "Resetting login state...";
    authManager->clearCredentials();
    qDebug() << "  ✓ Login state reset";
}
```

---

## 2. 使用方式

### 2.1 保留登录状态（默认）

```bash
# 方式 1：直接启动
bash scripts/run-client-ui.sh

# 方式 2：直接运行客户端
docker compose exec client-dev bash -c "cd /tmp/client-build && ./RemoteDrivingClient"
```

**行为**：
- 加载保存的登录状态
- 如果之前已登录，直接进入主界面或车辆选择界面
- 如果之前未登录，显示登录界面

### 2.2 清除登录状态

**方式 1：使用命令行参数**

```bash
# 通过启动脚本
bash scripts/run-client-ui.sh --reset-login

# 或直接运行客户端
docker compose exec client-dev bash -c "cd /tmp/client-build && ./RemoteDrivingClient --reset-login"
```

**方式 2：使用环境变量**

```bash
# 通过启动脚本（脚本会自动设置环境变量）
CLIENT_RESET_LOGIN=1 bash scripts/run-client-ui.sh

# 或直接运行客户端
docker compose exec -e CLIENT_RESET_LOGIN=1 client-dev bash -c "cd /tmp/client-build && ./RemoteDrivingClient"
```

**方式 3：使用简写参数**

```bash
bash scripts/run-client-ui.sh -r
```

**行为**：
- 不加载保存的登录状态
- 调用 `clearCredentials()` 清除所有保存的凭据
- 强制显示登录界面

---

## 3. 支持的参数和环境变量

### 3.1 命令行参数

| 参数 | 说明 | 示例 |
|------|------|------|
| `--reset-login` | 清除登录状态 | `./RemoteDrivingClient --reset-login` |
| `--clear-login` | 清除登录状态（同 --reset-login） | `./RemoteDrivingClient --clear-login` |
| `-r` | 清除登录状态（简写） | `./RemoteDrivingClient -r` |

### 3.2 环境变量

| 环境变量 | 值 | 说明 | 示例 |
|----------|-----|------|------|
| `CLIENT_RESET_LOGIN` | `1`, `true`, `yes` | 清除登录状态 | `CLIENT_RESET_LOGIN=1 ./RemoteDrivingClient` |

---

## 4. 启动脚本更新

**文件**：`scripts/run-client-ui.sh`

启动脚本已更新，**不再在脚本中清除登录状态**，而是：

1. 解析命令行参数（`--reset-login`, `--clear-login`, `-r`）
2. 设置环境变量 `CLIENT_RESET_LOGIN=1`（如果指定清除）
3. 将命令行参数传递给客户端程序
4. 客户端代码根据参数和环境变量决定是否清除登录状态

**优势**：
- ✅ 逻辑集中在代码中，更易维护
- ✅ 脚本只负责传递参数，不处理业务逻辑
- ✅ 可以直接运行客户端程序，不依赖脚本

---

## 5. 工作流程

### 5.1 保留登录状态流程

```
启动客户端
    ↓
main.cpp: 检查环境变量和命令行参数
    ↓
resetLogin = false
    ↓
创建 AuthManager(&app, true)  // 加载保存的凭据
    ↓
loadCredentials()  // 从 QSettings 加载
    ↓
如果已登录 → 显示主界面或车辆选择界面
如果未登录 → 显示登录界面
```

### 5.2 清除登录状态流程

```
启动客户端 --reset-login
    ↓
main.cpp: 检查环境变量和命令行参数
    ↓
resetLogin = true
    ↓
创建 AuthManager(&app, false)  // 不加载保存的凭据
    ↓
clearCredentials()  // 清除内存和 QSettings
    ↓
显示登录界面
```

---

## 6. 优势对比

### 6.1 代码实现 vs 脚本实现

| 方面 | 代码实现（当前） | 脚本实现（之前） |
|------|----------------|----------------|
| **逻辑位置** | 代码中，统一管理 | 脚本中，分散管理 |
| **可维护性** | ✅ 高，集中管理 | ⚠️ 低，分散在多个脚本 |
| **可测试性** | ✅ 高，可单元测试 | ⚠️ 低，需要集成测试 |
| **灵活性** | ✅ 高，支持多种方式 | ⚠️ 中，依赖脚本 |
| **直接运行** | ✅ 支持，可直接运行程序 | ❌ 不支持，必须通过脚本 |
| **代码清晰度** | ✅ 高，逻辑明确 | ⚠️ 中，需要查看脚本 |

### 6.2 代码实现的优势

1. **统一管理**：所有登录状态逻辑在 `AuthManager` 中
2. **易于测试**：可以编写单元测试验证 `clearCredentials()`
3. **灵活性高**：支持命令行参数和环境变量两种方式
4. **直接运行**：可以直接运行客户端程序，不依赖脚本
5. **代码清晰**：逻辑在代码中，易于理解和维护

---

## 7. 测试验证

### 7.1 编译验证

```bash
cd /home/wqs/bigdata/Remote-Driving
docker compose exec client-dev bash -c "cd /tmp/client-build && make -j4"
```

**预期**：编译成功，无错误

### 7.2 功能验证

**测试 1：默认启动（保留登录状态）**

```bash
bash scripts/run-client-ui.sh
```

**预期**：
- 如果之前已登录，直接进入主界面或车辆选择界面
- 如果之前未登录，显示登录界面
- 控制台日志：`AuthManager: Skipping loadCredentials (reset mode)` 不应出现

**测试 2：清除登录状态启动**

```bash
bash scripts/run-client-ui.sh --reset-login
```

**预期**：
- 显示登录界面
- 控制台日志应包含：
  - `CLIENT_RESET_LOGIN environment variable set, will reset login state` 或
  - `Command line argument detected, will reset login state`
  - `AuthManager: Skipping loadCredentials (reset mode)`
  - `AuthManager: Clearing saved credentials`
  - `AuthManager: Credentials cleared`

**测试 3：直接运行客户端程序**

```bash
docker compose exec client-dev bash -c "cd /tmp/client-build && ./RemoteDrivingClient --reset-login"
```

**预期**：同测试 2

---

## 8. 相关文档

- `docs/CLIENT_LOGIN_STATE_MANAGEMENT.md`：登录状态管理详细文档
- `docs/CLIENT_LOGIN_STATE_QUICK_REF.md`：快速参考
- `client/src/authmanager.h`：AuthManager 头文件
- `client/src/authmanager.cpp`：AuthManager 实现
- `client/src/main.cpp`：主程序入口

---

## 9. 总结

✅ **登录状态清除功能已在代码层面实现**

**实现位置**：
- `AuthManager::clearCredentials()`：清除方法
- `AuthManager::AuthManager(QObject*, bool)`：构造函数支持不加载凭据
- `main.cpp`：检查环境变量和命令行参数

**使用方式**：
- 命令行参数：`--reset-login`, `--clear-login`, `-r`
- 环境变量：`CLIENT_RESET_LOGIN=1`

**优势**：
- 逻辑集中，易于维护
- 支持多种方式控制
- 可以直接运行程序，不依赖脚本
