# M1 GATE B 验证（第六批）：POST /api/v1/vins/{vin}/sessions 占位

**对应提案**: [M1_GATE_A_PROPOSAL_SIXTH.md](./M1_GATE_A_PROPOSAL_SIXTH.md)

---

## 1. 验证目标

- **POST /api/v1/vins/{vin}/sessions**：无/无效 JWT → 401；有效 JWT 但 VIN 不在用户可访问列表 → 403；有效 JWT 且 VIN 在列表 → 201 + `{"sessionId":"<uuid>"}`。
- sessionId 为 UUID v4 格式字符串（占位，未落库）。

---

## 2. 前置条件

- Backend 已构建并启动；Postgres 含种子数据（E2ETESTVIN0000001）；Keycloak 含 e2e-test 用户。

---

## 3. 验证步骤

### 3.1 无 JWT → 401

```bash
curl -s -w "\n%{http_code}\n" -X POST http://localhost:8081/api/v1/vins/E2ETESTVIN0000001/sessions
# 预期 401
```

### 3.2 有效 JWT、VIN 不在列表 → 403

使用 e2e-test token，请求一个不在列表中的 VIN：

```bash
TOKEN=$(docker compose exec -T backend curl -s -X POST "http://keycloak:8080/realms/teleop/protocol/openid-connect/token" \
  -H "Content-Type: application/x-www-form-urlencoded" \
  -d "grant_type=password&client_id=teleop-client&username=e2e-test&password=e2e-test-password" | sed -n 's/.*"access_token":"\([^"]*\)".*/\1/p')
curl -s -w "\n%{http_code}\n" -X POST -H "Authorization: Bearer $TOKEN" http://localhost:8081/api/v1/vins/NOTINLIST00000001/sessions
# 预期 403
```

### 3.3 有效 JWT、VIN 在列表 → 201 + sessionId

```bash
curl -s -w "\n%{http_code}\n" -X POST -H "Authorization: Bearer $TOKEN" http://localhost:8081/api/v1/vins/E2ETESTVIN0000001/sessions
# 预期 201，body 含 {"sessionId":"..."}，sessionId 为 UUID v4 格式
```

### 3.4 验证 sessionId 格式

```bash
RESP=$(curl -s -X POST -H "Authorization: Bearer $TOKEN" http://localhost:8081/api/v1/vins/E2ETESTVIN0000001/sessions)
SESSION_ID=$(echo "$RESP" | sed -n 's/.*"sessionId":"\([^"]*\)".*/\1/p')
echo "$SESSION_ID" | grep -qE '^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$' && echo "PASS: sessionId 格式正确" || echo "FAIL: sessionId 格式错误"
```

---

## 4. 变更清单（已实施）

| 路径 | 变更 |
|------|------|
| `backend/src/main.cpp` | 新增 `generate_uuid_v4()`；POST /api/v1/vins/{vin}/sessions：JWT → 取 vins → 校验 vin 在列表中 → 生成 UUID 返回 201，否则 403 |

---

## 5. 运行说明

- **构建**：`docker compose build backend`
- **运行**：`docker compose up -d backend`
- **验证**：使用上述 curl 命令或 `scripts/verify-vins-e2e.sh` 获取 token 后测试 POST。
