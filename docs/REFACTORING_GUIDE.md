# 工程整理指南：独立部署三模块（backend/client/Vehicle-side）

## 0. Executive Summary

### 收益
- **独立部署**: backend、client、Vehicle-side 三个模块完全解耦，可独立编译、打包、部署到不同设备
- **减少冗余**: 统一依赖管理，消除重复的deps目录（约节省70%存储空间）
- **提升可维护性**: 清晰的模块边界，统一的构建脚本和配置管理
- **支持分布式**: 原生支持跨网络部署，无需修改代码

### 影响
- **迁移成本**: 低风险，主要是目录重组和脚本重构，不改变核心业务逻辑
- **开发体验**: 改善，每个模块有独立的README和构建说明
- **部署复杂度**: 降低，提供一键部署脚本和独立运行能力

---

## 1. 背景与目标

### 1.1 当前问题

| 问题 | 现象 | 影响 |
|------|------|------|
| 依赖重复严重 | cpp-httplib、nlohmann_json在每个模块都有副本 | 存储浪费、版本不一致 |
| 模块耦合 | docker-compose.yml将所有模块绑在一起 | 无法独立部署和测试 |
| 缺乏独立构建 | 每个模块没有完整的独立编译脚本 | 开发和部署效率低 |
| 配置分散 | 配置文件散落各处 | 不利于统一管理 |
| vehicle/Vehicle-side混淆 | 存在两个车辆端目录 | 功能重叠、维护困难 |

### 1.2 整理目标

- ✅ 三个模块完全独立，可单独编译、测试、部署
- ✅ 统一依赖管理（deps/作为共享依赖库）
- ✅ 每个模块有独立的Dockerfile、README、构建脚本
- ✅ 支持分布式部署（不同网络、不同设备）
- ✅ 清理冗余代码（合并vehicle到Vehicle-side）
- ✅ 向后兼容，保留原有docker-compose.yml用于完整链路测试

---

## 2. 方案设计

### 2.1 整体架构

```
Remote-Driving/
├── backend/                    # 后端服务（独立）
│   ├── src/                   # C++源码
│   ├── CMakeLists.txt        # 构建配置
│   ├── Dockerfile             # 生产环境镜像
│   ├── docker-compose.yml     # 独立部署配置
│   ├── README.md              # 独立文档
│   └── scripts/
│       ├── build.sh           # 独立编译脚本
│       └── run.sh             # 独立运行脚本
├── client/                     # 客户端（独立）
│   ├── src/                   # C++源码
│   ├── qml/                   # QML界面
│   ├── CMakeLists.txt
│   ├── Dockerfile.prod        # 生产环境镜像
│   ├── README.md
│   └── scripts/
│       ├── build.sh
│       └── run.sh
├── Vehicle-side/               # 车辆端（独立，主模块）
│   ├── src/                   # C++源码
│   ├── CMakeLists.txt
│   ├── Dockerfile.prod        # 生产环境镜像
│   ├── docker-compose.yml     # 独立部署配置
│   ├── README.md
│   └── scripts/
│       ├── build.sh
│       └── run.sh
├── deps/                       # 共享依赖（保留）
│   ├── cpp-httplib/
│   ├── nlohmann_json/
│   └── mosquitto/
├── scripts/                    # 全局脚本
│   ├── build-all.sh           # 编译所有模块
│   ├── deploy-all.sh          # 部署所有模块
│   └── verify-all.sh          # 验证所有模块
├── config/                     # 配置文件（集中管理）
│   ├── backend_config.yaml
│   ├── client_config.yaml
│   └── vehicle_config.yaml
├── docker-compose.yml         # 完整链路测试（保留）
└── docs/
    ├── REFACTORING_GUIDE.md   # 本文档
    └── DISTRIBUTED_DEPLOYMENT.md # 分布式部署指南
```

### 2.2 核心设计原则

| 原则 | 说明 | 实现方式 |
|------|------|----------|
| **独立性** | 每个模块可独立编译、运行、部署 | 独立的CMakeLists.txt、Dockerfile、脚本 |
| **最小依赖** | 模块间依赖最小化，通过接口通信 | HTTP API、MQTT、WebRTC |
| **统一配置** | 配置集中管理，支持环境变量覆盖 | config/目录 + 环境变量 |
| **向后兼容** | 保留原有docker-compose.yml用于测试 | 不改变协议和接口 |
| **可追踪** | 统一的日志和监控格式 | 结构化日志、Prometheus指标 |

