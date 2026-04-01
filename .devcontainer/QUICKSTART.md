# 快速开始指南

## 一键启动开发容器

### 步骤 1: 打开 Cursor
```bash
cd /home/wqs/bigdata/Remote-Driving
cursor .
```

### 步骤 2: 启动容器
1. 按 `F1` 或 `Ctrl+Shift+P`
2. 输入并选择：`Dev Containers: Reopen in Container`
3. 等待容器启动（首次启动可能需要几分钟）

### 步骤 3: 验证环境
容器启动后，在终端运行：
```bash
qmake --version    # 应显示 Qt 6.8.0
cmake --version    # 应显示 CMake 版本
g++ --version      # 应显示 GCC 版本
```

## 常用命令

### 构建项目
```bash
mkdir -p build && cd build
cmake .. -DCMAKE_PREFIX_PATH=/opt/Qt/6.8.0/gcc_64 -DCMAKE_BUILD_TYPE=Debug
make -j$(nproc)
```

### 运行应用
```bash
# 在 build 目录中
./bin/your_app_name
```

### 调试
1. 在代码中设置断点
2. 按 `F5` 启动调试
3. 或使用 `gdb` 命令行调试

## 端口访问

容器内的服务端口会自动转发到主机：

- **8080**: ZLMediaKit HTTP API → `http://localhost:8080`
- **1935**: RTMP → `rtmp://localhost:1935`
- **554**: RTSP → `rtsp://localhost:554`

## 故障排查

### 容器无法启动
```bash
# 检查 Docker
sudo systemctl status docker

# 检查镜像
docker images | grep qt6

# 查看日志
docker logs <container_id>
```

### 权限问题
```bash
# 在容器内
sudo chown -R user:user /workspace
```

### 重新构建容器
在 Cursor 中：`F1` → `Dev Containers: Rebuild Container`

## 更多信息

详细文档请查看 [README.md](./README.md)
