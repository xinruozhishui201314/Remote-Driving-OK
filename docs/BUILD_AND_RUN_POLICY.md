# 远程驾驶系统：编译与运行策略

## 原则

**所有节点默认在 Docker 镜像/容器中编译和运行，禁止在宿主机上编译和运行。**

- 保证环境一致、可复现，避免“在我机器上能跑”问题。
- 客户端、车辆端、媒体服务、后端等均通过 Docker Compose 与对应 Dockerfile 在容器内完成构建与运行。

## 各节点与容器对应关系

| 节点 | 编译方式 | 运行方式 | Compose 服务 / 镜像 |
|------|----------|----------|----------------------|
| **客户端** | 在 `client-dev` 容器内 `cmake` + `make` | 在 `client-dev` 容器内执行 `RemoteDrivingClient` | `client-dev`（Dockerfile.client-dev 构建：Qt6 + 镜像内已装 libdatachannel，启动即完备） |
| **车辆端** | 在 `vehicle` 镜像构建时或容器入口脚本内编译 | 容器启动后入口脚本编译并运行 `VehicleSide` | `vehicle`（docker-compose.vehicle.dev.yml） |
| **媒体服务器** | 使用现成镜像，无需在宿主机编译 | `docker compose up -d zlmediakit` | `zlmediakit` |
| **后端** | `docker build` 构建镜像 | `docker compose up -d backend` | `backend` |

## 正确用法（宿主机仅发起命令）

### 一行命令：全链路启动并在客户端操作验证

```bash
make e2e-full
# 或
bash scripts/start-full-chain.sh
```

会启动所有节点（Postgres / Keycloak / Coturn / ZLM / Backend / MQTT / 车辆端 / client-dev）、做逐环体验证，最后启动客户端窗口，可在客户端登录、选车、连接车端、拉四路流并操作验证。停止：`make e2e-stop`。

### 各节点单独编译/运行

```bash
# 客户端：编译 + 运行（均在 client-dev 容器内）
make build-client   # 在 client-dev 容器内编译
make run            # 或 make run-client：在 client-dev 容器内运行

# 车辆端：镜像构建 + 启动容器（容器内编译并运行）
make build-vehicle  # docker compose build vehicle
make run-vehicle    # docker compose up -d vehicle（需 vehicle.dev compose）

# 媒体：使用镜像，仅启动
make run-media      # docker compose up -d zlmediakit

# 后端：镜像构建 + 启动
make build-backend
make run-backend
```

## 禁止的用法

- **禁止** 在宿主机执行 `cd client && ./build.sh` 或 `./run.sh` 进行客户端编译/运行。
- **禁止** 在宿主机执行 `cd Vehicle-side && ./build.sh` 或 `./run.sh` 进行车辆端编译/运行。
- **禁止** 在宿主机执行 `cd media && ./build.sh` 或 `./run.sh` 进行媒体服务器编译/运行（流媒体使用 ZLMediaKit 官方镜像）。

子目录下的 `build.sh` / `run.sh` 设计为**仅在对应容器内**被调用；在宿主机直接执行将报错并提示使用 `make` 目标。