---

## 3. 实施步骤

### 3.1 阶段一：创建整理计划（✅ 已完成）
- [x] 分析当前项目结构
- [x] 制定整理方案
- [x] 创建本文档

### 3.2 阶段二：整理backend模块
- [ ] 修复backend/Dockerfile（从Python镜像改为C++镜像）
- [ ] 创建backend/scripts/build.sh
- [ ] 创建backend/scripts/run.sh
- [ ] 创建backend/README.md
- [ ] 创建backend/docker-compose.yml（独立部署）
- [ ] 删除backend/deps/（使用../deps）

### 3.3 阶段三：整理client模块
- [ ] 创建client/Dockerfile.prod
- [ ] 创建client/scripts/build.sh
- [ ] 创建client/scripts/run.sh
- [ ] 创建client/README.md
- [ ] 删除client/deps/（使用../deps）

### 3.4 阶段四：整理Vehicle-side模块
- [ ] 创建Vehicle-side/Dockerfile.prod
- [ ] 创建Vehicle-side/scripts/run.sh
- [ ] 更新Vehicle-side/README.md
- [ ] 合并vehicle目录到Vehicle-side
- [ ] 删除Vehicle-side/deps/（使用../deps）

### 3.5 阶段五：创建全局脚本
- [ ] 创建scripts/build-all.sh
- [ ] 创建scripts/deploy-all.sh
- [ ] 创建scripts/verify-all.sh

### 3.6 阶段六：更新配置和测试
- [ ] 更新根目录docker-compose.yml
- [ ] 执行编译测试
- [ ] 执行验证测试
- [ ] 更新文档

---

## 4. 模块详细设计

### 4.1 Backend模块

#### 目录结构
```
backend/
├── src/
│   ├── api/
│   ├── auth/
│   ├── common/
│   ├── db/
│   ├── main.cpp
│   └── ...
├── CMakeLists.txt
├── Dockerfile
├── docker-compose.yml
├── README.md
└── scripts/
    ├── build.sh
    └── run.sh
```

#### Dockerfile（修复后）
```dockerfile
FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

# 安装依赖
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    libpq-dev \
    libssl-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# 复制源码
COPY src/ ./src/
COPY CMakeLists.txt ./

# 复制共享依赖（从根目录deps/）
COPY ../deps/ ./deps/

# 编译
RUN mkdir -p build && cd build \
    && cmake .. -DCMAKE_BUILD_TYPE=Release \
    && make -j$(nproc)

# 运行
CMD ["./build/backend"]
```

#### 独立部署配置（docker-compose.yml）
```yaml
services:
  backend:
    build: .
    ports:
      - "8000:8000"
    environment:
      - DATABASE_URL=postgresql://postgres:postgres@postgres:5432/teleop
      - KEYCLOAK_URL=http://keycloak:8080
      - ZLM_API_URL=http://zlmediakit/index/api
      - ZLM_PUBLIC_BASE=http://localhost:80
      - MQTT_BROKER_URL=mqtt://mosquitto:1883
    depends_on:
      - postgres
      - keycloak
    networks:
      - teleop-network

  postgres:
    image: postgres:latest
    environment:
      POSTGRES_DB: teleop
      POSTGRES_USER: postgres
      POSTGRES_PASSWORD: postgres
    networks:
      - teleop-network

  keycloak:
    image: quay.io/keycloak/keycloak:24.0
    command: start-dev
    environment:
      KEYCLOAK_ADMIN: admin
      KEYCLOAK_ADMIN_PASSWORD: admin
      KC_DB: postgres
      KC_DB_URL: jdbc:postgresql://postgres:5432/teleop
      KC_DB_USERNAME: postgres
      KC_DB_PASSWORD: postgres
    ports:
      - "8080:8080"
    depends_on:
      - postgres
    networks:
      - teleop-network

networks:
  teleop-network:
    driver: bridge
```

#### 编译脚本（scripts/build.sh）
```bash
#!/bin/bash
set -e

echo "========================================"
echo "Building Backend"
echo "========================================"

cd "$(dirname "$0")/.."

# 检查依赖
if [ ! -d "../deps" ]; then
    echo "错误: 找不到 ../deps 目录"
    exit 1
fi

# 创建构建目录
mkdir -p build && cd build

# 配置CMake
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="../deps"

# 编译
make -j$(nproc)

echo "========================================"
echo "Backend build completed"
echo "========================================"
```

