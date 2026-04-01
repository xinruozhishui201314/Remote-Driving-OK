# M2 GATE B 验证记录：WebRTC WHEP/WHIP 绑定 ZLMediaKit（Option C）

**状态**: 待补充实测结果（步骤已给出，可在 dev 环境执行后更新为已验证）  
**日期**: 2026-02-06  

---

## 1. 验证目标

- 确认以下三点：
  1. `POST /api/v1/vins/{vin}/sessions` 返回的 JSON 中包含正确的 `media.whip` 与 `media.whep` 字段。  
  2. `GET /api/v1/sessions/{sessionId}/streams` 返回的 `webrtc.play` 与 `media.whep` 一致，且与 `sessions` 表中的 `vin`/`session_id` 一致。  
  3. 当使用不存在的 `sessionId` 调用 `/streams` 时，返回 `404 {"error":"session_not_found"}`。  

---

## 2. 环境前提

```bash
cd /home/wqs/bigdata/Remote-Driving

# Postgres / Keycloak / ZLMediaKit / Backend 均已启动（含 dev 覆盖）
docker compose -f docker-compose.yml -f docker-compose.dev.yml up -d
```

---

## 3. 创建 Session 并查看 media.whip / media.whep

```bash
cd /home/wqs/bigdata/Remote-Driving

docker compose exec -T backend sh -c '
TOKEN=$(curl -s -X POST "http://keycloak:8080/realms/teleop/protocol/openid-connect/token" \
  -d "client_id=teleop-client" \
  -d "username=e2e-test" \
  -d "password=e2e-test-password" \
  -d "grant_type=password" | sed -n "s/.*\"access_token\":\"\([^\"]*\)\".*/\1/p")

echo "TOKEN_LEN=${#TOKEN}"

SESSION_RESP=$(curl -s -w "\nHTTP_CODE:%{http_code}\n" \
  -X POST -H "Authorization: Bearer $TOKEN" \
  http://localhost:8080/api/v1/vins/E2ETESTVIN0000001/sessions)

echo "$SESSION_RESP"
SESSION_ID=$(echo "$SESSION_RESP" | sed -n "s/.*\"sessionId\":\"\([^\"]*\)\".*/\1/p")
echo "SESSION_ID=$SESSION_ID"
'
```

**预期**：

- `HTTP_CODE:201`。  
- 响应 JSON 类似：

```json
{
  "sessionId": "451c1aa9-c5cc-4df5-907d-b7b12b77c848",
  "media": {
    "whip": "whip://zlmediakit:80/index/api/webrtc?app=teleop&stream=E2ETESTVIN0000001-451c1aa9-c5cc-4df5-907d-b7b12b77c848&type=push",
    "whep": "whep://zlmediakit:80/index/api/webrtc?app=teleop&stream=E2ETESTVIN0000001-451c1aa9-c5cc-4df5-907d-b7b12b77c848&type=play"
  }
}
```

---

## 4. 验证 /streams 返回的 WHEP URL 与 DB 一致

```bash
cd /home/wqs/bigdata/Remote-Driving

docker compose exec -T backend sh -c "
TOKEN=$(curl -s -X POST \"http://keycloak:8080/realms/teleop/protocol/openid-connect/token\" \
  -d \"client_id=teleop-client\" \
  -d \"username=e2e-test\" \
  -d \"password=e2e-test-password\" \
  -d \"grant_type=password\" | sed -n \"s/.*\\\"access_token\\\":\\\"\\([^\\\"]*\\)\\\".*/\\1/p\")

SESSION_ID='<从上一步输出中复制>'

STREAMS_RESP=\$(curl -s -w \"\\nHTTP_CODE:%{http_code}\\n\" \
  -H \"Authorization: Bearer \$TOKEN\" \
  \"http://localhost:8080/api/v1/sessions/\$SESSION_ID/streams\")

echo \"STREAMS_RESP=\$STREAMS_RESP\"
"
```

**预期**：

- `HTTP_CODE:200`。  
- `STREAMS_RESP` 中 `webrtc.play` 字段应与 `media.whep` 一致，即：

```text
whep://<host>:<port>/index/api/webrtc?app=teleop&stream=E2ETESTVIN0000001-<SESSION_ID>&type=play
```

### 4.2 DB 一致性检查

```bash
docker compose exec postgres psql -U teleop_user -d teleop_db -c "
SELECT vin::text, session_id::text
FROM sessions
WHERE session_id = '<SESSION_ID>';
"
```

**预期**：

- 返回一行，`vin='E2ETESTVIN0000001'`，`session_id` 等于上一步 `SESSION_ID`。  
- `whep`/`whip` URL 中 `stream` 的 `<vin>-<sessionId>` 与此查询结果一致。  

---

## 5. 错误路径验证：不存在的 sessionId

```bash
BAD_SESSION_ID="00000000-0000-0000-0000-000000000000"

docker compose exec -T backend sh -c "
TOKEN=$(curl -s -X POST \"http://keycloak:8080/realms/teleop/protocol/openid-connect/token\" \
  -d \"client_id=teleop-client\" \
  -d \"username=e2e-test\" \
  -d \"password=e2e-test-password\" \
  -d \"grant_type=password\" | sed -n \"s/.*\\\"access_token\\\":\\\"\\([^\\\"]*\\)\\\".*/\\1/p\")

curl -s -w \"\\nHTTP_CODE:%{http_code}\\n\" \
  -H \"Authorization: Bearer \$TOKEN\" \
  \"http://localhost:8080/api/v1/sessions/\$BAD_SESSION_ID/streams\"
"
```

**预期**：

- `HTTP_CODE:404`。  
- body 为 `{"error":"session_not_found"}`。  

---

## 6. 回归测试

- 运行：  

```bash
cd /home/wqs/bigdata/Remote-Driving
bash scripts/e2e.sh
bash scripts/verify-vins-e2e.sh
```

- 预期所有既有接口（`/health`、`/ready`、`/me`、`/vins`、`POST /sessions`、`GET /streams`、`POST /lock`）行为不变，测试全部通过。  

---

## 7. 结论（待你执行后补充）

- [ ] 已根据上述步骤在 dev 环境完成验证。  
- [ ] 确认：session 创建返回的 `media.whip` / `media.whep` 与 `/streams` 返回的 URL 及 DB 中的 `sessions` 一致。  

