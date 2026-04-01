# M1 GATE B 验证记录（第八批）：POST /api/v1/sessions/{sessionId}/lock 占位

**状态**: 已验证通过  
**日期**: 2026-02-06  

---

## 1. 验证目标

- 确认新增接口 **POST /api/v1/sessions/{sessionId}/lock** 已按 GATE A 提案实现：
  - 需 Bearer JWT。
  - 无/无效 JWT → `401`。
  - 有效 JWT → `200`，返回 `{"locked":true,"lockId":"<uuid>"}`，其中 `lockId` 为 UUID v4 格式占位。
  - 当前批次不校验 `sessionId` 是否存在，不访问 DB。

---

## 2. 环境准备

```bash
cd /home/wqs/bigdata/Remote-Driving

# 确保基础服务已启动（postgres / keycloak / backend 等）
docker compose -f docker-compose.yml -f docker-compose.dev.yml up -d

# 确认 backend 就绪
curl -s http://localhost:8081/health
curl -s http://localhost:8081/ready
```

预期：
- `/health` → `{"status":"ok"}`。
- `/ready` → `{"ready":true}` 且 HTTP 200。

---

## 3. 测试步骤与结果

### 3.1 准备 JWT 与 sessionId

使用已有的 e2e 测试用户 `e2e-test`，在 **backend 容器内部** 获取 token，并通过已实现的
`POST /api/v1/vins/{vin}/sessions` 获取一个合法的 `sessionId`：

```bash
cd /home/wqs/bigdata/Remote-Driving

docker compose exec -T backend sh -c '
TOKEN=$(curl -s -X POST "http://keycloak:8080/realms/teleop/protocol/openid-connect/token" \
  -d "client_id=teleop-client" \
  -d "username=e2e-test" \
  -d "password=e2e-test-password" \
  -d "grant_type=password" | sed -n "s/.*\"access_token\":\"\([^\"]*\)\".*/\1/p");

SESSION_ID=$(curl -s -X POST \
  -H "Authorization: Bearer $TOKEN" \
  http://localhost:8080/api/v1/vins/E2ETESTVIN0000001/sessions \
  | sed -n "s/.*\"sessionId\":\"\([^\"]*\)\".*/\1/p");

echo "SESSION_ID=$SESSION_ID"
'
```

预期：
- 输出中包含 `SESSION_ID=<uuid>`，UUID v4 格式。

### 3.2 无 JWT 调用 /lock

```bash
curl -s -w "\nHTTP_CODE:%{http_code}\n" \
  -X POST "http://localhost:8081/api/v1/sessions/00000000-0000-0000-0000-000000000000/lock"
```

预期：
- HTTP 401。
- body 为 `{"error":"unauthorized"}`。

### 3.3 使用有效 JWT 调用 /lock

```bash
cd /home/wqs/bigdata/Remote-Driving

docker compose exec -T backend sh -c '
TOKEN=$(curl -s -X POST "http://keycloak:8080/realms/teleop/protocol/openid-connect/token" \
  -d "client_id=teleop-client" \
  -d "username=e2e-test" \
  -d "password=e2e-test-password" \
  -d "grant_type=password" | sed -n "s/.*\"access_token\":\"\([^\"]*\)\".*/\1/p");

SESSION_ID=$(curl -s -X POST \
  -H "Authorization: Bearer $TOKEN" \
  http://localhost:8080/api/v1/vins/E2ETESTVIN0000001/sessions \
  | sed -n "s/.*\"sessionId\":\"\([^\"]*\)\".*/\1/p");

echo "Session ID: $SESSION_ID";

curl -s -w "\nHTTP_CODE:%{http_code}\n" \
  -X POST \
  -H "Authorization: Bearer $TOKEN" \
  "http://localhost:8081/api/v1/sessions/$SESSION_ID/lock"
'
```

预期：
- HTTP 200。
- body 形如：

```json
{"locked":true,"lockId":"451c1aa9-c5cc-4df5-907d-b7b12b77c848"}
```

其中：
- `locked` 为 `true`。
- `lockId` 为 UUID v4 格式（8-4-4-4-12 十六进制字符）。

---

## 4. 结论

- 接口 `POST /api/v1/sessions/{sessionId}/lock` 已按 GATE A 提案实现并通过手工验证：
  - JWT 校验与现有接口一致；
  - 无/无效 JWT 返回 401；
  - 有效 JWT 返回 200，包含占位字段 `locked:true` 与 `lockId(UUID v4)`；
  - 当前批次不依赖数据库，不校验 sessionId 存在性。
- 符合 M1 第八批“占位锁接口”的范围定义，可以作为后续真实锁模型设计与实现的基础。

