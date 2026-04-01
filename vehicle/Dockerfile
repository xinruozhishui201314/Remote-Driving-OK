# 使用 Ubuntu 22.04 作为基础镜像
FROM docker.1ms.run/library/ubuntu:22.04

# 避免交互式提示
ENV DEBIAN_FRONTEND=noninteractive

# 安装构建工具、CMake、Git、MQTT客户端库
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    libssl-dev \
    wget \
    pkg-config \
    libmosquitto-dev \
    && rm -rf /var/lib/apt/lists/*

# 设置 Git 配置以防止克隆失败（针对 HTTP2 问题）
RUN git config --global http.version HTTP/1.1 && \
    git config --global http.postBuffer 524288000

# 安装 nlohmann/json (下载单文件，稳定且快速)
RUN mkdir -p /app/deps/nlohmann_json/include/nlohmann && \
    wget -q https://github.com/nlohmann/json/releases/download/v3.11.2/json.hpp \
    -O /app/deps/nlohmann_json/include/nlohmann/json.hpp

# 设置工作目录
WORKDIR /app

# 复制源代码和构建配置
COPY src/ ./src
COPY CMakeLists.txt .

# 构建项目
RUN cmake -B build && \
    cmake --build build

# 运行车辆服务
CMD ["./build/vehicle_service"]
