# M1 GATE B 验证（第五批）：迁移入 Postgres + GET /api/v1/vins 从 DB 返回

**对应提案**: [M1_GATE_A_PROPOSAL_FIFTH.md](./M1_GATE_A_PROPOSAL_FIFTH.md)

---

## 1. 验证目标

- Postgres **首次**启动时在 **teleop_db** 执行 `001_initial_schema.sql`，存在 accounts、users、vehicles、account_vehicles、vin_grants、sessions 等表。
- **GET /api/v1/vins**：无/无效 JWT → 401；有效 JWT 且 DB 无对应用户或无授权 → 200 + `{"vins":[]}`；有效 JWT 且有授权 → 200 + `{"vins":["...", ...]}`；DB 异常 → 503。

---

## 2. 前置条件

- 需**新库**才能验证迁移：`docker compose down -v` 后再 `docker compose up -d postgres`（否则 init 不会重跑）。
- Backend 已构建并启动，Postgres 已 healthy。

---

## 3. 验证步骤

### 3.1 迁移已执行（新库）

```bash
cd /home/wqs/bigdata/Remote-Driving
docker compose down -v
docker compose up -d postgres
sleep 15
docker compose exec postgres psql -U teleop_user -d teleop_db -c "\dt"
```

**预期**：列出 accounts、users、vehicles、account_vehicles、vin_grants、sessions、session_participants、fault_events、audit_logs 等表。

### 3.2 GET /api/v1/vins 无 JWT → 401

```bash
curl -s -w "\n%{http_code}\n" http://localhost:8081/api/v1/vins
# 预期 401
```

### 3.3 GET /api/v1/vins 有效 JWT、无用户数据 → 200 + 空列表

使用 Keycloak 颁发的有效 Bearer token（且 DB 中无该 sub 对应用户）：

```bash
curl -s -w "\n%{http_code}\n" -H "Authorization: Bearer <TOKEN>" http://localhost:8081/api/v1/vins
# 预期 200，body {"vins":[]}
```

### 3.4 GET /api/v1/vins 有用户与授权 → 200 + 列表（自动化）

**种子数据**：Postgres 首次 init 时会执行 `03_seed_test_data.sql`，插入与 Keycloak 用户 `e2e-test`（id 即 JWT sub）对应的 account、user、vehicle、account_vehicles，VIN 为 `E2ETESTVIN0000001`。Keycloak realm 中已预置用户 `e2e-test` / 密码 `e2e-test-password`（见 `deploy/keycloak/realm-export.json`）。

**一键验证**（需先启动 postgres、keycloak、backend，且 Keycloak 已导入 realm）：

```bash
./scripts/verify-vins-e2e.sh
```

预期：取到 token → GET /api/v1/vins 返回 200，body 含 `"E2ETESTVIN0000001"`。

**手动示例**（若未用种子数据）：在 teleop_db 中插入 account、user（keycloak_sub 与 JWT sub 一致）、vehicle、account_vehicles 后，用该用户 JWT 请求 GET /api/v1/vins。

---

## 4. 变更清单（已实施）

| 路径 | 变更 |
|------|------|
| `deploy/postgres/02_run_teleop_schema.sh` | 新增：在 teleop_db 上执行 `/migrations/001_initial_schema.sql` |
| `docker-compose.yml` | postgres volumes：01_init.sql、`./backend/migrations:/migrations:ro`、02_run_teleop_schema.sh、03_seed_test_data.sql |
| `deploy/postgres/03_seed_test_data.sql` | 新增：E2E 种子数据（account、user keycloak_sub=a1b2c3d4-e5f6-7890-abcd-e2etestuser01、vehicle E2ETESTVIN0000001、account_vehicles） |
| `deploy/keycloak/realm-export.json` | 新增用户 e2e-test（id 同上，密码 e2e-test-password），供自动化取 token |
| `scripts/verify-vins-e2e.sh` | 新增：取 Keycloak token → GET /api/v1/vins → 校验返回含 E2ETESTVIN0000001 |
| `backend/src/main.cpp` | `get_vins_for_sub()`；GET /api/v1/vins 解析 JWT sub → 查 users → account_vehicles + vin_grants，返回 JSON；DB 失败 503 |

---

## 5. 运行说明

- **新库**：`docker compose down -v && docker compose up -d postgres`，再 `docker compose up -d backend`。
- **已有库**：若 postgres_data 已存在，02 脚本不会再次执行；要重跑迁移需先 `down -v` 再 up。
