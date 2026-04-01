# M1 GATE B 验证：JWT 校验与 GET /api/v1/me（第二批）

**对应提案**: [M1_GATE_A_PROPOSAL_SECOND.md](./M1_GATE_A_PROPOSAL_SECOND.md)  
**实施日期**: 2026-02-06

---

## 1. 变更摘要与文件清单

| 文件 | 变更类型 | 说明 |
|------|----------|------|
| `backend/CMakeLists.txt` | 修改 | 增加 nlohmann_json (FetchContent)，编译 jwt_validator.cpp |
| `backend/src/main.cpp` | 修改 | 注册 GET /api/v1/me；从 env 读 KEYCLOAK_*；Bearer 校验；返回 sub/preferred_username/roles |
| `backend/src/auth/jwt_validator.h` | **新增** | JWT 校验接口：validate_jwt_claims(token, expected_iss, expected_aud) |
| `backend/src/auth/jwt_validator.cpp` | **新增** | base64url 解码 payload，校验 iss/aud/exp（不验签，开发模式） |

---

## 2. 测试与覆盖

- **单元测试**：未新增（可选）。
- **集成**：无 token → GET /api/v1/me → 401；有效 Keycloak token → 200 且 body 含 sub、roles。
- **Docker**：`docker compose build backend` 通过；镜像仍为 1ms.run 基础镜像。

**Definition of Done**：GET /api/v1/me 无 token 或非法 token 返回 401；有效 Bearer token 返回 200 且 JSON 含 sub、preferred_username、roles；/health、/ready 仍无需 token。

---

## 3. 运行命令与验证

```bash
# 构建（Docker 内）
docker compose build backend
docker compose up -d postgres keycloak backend

# 无 token → 401
curl -s -o /dev/null -w "%{http_code}" http://localhost:8081/api/v1/me
# 期望: 401

# 获取 Keycloak token（需在 teleop realm 下创建用户，或使用 admin-cli）
TOKEN=$(curl -s -X POST "http://localhost:8080/realms/teleop/protocol/openid-connect/token" \
  -H "Content-Type: application/x-www-form-urlencoded" \
  -d "username=<user>" -d "password=<pass>" \
  -d "grant_type=password" -d "client_id=teleop-client" \
  -d "client_secret=<secret>" | jq -r .access_token)

# 有效 token → 200 + JSON
curl -s -H "Authorization: Bearer $TOKEN" http://localhost:8081/api/v1/me
# 期望: {"sub":"...","preferred_username":"...","roles":[...]}
```

---

## 4. 安全清单

- [x] /api/v1/me 需 Bearer token，否则 401。
- [x] 校验 iss、exp、aud（不验签，文档已标注开发模式）。
- [ ] 生产环境应增加 JWKS 验签（后续批次）。

---

## 5. 结论

M1 第二批已按 GATE A 实现：JWT 校验（iss/aud/exp）+ GET /api/v1/me；本地与 Docker 构建通过。请在 Keycloak 就绪后使用上述 curl 验证 401 与 200 行为。
