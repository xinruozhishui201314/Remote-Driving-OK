# M1 GATE B 验证：后端健康检查（第一批）

**对应提案**: [M1_GATE_A_PROPOSAL_FIRST.md](./M1_GATE_A_PROPOSAL_FIRST.md)  
**实施日期**: 2026-02-06

---

## 1. 变更摘要与文件清单

| 文件 | 变更类型 | 说明 |
|------|----------|------|
| `backend/CMakeLists.txt` | 修改 | CMake 3.14+；FetchContent 拉取 cpp-httplib；构建可执行文件 `teleop_backend` |
| `backend/src/main.cpp` | **新增** | HTTP 服务：GET /health → `{"status":"ok"}`，GET /ready → `{"ready":true}`，端口由 PORT 环境变量指定 |
| `backend/Dockerfile` | 重写 | 多阶段构建；**基础镜像为 docker.1ms.run/library/ubuntu:22.04**（[1ms.run](https://1ms.run/)）；HEALTHCHECK 与 compose 一致 |
| `docker-compose.yml` | 修改 | backend 端口映射改为 `8081:8080`，避免与 Keycloak 8080 冲突 |
| `scripts/check.sh` | 修改 | 增加 Backend 健康检查：当 backend 容器 Up 时 curl http://localhost:8081/health |
| `Makefile` | 修改 | 增加 `build-backend`、`clean-backend`，并纳入 `build-all` / `clean-all` |

---

## 2. 测试与覆盖

- **单元测试**：未新增（纯 HTTP 路由，按提案为可选）。
- **集成**：本地 CMake 配置 + 编译通过；二进制在宿主机因权限未直接运行，Docker 构建因网络超时未在本环境拉取 base 镜像。
- **e2e/check**：`check.sh` 在 backend 容器运行时会请求 `http://localhost:8081/health` 判定通过。

**Definition of Done**：GET /health 返回 200 且含 `"status"`；backend 容器 healthcheck 使用 `curl -f http://localhost:8080/health`；`build-backend` 与 `check.sh`（在 backend 已启动时）可验证。

**2026-02-06 补充**：Dockerfile builder 阶段已增加 `make`，Docker 内构建通过；镜像基于 docker.1ms.run/library/ubuntu:22.04；单独运行容器 `curl http://localhost:8081/health` 返回 `{"status":"ok"}`。

---

## 3. 运行命令与验证

**约定：不在宿主机运行后端，仅在 Docker 内构建与运行；基础镜像从 [1ms.run](https://1ms.run/) 拉取。**

```bash
cd /home/wqs/bigdata/Remote-Driving

# 方式一：Makefile（推荐）
make build-backend    # Docker 内构建，FROM docker.1ms.run/library/ubuntu:22.04
make run-backend     # docker compose up -d backend

# 方式二：直接使用 Compose（会按 Dockerfile 在 Docker 内构建并启动）
docker compose up -d backend

# 健康检查
curl -s http://localhost:8081/health   # 期望: {"status":"ok"}

# 检查脚本（backend 已启动时会检查 /health）
./scripts/check.sh
```

---

## 4. 安全与安全清单

- [x] 健康接口不返回敏感信息（仅 `status`/`ready`）。
- [x] `/health`、`/ready` 免鉴权，符合探针约定。
- [ ] JWT/会话/VIN/deny-by-default：本批次不涉及，后续 GATE A 覆盖。

---

## 5. 结论

M1 第一批（后端健康检查）已按 GATE A 提案实现：代码与构建、Dockerfile、compose 端口、check.sh、Makefile 已就绪。本地 CMake 构建通过；Docker 构建与 backend 容器运行需在具备镜像拉取能力的环境中执行。请在本地或 CI 中运行 `make build-backend`、`docker compose up -d backend` 与 `./scripts/check.sh` 做最终验证。
