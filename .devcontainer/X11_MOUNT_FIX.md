# X11 Socket 挂载问题修复指南

## 问题描述

容器启动后，X11 socket (`/tmp/.X11-unix`) 未正确挂载，导致 GUI 应用无法显示。

## 原因分析

### devcontainer.json 配置

```json
"mounts": [
  "source=/tmp/.X11-unix,target=/tmp/.X11-unix,type=bind"
]
```

配置看起来正确，但实际运行时可能：
1. 宿主机 `/tmp/.X11-unix` 不存在
2. 挂载失败但没有报错
3. 容器启动方式不同导致挂载未生效

## 解决方案

### 方案一：检查宿主机 X11 socket

在宿主机上检查：
```bash
ls -la /tmp/.X11-unix/
```

如果不存在，可能需要：
- 启动 X 服务器
- 或者使用其他显示方式（如 Xvfb）

### 方案二：使用 X11 转发（SSH）

如果通过 SSH 连接：
```bash
ssh -X user@host
```

### 方案三：手动挂载（调试用）

在容器启动后手动挂载：
```bash
# 在宿主机上
docker exec -it <container_id> mount --bind /tmp/.X11-unix /tmp/.X11-unix
```

### 方案四：使用 Xvfb（无头模式）

如果不需要真实显示，可以使用虚拟显示：
```bash
apt-get install -y xvfb
Xvfb :99 -screen 0 1024x768x24 &
export DISPLAY=:99
```

## 验证 X11 连接

```bash
# 检查 socket
ls -la /tmp/.X11-unix/

# 测试连接
xdpyinfo -display $DISPLAY

# 测试简单 GUI 应用
xeyes &
```

## 当前状态

- ⚠️ X11 socket 未挂载
- ✅ 程序可以启动（但无法显示 GUI）
- ✅ QML 文件路径已修复

## 下一步

1. 检查宿主机 X11 服务是否运行
2. 重新构建容器以确保挂载配置生效
3. 或使用 Xvfb 虚拟显示进行测试
