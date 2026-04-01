# 新增功能检查清单：自动化测试 + 日志

**原则**：每次增加功能后都要配套**功能自动化测试**和**日志记录**，避免新功能带病上线；即使出问题也能通过日志**精准定位**。

---

## 1. 强制要求（必做）

| 项 | 说明 |
|----|------|
| **功能自动化测试** | 新增或扩展 `scripts/verify-*.sh` 或 e2e 场景，覆盖新功能的**主路径**（成功）和**关键失败分支**（如 401、404、参数错误）。修改后运行 `./scripts/build-and-verify.sh` 或对应 verify 脚本，必须通过。 |
| **日志记录** | 在新功能**入口、成功/失败分支、关键参数**处打日志，使用**统一前缀**（见下），便于 grep 和精准定位。 |

---

## 2. 日志规范（便于精准定位）

### 2.1 统一前缀

| 模块 | 前缀 | 示例 |
|------|------|------|
| Backend | `[Backend]` | `[Backend][GET /api/v1/vins] sub=... vins=...`、`[Backend][POST /api/v1/vehicles] 201 已添加车辆 vin=...`、`[Backend][未匹配] method=... path=...` |
| Client 认证 | `[Client][Auth]` | `[Client][Auth] 发起登录`、`[Client][Auth] onLoginReply: 登录成功 username=...` |
| Client 车辆列表 | `[Client][车辆列表]` | `[Client][车辆列表] 请求 GET .../api/v1/vins`、`[Client][车辆列表] 已加载 count= N` |
| Client 主流程 | `[Client][Main]` | `[Client][Main] loginSucceeded: isTestToken= false ...` |
| Client UI/QML | `[Client][UI]` | `[Client][UI] 打开车辆选择对话框`、`[Client][UI] onLoginFailed error=...` |
| Client 节点检测 | `[Client][节点检测]` | `[Client][节点检测] refresh serverUrl= ...`、`Backend/Keycloak/ZLM 正常|不可达`、`全部完成` |
| Client 账户名历史 | `[Client][Auth]` | `loadCredentials: ... usernameHistory.count=`、`保存账户名历史 count= N 新增= xxx`、`addUsernameToHistory: 跳过空账户名` |
| Client 登录页 UI | `[Client][UI]` | `登录页 已填充上次账户名`、`选择历史账户名 xxx`、`密码框 可见=true/false` |
| 车端/控制 | `[Control]`、`[Vehicle]` 等 | 按现有项目约定 |

### 2.2 必须记录的时机

- **入口**：请求/事件进入该功能时（method、path、关键参数或 vin/sessionId）。
- **成功**：返回 200/201/204 时，带关键结果（如 vin、account_id、size）。
- **失败**：4xx/5xx 或业务失败时，带**原因**（如「本账号未绑定该 VIN」、HTTP 错误、解析错误）。
- **外部调用**：发起 HTTP/DB/MQTT 前后可打简短日志（URL、状态码、bodySize），避免打满屏。

### 2.3 出问题后如何用日志定位

1. 确定**最后一条**带上述前缀的日志（时间顺序或 grep）。
2. 根据该条判断**断点**：若为「登录成功 → emit loginSucceeded」之后无「车辆列表 请求」，则问题在 Main 或车辆列表请求前；若为「HTTP 错误 statusCode=401」，则问题在鉴权或 token。
3. 参考 `docs/ADD_VEHICLE_GUIDE.md` §10.2 的「断点表」或各 verify 脚本输出中的「依据日志」说明。

---

## 3. 功能自动化测试（如何加）

### 3.1 新增独立验证脚本（推荐）

- 在 `scripts/` 下新增 `verify-<功能名>.sh`，用 curl/exec 模拟**主路径**和 1～2 个**关键失败**场景。
- 脚本内：**打印预期日志关键字**，便于与「依据日志定位」一致。
- 可选：在 `scripts/build-and-verify.sh` 中增加一步调用该脚本（见下方 3.3）。

### 3.2 扩展现有 verify 脚本

- 若新功能属于「登录」「管理页」「车辆增删」等已有链路，在 `verify-client-login.sh`、`verify-admin-add-vehicle-page.sh`、`verify-add-vehicle-e2e.sh` 中**增加检查点**或**新场景**（如新 API、新参数）。
- 保证 `./scripts/build-and-verify.sh` 仍能覆盖新逻辑。

### 3.3 接入 build-and-verify.sh

- 若希望**每次修改代码后**都跑新功能的验证，在 `scripts/build-and-verify.sh` 中增加一节，例如：
  - `log_section "6/6 验证 xxx 功能 (verify-xxx.sh)"`
  - `./scripts/verify-xxx.sh`，失败则 `FAILED=1`。
- 保持脚本可单独执行：`./scripts/verify-xxx.sh`。

---

## 4. 交付清单（每次加功能必勾）

- [ ] **功能实现**：代码完整，无 TODO/占位。
- [ ] **日志**：入口、成功/失败、关键参数已打，且使用约定前缀（如 `[Backend]`、`[Client][Auth]`）。
- [ ] **功能自动化测试**：新增或扩展了 `verify-*.sh` 或 e2e，覆盖主路径（及关键失败）。
- [ ] **验证通过**：执行 `./scripts/build-and-verify.sh` 通过（或对应 verify 脚本通过）。

---

## 5. 参考

- **验证脚本入口**：`./scripts/build-and-verify.sh`（编译 + 登录链路 + 管理页 + 增加车辆 E2E）。
- **日志与断点表**：`docs/ADD_VEHICLE_GUIDE.md` §9.2、§10.2。
- **规则**：`.cursorrules` §4 RULE V8/V9、新增功能交付清单。
