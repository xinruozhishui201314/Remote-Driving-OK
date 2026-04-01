# OpenGL 依赖问题修复指南

## 问题描述

CMake 配置时出现以下错误：

```
Could NOT find OpenGL (missing: OPENGL_opengl_LIBRARY OPENGL_glx_LIBRARY OPENGL_INCLUDE_DIR)
Qt6Gui could not be found because dependency WrapOpenGL could not be found
Qt6Quick could not be found because dependency Qt6Gui could not be found
```

## 原因

Qt6 的 GUI 和 Quick 模块依赖 OpenGL，但容器中缺少 OpenGL 开发库（头文件和 CMake 配置文件）。

## 解决方案

### 已自动安装（setup.sh）

`setup.sh` 已更新，会自动安装以下 OpenGL 相关开发库：

- `libgl1-mesa-dev` - OpenGL 开发库（必需）
- `libglu1-mesa-dev` - GLU 开发库（推荐）
- `libxcb-*-dev` - X11/XCB 相关开发库（Qt6 GUI 必需）

### 手动安装（如果需要）

```bash
# 安装 OpenGL 开发库
sudo apt-get update
sudo apt-get install -y \
    libgl1-mesa-dev \
    libglu1-mesa-dev \
    libxcb-xinerama0-dev \
    libxcb-cursor-dev \
    libxcb-keysyms1-dev \
    libxcb-image0-dev \
    libxcb-shm0-dev \
    libxcb-icccm4-dev \
    libxcb-sync-dev \
    libxcb-xfixes0-dev \
    libxcb-shape0-dev \
    libxcb-randr0-dev \
    libxcb-render-util0-dev \
    libxcb-util-dev \
    libxcb-xkb-dev \
    libxkbcommon-dev \
    libxkbcommon-x11-dev
```

## 验证安装

### 检查 OpenGL 库

```bash
# 检查库文件
ls -la /usr/lib/x86_64-linux-gnu/libGL.so*
ls -la /usr/include/GL/gl.h

# 检查 pkg-config
pkg-config --exists gl && echo "✓ OpenGL found" || echo "✗ OpenGL not found"
```

### 重新配置 CMake

```bash
cd /workspaces/Remote-Driving/client/build
rm -rf *
cmake .. \
    -DCMAKE_PREFIX_PATH=/opt/Qt/6.8.0/gcc_64 \
    -DCMAKE_BUILD_TYPE=Debug
```

## 常见问题

### Q1: 安装后仍然找不到 OpenGL

**解决**：
1. 确保已安装 `libgl1-mesa-dev`
2. 清理 CMake 缓存：`rm -rf build/*`
3. 重新运行 CMake 配置

### Q2: 在宿主机上编译正常，容器内失败

**原因**：宿主机可能已安装 OpenGL 开发库，但容器镜像中没有。

**解决**：容器启动时 `setup.sh` 会自动安装，或手动安装上述包。

### Q3: 是否需要 GPU 支持？

**不需要**：Mesa 软件渲染已足够用于开发和测试。如果需要硬件加速，需要：
- NVIDIA GPU：安装 `nvidia-container-toolkit`
- 配置 Docker 使用 GPU：`--gpus all`

## 总结

✅ **已修复**：`setup.sh` 会自动安装 OpenGL 开发库
✅ **持久化**：每次容器启动时自动安装
✅ **验证**：重新运行 `bash build.sh` 应该可以成功配置

---

**重新运行编译命令即可！** 🚀
