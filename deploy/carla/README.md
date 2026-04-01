# CARLA 容器内运行 C++ Bridge（推荐：先构建镜像再启动）

**推荐流程**：先构建满足 C++ Bridge 运行的 CARLA 镜像，后续直接启动该镜像容器，并对 C++ Bridge 逐项功能及远驾客户端→CARLA 整链做验证。

1. **构建镜像**（仅需执行一次）：`./scripts/build-carla-image.sh` → 生成 `remote-driving/carla-with-bridge:latest`
2. **启动容器**：`./scripts/start-all-nodes.sh` → 使用已构建镜像启动 CARLA，entrypoint 在容器内编译并运行 C++ Bridge
3. **推流链路分步验证**（推荐）：`./scripts/verify-carla-stream-chain.sh` — 5 步依次检查，任一步失败即停并提示修复
4. **C++ Bridge 逐项验证**：`./scripts/verify-carla-bridge-cpp-features.sh`（start_stream/stop_stream/status 等）
5. **远驾客户端→CARLA 整链验证**：`./scripts/verify-full-chain-client-to-carla.sh`（会话创建 → MQTT → 四路流）

- **视频流来源**：默认 **USE_PYTHON_BRIDGE=1**，使用 **Python Bridge**（CARLA 仿真相机）。若设 `USE_PYTHON_BRIDGE=0`，则使用 C++ Bridge（testsrc 测试图案）。
- **如何确认当前视频源**：看容器日志 `docker logs carla-server 2>&1 | grep 视频源`：出现「视频源: CARLA 仿真相机」即为 CARLA 画面；出现「视频源: testsrc」则为 ffmpeg 测试源。
- **C++ Bridge**：USE_PYTHON_BRIDGE=0 时在容器内编译并运行；无 LibCarla 时为 ffmpeg testsrc。
- **Python Bridge**：USE_PYTHON_BRIDGE=1 时使用 `carla_bridge.py`（需 CARLA Python 正常）；C++ 编译失败时也会回退到 Python。
- **为何当前是 testsrc**：若镜像内仅有 Python 2.7 的 CARLA egg（无 Python 3 可用 `import carla`），entrypoint 会打日志并自动回退到 C++ Bridge（testsrc）。要看到 CARLA 画面需使用提供 Python 3 carla 的镜像，或挂载 `carla-src/PythonAPI/carla` 供 entrypoint 使用。
- **修改后验证**：修改 `carla-bridge` 或 `deploy/carla` 后请执行：`VERIFY_CARLA=1 ./scripts/check.sh`（需 carla-server 已运行），或先启动 CARLA 再执行 `./scripts/verify-carla-stream-chain.sh`。

## 宿主机要求

- **NVIDIA 驱动**：`nvidia-smi` 可用。
- **NVIDIA Container Toolkit**：使 Docker 容器可使用 GPU，否则 CARLA 容器会报 `unknown or invalid runtime name: nvidia`。安装方法：
  ```bash
  sudo ./scripts/install-nvidia-container-toolkit.sh
  ```
  安装后需重启 Docker；验证：`docker run --rm --runtime=nvidia nvidia/cuda:12.0-base-ubuntu22.04 nvidia-smi`
- **CARLA 源码（可选）**：`./scripts/setup-carla-src.sh` 将 GitHub 最新 CARLA 克隆到 `carla-src/`，挂载到容器 `/opt/carla-src` 后 entrypoint 优先使用其 PythonAPI。

## 运行（先构建后启动）

```bash
# 1. 构建 CARLA + C++ Bridge 运行环境镜像（约数分钟，依赖 carlasim/carla:latest）
./scripts/build-carla-image.sh

# 2. 启动所有节点（含已构建的 CARLA 镜像容器）
./scripts/start-all-nodes.sh

# 3. 可选：C++ Bridge 逐项功能验证
./scripts/verify-carla-bridge-cpp-features.sh

# 4. 可选：远驾客户端到 CARLA 整链验证
./scripts/verify-full-chain-client-to-carla.sh
```

Compose 使用 `image: remote-driving/carla-with-bridge:latest`（由 `deploy/carla/Dockerfile` 构建），挂载 `./carla-bridge`、`./deploy/carla/entrypoint.sh`、`./carla-src`。

## 挂载约定

