# Backend 生产环境 Dockerfile
# 基于 Ubuntu 22.04，使用 C++ 编译

FROM docker.1ms.run/library/ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

# 安装基础工具和依赖
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    git \
    ca-certificates \
    libpq-dev \
    libssl-dev \
    wget \
    pkg-config \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# 复制共享依赖（从项目根目录的 deps/）
# 注意：构建时需要将 ../deps/ 复制到容器内
COPY deps/ ./deps/

# 复制源码
COPY src/ ./src/
COPY config/ ./config/
COPY CMakeLists.txt ./

# 编译项目
RUN mkdir -p build && cd build \
    && cmake .. \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_PREFIX_PATH="/app/deps" \
    && make -j$(nproc)

# 暴露端口
EXPOSE 8000
ENV BACKEND_PORT=8000

# 设置环境变量默认值
ENV DATABASE_URL=postgresql://postgres:postgres@postgres:5432/teleop
ENV KEYCLOAK_URL=http://keycloak:8080
ENV KEYCLOAK_ISSUER=http://keycloak:8080/realms/teleop
ENV ZLM_API_URL=http://zlmediakit/index/api
# ZLM_PUBLIC_BASE：客户端拉流时使用的 ZLM 可达地址（需在 docker-compose 中覆盖为实际可达地址）
ENV ZLM_PUBLIC_BASE=http://zlmediakit:80
ENV MQTT_BROKER_URL=mqtt://mosquitto:1883

# 健康检查
HEALTHCHECK --interval=30s --timeout=10s --start-period=5s --retries=3 \
    CMD curl -f http://localhost:8000/health || exit 1

# 运行程序
CMD ["./build/teleop_backend"]
