# M1 GATE A 变更提案（第五批）：迁移入 Postgres + GET /api/v1/vins 从 DB 返回列表

**状态**: 已确认并实施（见 M1_GATE_B_VERIFICATION_FIFTH.md）  
**日期**: 2026-02-06

---

## 0. Executive Summary

- **目标**：(1) Postgres 首次启动时在 **teleop_db** 执行 `001_initial_schema.sql`，保证表存在；(2) **GET /api/v1/vins** 根据 JWT 的 `sub` 查 **users** → **account_vehicles** + **vin_grants**，返回该用户可访问的 VIN 列表。
- **收益**：/vins 与真实数据模型一致，为后续会话、授权、审计打基础。
- **非目标**：本批不实现用户/账号/车辆的创建接口，不实现会话 API；无数据时返回空列表。

---

## 1. 目标与非目标

| 目标 | 非目标 |
|------|--------|
| teleop_db 启动时执行 001_initial_schema（仅首次 init） | 用户/账号/车辆 CRUD、会话 API |
| GET /api/v1/vins：JWT sub → users → VINs（account_vehicles + vin_grants） | 迁移版本表、多版本迁移 |
| 返回 JSON `{"vins":["VIN1","VIN2",...]}` | 分页、过滤 |

---

## 2. 数据与接口约定

- **用户解析**：JWT payload 的 `sub` 即 Keycloak Subject，对应 **users.keycloak_sub**。若不存在对应用户，返回 `{"vins":[]}`（不 403）。
- **VIN 来源**（去重）：
  - 账号车辆：该用户所属 **account_id** 下的 **account_vehicles**，且 **status = 'active'**；
  - 个人授权：**vin_grants** 中 **grantee_user_id = 当前用户 id**，且 **expires_at IS NULL OR expires_at > now()**。
- **顺序**：返回顺序不保证，客户端按需排序。

---

## 3. 实现要点

### 3.1 迁移在 Postgres 初始化时执行

- **方式**：在 `deploy/postgres` 增加 init 脚本（如 `02_run_teleop_schema.sh`），在 `docker-entrypoint-initdb.d` 中于 `init.sql` 之后执行；脚本内对 **teleop_db** 执行 `backend/migrations/001_initial_schema.sql`。
- **Compose**：postgres 服务增加卷挂载 `./backend/migrations:/migrations:ro`，并将 `02_run_teleop_schema.sh` 挂到 `docker-entrypoint-initdb.d/`。注意：仅在新库首次初始化时执行，已有数据卷不会重跑。

### 3.2 Backend GET /api/v1/vins

- **依赖**：已有 libpq、DATABASE_URL、JWT 校验。
- **步骤**：校验 JWT → 取 `sub` → 用 DATABASE_URL 连接 →  
  - 查 `users` 表 `WHERE keycloak_sub = $1` 得 `user_id`, `account_id`；  
  - 若无用户，返回 200 + `{"vins":[]}`；  
  - 若有：  
    - `SELECT vin FROM account_vehicles WHERE account_id = $1 AND status = 'active'`  
    - `SELECT vin FROM vin_grants WHERE grantee_user_id = $2 AND (expires_at IS NULL OR expires_at > now())`  
  - 合并去重后返回 `{"vins":[...]}`。
- **错误**：DB 错误可返回 503 或 500，并 `{"error":"internal"}`（与现有风格一致即可）。

---

## 4. 测试

- 无 JWT 或无效 JWT：401。
- 有效 JWT、DB 中无对应用户：200 + `{"vins":[]}`。
- 有效 JWT、用户存在但无 account_vehicles 与 vin_grants：200 + `{"vins":[]}`。
- 插入测试用户 + account + vehicle + account_vehicles（或 vin_grants）后，同用户 JWT 请求：200 + `{"vins":["..."]}`。

---

## 5. 变更清单（预估）

| 路径 | 变更 |
|------|------|
| `deploy/postgres/02_run_teleop_schema.sh` | 新增：`psql -v ON_ERROR_STOP=1 -f /migrations/001_initial_schema.sql`（在 teleop_db 上下文） |
| `docker-compose.yml` | postgres 增加 volumes：`./backend/migrations:/migrations:ro`，`./deploy/postgres/02_run_teleop_schema.sh:/docker-entrypoint-initdb.d/02_run_teleop_schema.sh:ro` |
| `backend/src/main.cpp` | GET /api/v1/vins：解析 sub → 查 users → 查 account_vehicles + vin_grants，拼 JSON 返回 |

---

## 6. 运行与验证

- **新起库**：`docker compose down -v` 后 `docker compose up -d postgres`，检查 teleop_db 中是否存在 accounts、users、vehicles、account_vehicles、vin_grants 等表。
- **Backend**：`docker compose up -d backend`，带有效 JWT 请求 GET /api/v1/vins，预期 200 与 `{"vins":[]}` 或含 VIN 的列表。

---

**请确认**：若同意按本提案实施，请回复 **CONFIRM**。