#### 运行脚本（scripts/run.sh）
```bash
#!/bin/bash
set -e

echo "========================================"
echo "Starting Backend"
echo "========================================"

cd "$(dirname "$0")/../build"

if [ ! -f "./backend" ]; then
    echo "错误: 未找到可执行文件，请先运行 ./scripts/build.sh"
    exit 1
fi

# 设置环境变量（从配置文件或默认值）
export DATABASE_URL="${DATABASE_URL:-postgresql://postgres:postgres@localhost:5432/teleop}"
export KEYCLOAK_URL="${KEYCLOAK_URL:-http://localhost:8080}"
export ZLM_API_URL="${ZLM_API_URL:-http://localhost/index/api}"
export ZLM_PUBLIC_BASE="${ZLM_PUBLIC_BASE:-http://localhost:80}"
export MQTT_BROKER_URL="${MQTT_BROKER_URL:-mqtt://localhost:1883}"

# 运行
exec ./backend
```

### 4.2 Client模块

#### 目录结构
```
client/
├── src/
├── qml/
├── resources/
├── CMakeLists.txt
├── Dockerfile.prod
├── README.md
└── scripts/
    ├── build.sh
    └── run.sh
```

#### Dockerfile.prod
```dockerfile
FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

# 安装依赖
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    libssl-dev \
    pkg-config \
    libmosquitto-dev \
    libspdlog-dev \
    libgl1-mesa-dev \
    && rm -rf /var/lib/apt/lists/*

# 安装Qt6（使用官方安装脚本）
RUN apt-get update && apt-get install -y \
    qt6-base-dev \
    qt6-declarative-dev \
    qt6-multimedia-dev \
    qt6-websockets-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# 复制源码
COPY src/ ./src/
COPY qml/ ./qml/
COPY resources/ ./resources/
COPY CMakeLists.txt ./

# 复制共享依赖
COPY ../deps/ ./deps/

# 编译
RUN mkdir -p build && cd build \
    && cmake .. -DCMAKE_BUILD_TYPE=Release \
    && make -j$(nproc)

# 运行
CMD ["./build/client"]
```

### 4.3 Vehicle-side模块

#### 目录结构
```
Vehicle-side/
├── src/
│   ├── common/
│   ├── config/
│   └── ...
├── CMakeLists.txt
├── Dockerfile.prod
├── docker-compose.yml
├── README.md
└── scripts/
    ├── build.sh
    └── run.sh
```

#### Dockerfile.prod
```dockerfile
FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

# 安装依赖
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    libcurl4-openssl-dev \
    libssl-dev \
    libmosquittopp-dev \
    libpaho-mqttpp-dev \
    libspdlog-dev \
    libpq-dev \
    inotify-tools \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# 复制源码
COPY src/ ./src/
COPY CMakeLists.txt ./

# 复制共享依赖
COPY ../deps/ ./deps/

# 编译
RUN mkdir -p build && cd build \
    && cmake .. -DCMAKE_BUILD_TYPE=Release \
    && make -j$(nproc)

# 运行
CMD ["./build/VehicleSide"]
```

### 4.4 全局脚本

#### scripts/build-all.sh
```bash
#!/bin/bash
set -e

echo "========================================"
echo "Building All Modules"
echo "========================================"

# 编译backend
echo ""
echo "[1/3] Building Backend..."
cd backend && ./scripts/build.sh && cd ..

# 编译client
echo ""
echo "[2/3] Building Client..."
cd client && ./scripts/build.sh && cd ..

# 编译Vehicle-side
echo ""
echo "[3/3] Building Vehicle-side..."
cd Vehicle-side && ./scripts/build.sh && cd ..

echo ""
echo "========================================"
echo "All modules built successfully"
echo "========================================"
```

