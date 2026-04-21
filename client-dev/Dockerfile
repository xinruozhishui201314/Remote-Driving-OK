# 使用 Ubuntu 22.04 作为基础镜像
FROM docker.1ms.run/library/ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive
# 强制 Qt 使用硬件加速路径
ENV QT_XCB_GL_INTEGRATION=glx
ENV __GLX_VENDOR_LIBRARY_NAME=nvidia

# 安装基础工具和开发库
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    curl \
    vim \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# 复制源码
COPY . .

# 编译
RUN cmake -B build && \
    cmake --build build

# 保持容器运行以便开发或调试
CMD ["sleep", "infinity"]
