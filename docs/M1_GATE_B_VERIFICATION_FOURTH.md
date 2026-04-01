# M1 GATE B 验证（第四批）：/ready DB 健康检查

**对应提案**: [M1_GATE_A_PROPOSAL_FOURTH.md](./M1_GATE_A_PROPOSAL_FOURTH.md)

---

## 1. 验证目标

- GET /ready 在 **PostgreSQL 可达** 时返回 **200**，body `{"ready":true}`。
- GET /ready 在 **PostgreSQL 不可达**（未启动或 DATABASE_URL 错误）时返回 **503**，body `{"ready":false}`。
- GET /health 不受影响，始终返回 200（仅进程存活）。

---

## 2. 前置条件

- 已执行 `docker compose build backend` 且构建成功。
- 已启动 postgres 服务（compose 中 DATABASE_URL 指向 postgres:5432）。

---

## 3. 验证步骤

### 3.1 Postgres 正常时 /ready 为 200

```bash
cd /home/wqs/bigdata/Remote-Driving
docker compose up -d postgres backend
sleep 5
curl -s -w "\nHTTP_CODE:%{http_code}\n" http://localhost:8081/ready
```

**预期**：HTTP_CODE:200，body 含 `"ready":true`。

### 3.2 Postgres 停掉时 /ready 为 503

```bash
docker compose stop postgres
sleep 2
curl -s -w "\nHTTP_CODE:%{http_code}\n" http://localhost:8081/ready
```

**预期**：HTTP_CODE:503，body 含 `"ready":false`。

### 3.3 /health 始终 200

```bash
# postgres 已停的情况下
curl -s -w "\nHTTP_CODE:%{http_code}\n" http://localhost:8081/health
```

**预期**：HTTP_CODE:200，body `{"status":"ok"}`。

### 3.4 恢复 postgres 后 /ready 恢复 200

```bash
docker compose start postgres
sleep 5
curl -s -w "\nHTTP_CODE:%{http_code}\n" http://localhost:8081/ready
```

**预期**：HTTP_CODE:200。

---

## 4. 编译与运行说明

- **构建**：`docker compose build backend`（Docker 内安装 libpq-dev，链接 libpq）。
- **运行**：`docker compose up -d postgres backend`；backend 依赖 postgres 健康后启动。
- **配置**：DATABASE_URL 由 compose 注入，格式 `postgresql://teleop_user:xxx@postgres:5432/teleop_db`。

---

## 5. 变更清单（已实施）

| 路径 | 变更 |
|------|------|
| `backend/CMakeLists.txt` | FindPostgreSQL + pkg-config 回退，链接 libpq |
| `backend/src/main.cpp` | `#include <libpq-fe.h>`，`check_db_ready()`，/ready 根据 DB 返回 200/503 |
| `backend/Dockerfile` | builder: libpq-dev；runtime: libpq5 |
