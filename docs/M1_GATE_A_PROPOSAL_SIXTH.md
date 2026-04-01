# M1 GATE A 变更提案（第六批）：POST /api/v1/vins/{vin}/sessions 占位

**状态**: 已确认并实施（见 M1_GATE_B_VERIFICATION_SIXTH.md）  
**日期**: 2026-02-06

---

## 0. Executive Summary

- **目标**：新增 **POST /api/v1/vins/{vin}/sessions**，需 JWT，占位实现：校验用户对该 VIN 有权限（在 GET /vins 列表中）后返回 **201** + `{"sessionId":"<uuid>"}`；无权限或 VIN 不在列表返回 **403**。
- **收益**：为会话链路占位，与 M1 架构中的 Session API 对齐。
- **非目标**：本批不落库 sessions 表、不实现状态机与锁；仅权限校验 + 生成临时 sessionId 返回。

---

## 1. 目标与非目标

| 目标 | 非目标 |
|------|--------|
| POST /api/v1/vins/{vin}/sessions：JWT 校验，校验 vin 在用户可访问列表中 | 写入 sessions 表、状态机、lock/unlock、streams |
| 有权限 → 201 + sessionId（UUID 字符串） | 与 ZLM、车端联动 |
| 无权限 / VIN 不在列表 → 403 | 心跳、结束会话 |

---

## 2. 实现要点

- **路径**：`POST /api/v1/vins/{vin}/sessions`，`{vin}` 为路径参数（17 位 VIN）。
- **鉴权**：与 /me、/vins 相同 JWT 校验；再从 DB 取该用户可访问 VIN 列表（复用 get_vins_for_sub），若 `vin` 不在列表中则 403。
- **响应**：有权限时生成一个 UUID（如 `uuid_generate_v4()` 或 C++ 随机 UUID），返回 201，body `{"sessionId":"<uuid>"}`。
- **错误**：无/无效 JWT → 401；VIN 不在用户列表 → 403；DB 错误 → 503。

---

## 3. 测试

- 无 JWT → 401。
- 有效 JWT、VIN 不在列表 → 403。
- 有效 JWT、VIN 在列表（如 E2ETESTVIN0000001）→ 201，body 含 sessionId。

---

## 4. 变更清单（预估）

| 路径 | 变更 |
|------|------|
| `backend/src/main.cpp` | 新增 POST /api/v1/vins/{vin}/sessions：JWT → 取 vins → 若 vin 在列表中则生成 UUID 返回 201，否则 403 |

---

## 5. 运行与验证

```bash
# 使用 e2e-test token
TOKEN=$(docker compose exec -T backend curl -s -X POST "http://keycloak:8080/realms/teleop/protocol/openid-connect/token" \
  -H "Content-Type: application/x-www-form-urlencoded" \
  -d "grant_type=password&client_id=teleop-client&username=e2e-test&password=e2e-test-password" | sed -n 's/.*"access_token":"\([^"]*\)".*/\1/p')
curl -s -w "\n%{http_code}\n" -X POST -H "Authorization: Bearer $TOKEN" http://localhost:8081/api/v1/vins/E2ETESTVIN0000001/sessions
# 预期 201，body 含 sessionId
curl -s -w "\n%{http_code}\n" -X POST -H "Authorization: Bearer $TOKEN" http://localhost:8081/api/v1/vins/NOTINLIST00000001/sessions
# 预期 403
```

---

**请确认**：若同意按本提案实施，请回复 **CONFIRM**。
