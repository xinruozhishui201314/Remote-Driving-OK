# M1 GATE A 变更提案（第七批）：GET /api/v1/sessions/{sessionId}/streams 占位

**状态**: 已确认并实施（见 M1_GATE_B_VERIFICATION_SEVENTH.md）  
**日期**: 2026-02-06

---

## 0. Executive Summary

- **目标**：新增 **GET /api/v1/sessions/{sessionId}/streams**，需 JWT，占位实现：校验 JWT 后返回 **200** + 占位流地址 JSON（WebRTC WHIP/WHEP URL 格式，基于 ZLMediaKit）。
- **收益**：为流媒体链路占位，与 M1 架构中的 Session Streams API 对齐。
- **非目标**：本批不验证 sessionId 是否真实存在、不查 sessions 表、不生成真实流地址；仅 JWT 校验 + 返回占位 URL。

---

## 1. 目标与非目标

| 目标 | 非目标 |
|------|--------|
| GET /api/v1/sessions/{sessionId}/streams：JWT 校验，返回占位流地址 | 验证 sessionId 存在性、查 sessions 表、生成真实流 |
| 返回 JSON：`{"webrtc":{"play":"whep://..."}}` 或类似格式 | 与 ZLMediaKit 真实交互、推流鉴权 |
| 无/无效 JWT → 401 | 流状态查询、录制控制 |

---

## 2. 实现要点

- **路径**：`GET /api/v1/sessions/{sessionId}/streams`，`{sessionId}` 为路径参数（UUID 格式）。
- **鉴权**：与 /me、/vins、POST /sessions 相同 JWT 校验。
- **响应**：成功时返回 200，body 含占位流地址（如 `{"webrtc":{"play":"whep://zlmediakit:80/index/api/webrtc?app=live&stream=test&type=play"}}` 或 `{"streams":[{"type":"webrtc","url":"..."}]}`）。
- **错误**：无/无效 JWT → 401；DB 错误（若后续查表）→ 503。

---

## 3. 流地址格式（占位）

基于 ZLMediaKit WebRTC API（WHEP 播放）：
- 格式：`whep://<host>:<port>/index/api/webrtc?app=<app>&stream=<stream>&type=play`
- 占位值：`app=live`，`stream=session-{sessionId}`（或固定 `test`），`host:port` 从环境变量 `ZLM_API_URL` 解析（如 `http://zlmediakit/index/api` → `zlmediakit:80`）。

---

## 4. 测试

- 无 JWT → 401。
- 有效 JWT → 200，body 含 `webrtc` 或 `streams` 字段，URL 为占位格式。

---

## 5. 变更清单（预估）

| 路径 | 变更 |
|------|------|
| `backend/src/main.cpp` | 新增 GET /api/v1/sessions/{sessionId}/streams：JWT → 解析 sessionId → 返回占位流地址 JSON |

---

## 6. 运行与验证

```bash
# 使用 e2e-test token
TOKEN=$(docker compose exec -T backend curl -s -X POST "http://keycloak:8080/realms/teleop/protocol/openid-connect/token" \
  -H "Content-Type: application/x-www-form-urlencoded" \
  -d "grant_type=password&client_id=teleop-client&username=e2e-test&password=e2e-test-password" | sed -n 's/.*"access_token":"\([^"]*\)".*/\1/p')
SESSION_ID="7fa4b787-c082-4d24-bd4e-3345f615d8a0"  # 从 POST /sessions 获取
curl -s -w "\n%{http_code}\n" -H "Authorization: Bearer $TOKEN" http://localhost:8081/api/v1/sessions/$SESSION_ID/streams
# 预期 200，body 含流地址
```

---

**请确认**：若同意按本提案实施，请回复 **CONFIRM**。
