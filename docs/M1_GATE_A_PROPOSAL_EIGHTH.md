# M1 GATE A 变更提案（第八批）：POST /api/v1/sessions/{sessionId}/lock 占位

**状态**: 已确认并实施  
**日期**: 2026-02-06

---

## 0. Executive Summary

- **目标**：新增 **POST /api/v1/sessions/{sessionId}/lock**，需 JWT，占位实现：校验 JWT 后返回 **200** + `{"locked":true,"lockId":"<uuid>"}`。
- **收益**：为控制锁机制占位，与 M1 架构中的 Session Lock API 对齐。
- **非目标**：本批不验证 sessionId 是否真实存在、不查 sessions 表、不实现真正的锁机制；仅 JWT 校验 + 返回占位 lockId。

---

## 1. 目标与非目标

| 目标 | 非目标 |
|------|--------|
| POST /api/v1/sessions/{sessionId}/lock：JWT 校验，返回占位 lockId | 验证 sessionId 存在性、查 sessions 表、实现真正的锁机制 |
| 返回 JSON：`{"locked":true,"lockId":"<uuid>"}` | 锁竞争、锁超时、锁释放、多用户锁管理 |
| 无/无效 JWT → 401 | 与车端、控制指令链路集成 |

---

## 2. 实现要点

- **路径**：`POST /api/v1/sessions/{sessionId}/lock`，`{sessionId}` 为路径参数（UUID 格式）。
- **鉴权**：与 /me、/vins、POST /sessions、GET /streams 相同 JWT 校验。
- **响应**：成功时返回 200，body 含 `{"locked":true,"lockId":"<uuid>"}`（lockId 为生成的 UUID v4）。
- **错误**：无/无效 JWT → 401；DB 错误（若后续查表）→ 503。

---

## 3. 测试

- 无 JWT → 401。
- 有效 JWT → 200，body 含 `{"locked":true,"lockId":"..."}`，lockId 为 UUID v4 格式。

---

## 4. 变更清单（预估）

| 路径 | 变更 |
|------|------|
| `backend/src/main.cpp` | 新增 POST /api/v1/sessions/{sessionId}/lock：JWT → 解析 sessionId → 生成 lockId 返回 200 |

---

## 5. 运行与验证

```bash
# 使用 e2e-test token
TOKEN=$(docker compose exec -T backend curl -s -X POST "http://keycloak:8080/realms/teleop/protocol/openid-connect/token" \
  -H "Content-Type: application/x-www-form-urlencoded" \
  -d "grant_type=password&client_id=teleop-client&username=e2e-test&password=e2e-test-password" | sed -n 's/.*"access_token":"\([^"]*\)".*/\1/p')
SESSION_ID="7fa4b787-c082-4d24-bd4e-3345f615d8a0"  # 从 POST /sessions 获取
curl -s -w "\n%{http_code}\n" -X POST -H "Authorization: Bearer $TOKEN" "http://localhost:8081/api/v1/sessions/$SESSION_ID/lock"
# 预期 200，body 含 {"locked":true,"lockId":"..."}
```

---

**请确认**：若同意按本提案实施，请回复 **CONFIRM**。
