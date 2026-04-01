# 错误码清单（日志与告警用）

各模块在日志中应使用统一错误码前缀，便于 `auto_diagnose.py` 与告警规则匹配。格式：`code=<CODE>` 或 `err=<CODE>: <message>`。

---

## 1. 通用

| 代码 | 含义 | 典型场景 |
|------|------|----------|
| E_CONFIG_MISSING | 必要配置缺失 | 环境变量/配置文件缺少必填项 |
| E_TIMEOUT | 操作超时 | 连接/请求超过设定时间 |
| E_NETWORK | 网络不可达 | 连接被拒绝、DNS 失败、超时 |

---

## 2. Backend

| 代码 | 含义 | 典型场景 |
|------|------|----------|
| E_DB_CONN_FAILED | 数据库连接失败 | DATABASE_URL 错误或 postgres 未就绪 |
| E_DB_QUERY | 查询执行失败 | 语法/权限/约束错误 |
| E_JWT_INVALID | JWT 校验失败 | 过期、签名错误、iss/aud 不匹配 |
| E_VIN_FORBIDDEN | 无 VIN 权限 | 用户未获该 VIN 的 view/control |
| E_SESSION_LOCKED | 会话锁冲突 | 该 VIN 已被他人控制 |
| E_ZLM_API | ZLM 内部 API 调用失败 | getServerConfig/open 等返回非 200 |

---

## 3. Client

| 代码 | 含义 | 典型场景 |
|------|------|----------|
| E_LOGIN_FAILED | 登录失败 | Keycloak 返回 401/400 |
| E_BACKEND_UNREACHABLE | Backend 不可达 | 连接超时、DNS 失败 |
| E_MQTT_CONN_FAILED | MQTT 连接失败 | Broker 地址错误或网络不通 |
| E_WEBRTC_FAILED | WebRTC 拉流失败 | WHEP 连接关闭、SDP 错误、TURN 不可达 |

---

## 4. Vehicle-side / carla-bridge

| 代码 | 含义 | 典型场景 |
|------|------|----------|
| E_MQTT_CONN_FAILED | MQTT 连接失败 | Broker 不可达、认证失败 |
| E_MQTT_DISCONNECTED | MQTT 连接断开 | 网络抖动、Broker 重启 |
| E_ZLM_PUSH_TIMEOUT | 推流首帧超时 | ZLM 不可达或 RTMP 端口错误 |
| E_ZLM_PUSH_FAILED | 推流失败 | 编码/网络错误 |
| E_CARLA_CONN_FAILED | CARLA 连接失败 | CARLA 未启动或端口错误 |
| E_CARLA_RPC_TIMEOUT | CARLA RPC 超时 | 仿真卡顿或网络问题 |
| E_WATCHDOG_SAFE_STOP | 看门狗触发安全停车 | 超时未收到心跳/控制指令 |
| E_SEC_REPLAY | 防重放拒绝 | seq 回退或时间窗超限 |
| E_SEC_SIGNATURE | 签名校验失败 | 控制消息 HMAC 不通过 |

---

## 5. 使用约定

- 日志中必须包含：`code=<CODE>` 或 `err=<CODE>`，以及简短原因（如 `err=E_MQTT_CONN_FAILED: connection refused`）。
- 新增错误码时在此文档与各模块日志处同步更新。
- `scripts/auto_diagnose.py` 可基于 `code=` / `err=` 做关键词匹配并给出排查建议（见 TROUBLESHOOTING_RUNBOOK.md）。
