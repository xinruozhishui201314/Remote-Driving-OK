# M1 GATE B 验证（第七批）：GET /api/v1/sessions/{sessionId}/streams 占位

**对应提案**: [M1_GATE_A_PROPOSAL_SEVENTH.md](./M1_GATE_A_PROPOSAL_SEVENTH.md)

---

## 1. 验证目标

- **GET /api/v1/sessions/{sessionId}/streams**：无/无效 JWT → 401；有效 JWT → 200 + 占位流地址 JSON（`{"webrtc":{"play":"whep://..."}}`）。
- 流地址基于 ZLM_API_URL 解析，格式为 WHEP URL。

---

## 2. 前置条件

- Backend 已构建并启动（开发模式或生产模式）；Keycloak 含 e2e-test 用户。

---

## 3. 验证步骤

### 3.1 无 JWT → 401

```bash
curl -s -w "\n%{http_code}\n" http://localhost:8081/api/v1/sessions/7fa4b787-c082-4d24-bd4e-3345f615d8a0/streams
# 预期 401
```

### 3.2 有效 JWT → 200 + 流地址

```bash
# 获取 token
TOKEN=$(docker compose exec -T backend curl -s -X POST "http://keycloak:8080/realms/teleop/protocol/openid-connect/token" \
  -H "Content-Type: application/x-www-form-urlencoded" \
  -d "grant_type=password&client_id=teleop-client&username=e2e-test&password=e2e-test-password" | sed -n 's/.*"access_token":"\([^"]*\)".*/\1/p')

# 先创建 session（获取 sessionId）
SESSION_RESP=$(docker compose exec -T -e BEARER_TOKEN="$TOKEN" backend sh -c 'curl -s -X POST -H "Authorization: Bearer $BEARER_TOKEN" http://localhost:8080/api/v1/vins/E2ETESTVIN0000001/sessions')
SESSION_ID=$(echo "$SESSION_RESP" | sed -n 's/.*"sessionId":"\([^"]*\)".*/\1/p')
echo "SessionId: $SESSION_ID"

# 获取流地址
curl -s -w "\n%{http_code}\n" -H "Authorization: Bearer $TOKEN" "http://localhost:8081/api/v1/sessions/$SESSION_ID/streams"
# 预期 200，body 含 {"webrtc":{"play":"whep://zlmediakit:80/index/api/webrtc?app=live&stream=session-..."}}
```

### 3.3 验证流地址格式

```bash
RESP=$(curl -s -H "Authorization: Bearer $TOKEN" "http://localhost:8081/api/v1/sessions/$SESSION_ID/streams")
echo "$RESP" | grep -q "whep://" && echo "PASS: 包含 WHEP URL" || echo "FAIL: 未找到 WHEP URL"
echo "$RESP" | grep -q "session-$SESSION_ID" && echo "PASS: URL 包含 sessionId" || echo "FAIL: URL 未包含 sessionId"
```

---

## 4. 变更清单（已实施）

| 路径 | 变更 |
|------|------|
| `backend/src/main.cpp` | 新增 `build_stream_url()`；GET /api/v1/sessions/{sessionId}/streams：JWT → 解析 sessionId → 返回占位流地址 JSON |

---

## 5. 运行说明

- **生产模式**：`docker compose build backend && docker compose up -d backend`
- **开发模式**（推荐）：
  - 首次：`./scripts/dev-backend.sh build`（构建开发镜像）
  - 之后：修改代码 → `./scripts/dev-backend.sh restart`（重启容器，自动重新编译）
- **验证**：使用上述 curl 命令测试。