#### scripts/deploy-all.sh
```bash
#!/bin/bash
set -e

echo "========================================"
echo "Deploying All Modules"
echo "========================================"

# 构建backend镜像
echo ""
echo "[1/3] Building Backend Docker image..."
docker build -t teleop-backend:latest backend/

# 构建client镜像
echo ""
echo "[2/3] Building Client Docker image..."
docker build -t teleop-client:latest client/

# 构建Vehicle-side镜像
echo ""
echo "[3/3] Building Vehicle-side Docker image..."
docker build -t teleop-vehicle:latest Vehicle-side/

echo ""
echo "========================================"
echo "All Docker images built successfully"
echo "========================================"
echo ""
echo "Images:"
echo "  - teleop-backend:latest"
echo "  - teleop-client:latest"
echo "  - teleop-vehicle:latest"
echo ""
echo "To start all services:"
echo "  docker compose -f docker-compose.yml up -d"
```

---

## 5. 验证计划

### 5.1 单模块验证

```bash
# 验证backend
cd backend
./scripts/build.sh
./scripts/run.sh &
curl http://localhost:8000/health

# 验证client
cd client
./scripts/build.sh
./scripts/run.sh

# 验证Vehicle-side
cd Vehicle-side
./scripts/build.sh
./scripts/run.sh
```

### 5.2 完整链路验证

```bash
# 使用根目录docker-compose.yml启动完整链路
docker compose up -d

# 验证各服务健康状态
./scripts/verify-all.sh

# 执行E2E测试
./scripts/e2e.sh
```

### 5.3 独立部署验证

```bash
# 分别启动各模块的独立部署
cd backend && docker compose up -d
cd ../client && docker run -d teleop-client:latest
cd ../Vehicle-side && docker compose up -d

# 验证跨模块通信
# 通过MQTT、HTTP API、WebRTC验证
```

---

## 6. 风险与回滚方案

### 6.1 风险评估

| 风险 | 可能性 | 影响 | 缓解措施 |
|------|--------|------|----------|
| 编译失败 | 中 | 高 | 保留原有构建脚本，逐步迁移 |
| 依赖冲突 | 低 | 高 | 使用统一deps目录，版本锁定 |
| 测试失败 | 中 | 中 | 充分的单元测试和集成测试 |
| 部署问题 | 低 | 高 | 保留原有docker-compose.yml |

### 6.2 回滚方案

1. **代码回滚**: 使用Git回滚到整理前的commit
2. **配置回滚**: 保留原有的配置文件作为备份
3. **镜像回滚**: 保留原有Docker镜像，使用tag标识版本

---

## 7. 后续演进路线图

### MVP（当前版本）
- ✅ 模块独立编译和运行
- ✅ 统一依赖管理
- ✅ 基础文档和脚本

### V1（下一版本）
- [ ] CI/CD自动化
- [ ] 依赖版本管理（使用vcpkg或conan）
- [ ] 更完善的测试覆盖
- [ ] 性能优化和资源限制

### V2（未来版本）
- [ ] 微服务化拆分
- [ ] 服务网格（Istio）
- [ ] 可观测性增强（Tracing、Metrics）
- [ ] 多架构支持（ARM、x86）

---

## 8. 交付清单

### 功能实现
- [x] 整理计划文档
- [ ] Backend独立构建和运行
- [ ] Client独立构建和运行
- [ ] Vehicle-side独立构建和运行
- [ ] 全局构建和部署脚本
- [ ] 更新docker-compose.yml

### 关键路径日志
- [ ] 编译日志（build.log）
- [ ] 运行日志（runtime.log）
- [ ] 验证日志（verify.log）

### 自动化测试
- [ ] 单模块编译测试
- [ ] 完整链路测试
- [ ] 独立部署测试

### 执行验证
- [ ] ./scripts/build-and-verify.sh 通过
- [ ] 单模块独立运行测试通过
- [ ] 完整链路E2E测试通过

---

## 9. 附录

### 9.1 相关文档
- [DISTRIBUTED_DEPLOYMENT.md](./DISTRIBUTED_DEPLOYMENT.md) - 分布式部署指南
- [CONFIGURATION_GUIDE.md](./CONFIGURATION_GUIDE.md) - 配置指南
- [BUILD_GUIDE.md](../BUILD_GUIDE.md) - 构建指南

### 9.2 参考资料
- [CMake Documentation](https://cmake.org/documentation/)
- [Docker Best Practices](https://docs.docker.com/develop/dev-best-practices/)
- [Remote Driving System Spec](../project_spec.md)

---

**文档版本**: v1.0
**创建日期**: 2026-02-28
**最后更新**: 2026-02-28
**维护者**: Remote-Driving Team
