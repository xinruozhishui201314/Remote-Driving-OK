# 项目目录结构

## Executive Summary

本文档描述远程驾驶系统的完整目录结构，基于 `project_spec.md` 和 M0 阶段要求。

---

## 根目录结构

```
Remote-Driving/
├── backend/                    # C++ 后端服务（待实现）
├── Vehicle-side/              # C++ 车端代理
│   ├── build.sh
│   ├── CMakeLists.txt
│   ├── src/
│   └── README.md
├── client/                    # Qt C++ 客户端
│   ├── build.sh
│   ├── CMakeLists.txt
│   ├── qml/                   # QML UI 文件
│   ├── src/                   # C++ 源代码
│   └── resources/
├── media/                     # ZLMediaKit 配置与 Dockerfile
│   └── ZLMediaKit/
├── deploy/                    # 部署配置 ✅ M0
│   ├── docker-compose.yml     # Docker Compose 编排
│   ├── .env.example          # 环境变量示例
│   ├── README.md             # 部署说明
│   ├── keycloak/
│   │   ├── realm-export.json  # Keycloak Realm 配置
│   │   └── import-realm.sh   # Realm 导入脚本
│   ├── postgres/
│   │   └── init.sql          # PostgreSQL 初始化脚本
│   ├── zlm/
│   │   └── config.ini        # ZLMediaKit WebRTC 配置
│   └── coturn/
│       └── turnserver.conf   # Coturn STUN/TURN 配置
├── docs/                      # 文档 ✅ M0
│   └── M0_DEPLOYMENT.md      # M0 阶段部署文档
├── scripts/                   # 脚本 ✅ M0
│   ├── setup.sh              # 一键部署脚本
│   ├── check.sh              # 检查脚本（待实现）
│   ├── dev.sh                # 开发脚本（待实现）
│   └── e2e.sh                # 端到端测试脚本（待实现）
├── project_spec.md           # 项目规格说明
├── .cursorrules              # Cursor 开发规则
├── docker-compose.yml        # 根目录 Docker Compose（M0）
└── PROJECT_STRUCTURE.md      # 本文档
```

---

## M0 阶段已创建文件

### 1. Docker Compose 配置

**`docker-compose.yml`** (根目录)
- PostgreSQL 服务
- Keycloak 服务（自动导入 Realm）
- ZLMediaKit 服务（WebRTC 启用）
- Coturn 服务（STUN/TURN）
- Client Dev 服务（Qt6 开发环境）

### 2. Keycloak 配置

**`deploy/keycloak/realm-export.json`**
- Realm: `teleop`
- 角色: admin, owner, operator, observer, maintenance
- 客户端: teleop-backend, teleop-client

**`deploy/keycloak/import-realm.sh`**
- 自动导入 Realm 配置脚本
- 支持手动执行或通过 API 导入

### 3. PostgreSQL 配置

**`deploy/postgres/init.sql`**
- 创建 Keycloak 数据库和用户
- 预留业务数据库初始化接口

### 4. ZLMediaKit 配置

**`deploy/zlm/config.ini`**
- WebRTC 启用（端口 3000/3001/8000）
- Hook 鉴权配置
- 低延迟优化
- MP4 录制启用

### 5. Coturn 配置

**`deploy/coturn/turnserver.conf`**
- STUN/TURN 服务（端口 3478）
- 中继端口范围（49152-65535）
- 认证配置

### 6. 环境变量

**`deploy/.env.example`**
- 所有服务的环境变量模板
- 包含默认密码和配置说明

### 7. 文档

**`deploy/README.md`**
- 快速开始指南
- 服务说明
- 配置说明
- 故障排查

**`docs/M0_DEPLOYMENT.md`**
- 完整的部署文档
- 架构设计
- 配置详解
- 生产环境注意事项

### 8. 脚本

**`scripts/setup.sh`**
- 一键部署脚本
- 自动检查依赖
- 服务启动和验证

---

## 服务端口映射

| 服务 | 容器端口 | 主机端口 | 协议 | 说明 |
|------|----------|----------|------|------|
| PostgreSQL | 5432 | 5432 | TCP | 数据库 |
| Keycloak | 8080 | 8080 | HTTP | 身份认证 |
| ZLMediaKit HTTP | 80 | 80 | HTTP | Web 服务 |
| ZLMediaKit HTTPS | 443 | 443 | HTTPS | Web 服务（TLS） |
| ZLMediaKit RTMP | 1935 | 1935 | TCP | RTMP 推拉流 |
| ZLMediaKit RTSP | 554 | 554 | TCP | RTSP 推拉流 |
| ZLMediaKit WebRTC Signaling | 3000 | 3000 | HTTP | WebRTC 信令 |
| ZLMediaKit WebRTC Signaling SSL | 3001 | 3001 | HTTPS | WebRTC 信令（TLS） |
| ZLMediaKit WebRTC RTC | 8000 | 8000 | UDP/TCP | WebRTC 媒体传输 |
| Coturn STUN/TURN | 3478 | 3478 | UDP/TCP | NAT 穿透 |
| Coturn Relay | 49152-65535 | 49152-65535 | UDP | TURN 中继端口 |

---

## Docker 网络

所有服务运行在 `teleop-network` 网络中，可以通过服务名互相访问：

- `postgres` → PostgreSQL
- `keycloak` → Keycloak
- `zlmediakit` → ZLMediaKit
- `coturn` → Coturn
- `backend` → 后端服务（待实现）

---

## Docker 卷

| 卷名 | 用途 | 数据持久化 |
|------|------|------------|
| `postgres_data` | PostgreSQL 数据 | ✅ |
| `keycloak_data` | Keycloak 数据 | ✅ |
| `zlm_recordings` | ZLMediaKit 录制文件 | ✅ |
| `zlm_snapshots` | ZLMediaKit 截图 | ✅ |
| `client_build` | 客户端构建缓存 | ⚠️ 开发环境 |

---

## 快速开始

### 1. 准备环境

```bash
cd deploy
cp .env.example .env
# 编辑 .env，修改密码
```

### 2. 启动服务

```bash
# 方式1: 使用脚本
cd ../scripts
./setup.sh

# 方式2: 手动启动
cd ../deploy
docker-compose up -d
```

### 3. 验证服务

```bash
# 检查所有服务状态
docker-compose ps

# 查看日志
docker-compose logs -f

# 访问 Keycloak
open http://localhost:8080/admin
```

---

## 下一步（M1 阶段）

1. **后端服务**
   - 创建 `backend/` 目录结构
   - 实现 REST API
   - 集成 Keycloak JWT 验证
   - 实现 ZLMediaKit Hook 接口

2. **数据库迁移**
   - 创建表结构（accounts, users, vehicles, vin_grants, sessions 等）
   - 初始化数据

3. **客户端集成**
   - 集成 Keycloak OIDC 登录
   - WebRTC 推拉流测试
   - 控制通道测试

4. **端到端测试**
   - 登录流程测试
   - VIN 授权测试
   - 推拉流测试
   - 控制指令测试

---

## 参考

- [project_spec.md](./project_spec.md) - 项目规格说明
- [deploy/README.md](./deploy/README.md) - 部署说明
- [docs/M0_DEPLOYMENT.md](./docs/M0_DEPLOYMENT.md) - M0 部署文档
