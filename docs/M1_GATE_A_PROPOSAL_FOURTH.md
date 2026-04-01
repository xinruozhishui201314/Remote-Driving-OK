# M1 GATE A 变更提案（第四批）：Backend 连接 PostgreSQL，/ready 做 DB 健康检查

**状态**: 已确认并实施（见 M1_GATE_B_VERIFICATION_FOURTH.md）  
**日期**: 2026-02-06

---

## 0. Executive Summary

- **目标**：后端使用 **DATABASE_URL** 连接 PostgreSQL，**GET /ready** 在 DB 连通时返回 200、不可达时返回 **503**（并保留 /health 仅进程存活、不依赖 DB）。
- **收益**：K8s/Compose 可用 /ready 判断“可接流量”；为后续 GET /api/v1/vins 从 DB 查数据打基础。
- **非目标**：本批不实现业务表查询、不跑迁移；仅“连接 + SELECT 1”检查。

---

## 1. 目标与非目标

| 目标 | 非目标 |
|------|--------|
| 读取 DATABASE_URL，建立到 PostgreSQL 的连接 | 执行 001_initial_schema.sql、业务表查询 |
| /ready：连接成功且 SELECT 1 成功 → 200 | /health 逻辑不变（仍仅进程存活） |
| /ready：连接或查询失败 → 503，body 可选 `{"ready":false}` | GET /api/v1/vins 查 DB（下一批） |

---

## 2. 实现要点

- **依赖**：libpq（PostgreSQL 官方 C 客户端），Ubuntu 下 `libpq-dev`；CMake 用 `FindPostgreSQL` 或 `pkg-config libpq`。
- **行为**：每次 GET /ready 时建立短连接（或使用连接池占位），执行 `SELECT 1`，成功则 200，失败则 503。
- **配置**：沿用 compose 中的 `DATABASE_URL`（如 `postgresql://teleop_user:xxx@postgres:5432/teleop_db`）。

---

## 3. 测试

- Postgres 未启动或不可达时，GET /ready → 503。
- Postgres 正常时，GET /ready → 200（/health 始终 200）。

---

## 4. 变更清单（预估）

| 路径 | 变更 |
|------|------|
| `backend/CMakeLists.txt` | 增加 FindPostgreSQL / libpq，链接 libpq |
| `backend/src/main.cpp` 或 `db_health.cpp` | 实现 DB 连通检查（连接 + SELECT 1），/ready 根据结果返回 200 或 503 |
| `backend/Dockerfile` | builder 与运行阶段安装 `libpq-dev` / `libpq5`（若需） |

---

## 5. 运行命令

```bash
docker compose build backend && docker compose up -d postgres backend
curl -s -w "\n%{http_code}\n" http://localhost:8081/ready
# Postgres 正常: 200；Postgres 停掉: 503
```

---

**请确认**：若同意按本提案实施，请回复 **CONFIRM** / **APPROVE** / **GO AHEAD**。
