# 客户端编译问题修复

## 问题总结

客户端在容器内编译时遇到 Qt6Gui 依赖问题：`WrapOpenGL could not be found`。

## 根本原因

Qt6Gui 需要 OpenGL 库，但 `docker.1ms.run/stateoftheartio/qt6:6.8-gcc-aqt` 镜像没有安装 OpenGL 开发库。

## 解决方案

在容器内安装 OpenGL 开发库：

```bash
docker compose exec -u root client-dev bash -c "apt-get update && apt-get install -y libgl1-mesa-dev libglu1-mesa-dev"
```

## 验证步骤

```bash
# 1. 安装 OpenGL 库
docker compose exec -u root client-dev bash -c "apt-get update && apt-get install -y libgl1-mesa-dev libglu1-mesa-dev"

# 2. 清理并重新配置
docker compose exec client-dev bash -c "cd /tmp/client-build && rm -rf * && cmake /workspace/client -DCMAKE_PREFIX_PATH=/opt/Qt/6.8.0/gcc_64 -DCMAKE_BUILD_TYPE=Debug"

# 3. 编译
docker compose exec client-dev bash -c "cd /tmp/client-build && make -j4"

# 4. 验证
docker compose exec client-dev bash -c "test -f /tmp/client-build/RemoteDrivingClient && ls -lh /tmp/client-build/RemoteDrivingClient"
```

## 永久解决方案

如果需要永久解决，可以创建一个自定义 Dockerfile：

```dockerfile
FROM docker.1ms.run/stateoftheartio/qt6:6.8-gcc-aqt

# 安装 OpenGL 开发库
RUN apt-get update && \
    apt-get install -y libgl1-mesa-dev libglu1-mesa-dev && \
    rm -rf /var/lib/apt/lists/*
```

然后修改 `docker-compose.yml` 使用自定义镜像。

## 当前状态

- ✅ OpenGL 库已安装
- ⏳ 等待验证编译结果