| 容器路径 | 说明 |
|----------|------|
| `/workspace/carla-bridge` | **必选**：项目内 `carla-bridge` 源码（只读挂载即可）。entrypoint 会在容器内从 `carla-bridge/cpp` 编译 C++ Bridge，构建目录为容器内 `/tmp/carla-bridge-cpp-build` |
| `/opt/carla-src` | **推荐**：工程目录下 `carla-src/`（GitHub 最新代码），由 `./scripts/setup-carla-src.sh` 拉取；entrypoint 优先使用其中 PythonAPI |
| `/data/carla` | 可选：场景/数据集；缺省为空卷。需挂载时在 override 中指定 `- /path:/data/carla:ro` |

使用**自定义镜像**（本项目 `deploy/carla/Dockerfile`）时，镜像内已包含 C++ 构建依赖（**CMake 3.16+**、build-essential、Paho MQTT C/C++），挂载 `carla-bridge` 后即可在启动时自动编译并运行 C++ Bridge。

### C++ Bridge 编译失败与 CARLA 源码的关系

- **C++ Bridge 编译失败**的常见原因是镜像内 **CMake 版本过旧**（需 3.16+）。本 Dockerfile 已通过 Kitware 源安装 CMake 3.16+，**无需拉取 CARLA 源码**即可在容器内编过 `carla-bridge/cpp`。
- **CARLA 源码**（[carla-simulator/carla](https://github.com/carla-simulator/carla)）挂载的用途：
  - **PythonAPI**：挂载到 `/opt/carla-src` 后 entrypoint 可优先使用其 `PythonAPI/carla`（解决镜像内 egg 与 Python3 不兼容时的 import 问题）。
  - **LibCarla（可选）**：若需 C++ Bridge 链接 LibCarla 原生客户端，需在宿主机或镜像内按 CARLA 官方文档先构建出 `Build/libcarla_client.a`，再在容器中设 `CARLA_ROOT` 指向挂载的 CARLA 源码根目录；未设置时 C++ Bridge 仍可编过并运行（仅 MQTT/推流，无 LibCarla）。
- **推荐**：宿主机执行 `./scripts/setup-carla-src.sh` 拉取 CARLA 源码时，使用与 `carlasim/carla` 镜像版本一致的分支或 tag（如 `0.9.13`），避免 ue5-dev 与当前 UE4 镜像不匹配。

## 若未挂载 carla-src 或镜像内无 PythonAPI

- **Python `carla`**：entrypoint 会依次尝试：挂载的 `/opt/carla-src/PythonAPI/carla`、镜像内 `/home/carla/PythonAPI/carla/dist/*.egg`、最后 `pip install carla`。
- **场景数据**：若镜像缺地图，需从网络下载或使用官方 Release；可将数据挂载到 `/data/carla`（compose override）。

## 文件说明

- `Dockerfile`：基于 `carlasim/carla:latest`，安装 ffmpeg、Python 依赖、C++ 构建链（cmake、build-essential、Paho MQTT C/C++ 从源码安装），设置 entrypoint。
- `entrypoint.sh`：容器内先启动 CARLA 服务（含可选地图），等待端口就绪；若 **USE_PYTHON_BRIDGE=1** 则直接运行 Python Bridge（CARLA 相机推流）；否则在 `/tmp` 下编译并运行 C++ Bridge，失败则回退到 `carla_bridge.py`。

## 固化镜像（Python Bridge 运行环境就绪后）

当 `./scripts/verify-carla-stream-chain.sh` 的 **[2/5] Bridge 进程在容器内运行** 通过且 Python Bridge 稳定时，可将当前镜像固化为带标签与 tar 备份：

```bash
# 打 verified 标签（与 latest 同镜像）
docker tag remote-driving/carla-with-bridge:latest remote-driving/carla-with-bridge:verified

# 保存为 tar（供离线或它机加载）
docker save -o deploy/carla/carla-with-bridge-latest.tar remote-driving/carla-with-bridge:latest
```

- **latest**：每次 `./scripts/build-carla-image.sh` 或 `docker compose ... build carla` 构建产出。
- **verified**：与 latest 同内容，表示「已通过 Bridge 运行验证」的版本，便于回滚或对照。
- **carla-with-bridge-latest.tar**：`build-carla-image.sh` 在 `SAVE_CARLA_IMAGE=1`（默认）时也会生成；加载：`docker load -i deploy/carla/carla-with-bridge-latest.tar`。
