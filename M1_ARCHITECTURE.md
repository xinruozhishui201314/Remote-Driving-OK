# M1 阶段架构设计

## 1. 后端核心模块

### 1.1 认证授权模块（auth/）
**目的**: JWT 验证和 VIN 权限检查

#### 文件
- `auth/jwt_validator.h` - JWT 验证器
- `auth/jwt_validator.cpp` - JWT 验证实现
- `auth/permission_check.h` - 权限检查器
- `auth/permission_check.cpp` - 权限检查实现

**关键功能**:
- 验证 Keycloak JWT token
- 检查 VIN 绑定状态
- 检查 VIN 权限（vin.view, vin.control, vin.maintain）
- 检查用户角色权限（admin, owner, operator, observer, maintenance）

---

### 1.2 会话管理模块（session/）
**目的**: 管理会话生命周期、状态机、锁机制

#### 文件
- `session/teleop_state.h` - 会话状态机
- `session/teleop_state.cpp` - 状态机实现
- `session/session_manager.h` - 会话管理器
- `session/session_manager.cpp` - 管理会话创建、查询、结束
- `session/session_store.h` - PostgreSQL 会话存储
- `session/session_store.cpp` - 会话数据持久化

**关键功能**:
- 会话创建：检查权限 + 检查 VIN 在线 + 检查无控制者
- 会话状态机：IDLE → ARMED → ACTIVE → SAFE_STOP → STOPPED → IDLE
- 状态机切换：事件触发
- 控制锁机制：单一 VIN 单控制器
- 会话密钥：生成和验证 sessionSecret

### 1.3 Hook 接口模块（hook/）
**目的**: 与 ZLMediaKit 集成，控制推拉流鉴权

#### 文件
- `hook/auth_handler.h` - ZLMediaKit Hook 处理器
- `hook/auth_handler.cpp` - Hook 接口实现
- `hook/server_started.h` - 服务器启动回调
- `hook/server_started.cpp` - 启动回调实现

**关键功能**:
- `on_play` - 播放鉴权
- `on_publish` - 推流鉴权
- `on_stream_changed` - 流状态变更通知
- `on_record_*` - 录制完成回调

### 1.4 API 控制器（api/）
**目的**: 后端 REST API 实现

#### 文件
- `api/vin_controller.h` - VIN API 控制器
- `api/vin_controller.cpp` - VIN API 实现
- `api/session_controller.h` - Session API 控制器
- `api/session_controller.cpp` - Session API 实现
- `api/fault_controller.h` - 故障 API 控制器
- `api/fault_controller.cpp` - 故障 API 实现

**关键功能**:
- GET /api/v1/me
- GET /api/v1/vins （列表当前用户的VIN）
- POST /api/v1/vins/{vin}/sessions （创建会话）
- POST /api/v1/sessions/{sessionId}/lock （获取控制锁）
- POST /api/v1/sessions/{sessionId}/unlock （释放控制锁）
- GET /api/v1/sessions/{sessionId}/streams （获取流地址）
- GET /api/v1/vins/{vin}/faults （查询故障）
- POST /api/v1/faults/{faultId}/ack （确认故障）

### 1.5 遥测数据处理模块（telemetry/）
**目的**: 收集、存储、分析遥测数据

#### 文件
- `telemetry/telemetry_data.h` - 遥测数据结构
- `telemetry/telemetry_collector.h` - 遥测收集器
- `telemetry/telemetry_collector.cpp` - 收集器实现
- `telemetry/telemetry_uploader.h` - 遥测数据处理
- `telemetry/telemetry_uploader.cpp` - 遥测数据上传

**关键功能**:
- 10-20Hz 遥测频率
- 数据收集和缓存（最近10分钟或1000条）
- 统计信息生成
- 故障触发检查
- 历史数据查询

---

## 2. 车端代理核心模块（Vehicle-side/）

### 2.1 安全状态机模块
**目的**: 管理车端状态机、安全停车逻辑

#### 文件
- `session/teleop_state.h` - 状态机（已创建）
- `session/teleop_state.cpp` - 状态机实现
- `session/safety_state_machine.h` - 安全状态机（待创建）
- `session/safety_state_machine.cpp` - 安全状态机实现（待创建）

### 2.2 控制指令验证模块
**目的**: 验证控制指令的有效性和安全性

#### 文件
- `control/command_validator.h` - 验证器接口
- `control/command_validator.cpp` - 验证器实现（待创建）

**关键功能**:
- 参数范围检查（油门0~1.0，方向盘-1~1，档位-10~10）
- 边界检查（突变检查）
- 频率超限检查
- Deadman 开关检查

### 2.3 SessionSecret 管理
**目的**: 生成和验证会话密钥

#### 文件
- `security/session_secret.h` – 会话密钥生成器（待创建）
- `security/session_secret.cpp` – 会话密钥生成实现（待创建）

### 2.4 Watchdog 模块
**目的**: 监控会话健康状态，触发安全停车

#### 文件
- `watchdog/watchdog.h` - 看狗管理（待创建）
- `watchdog/watchdog.cpp` - 看狗实现（待创建）

**关键功能**:
- 500ms 超时监控
- 无心跳自动触发安全停车
- 重连机制
- 超时配置可调

---

## 3. 客户端核心模块（client/）

### 3.1 OIDC 客户端模块
**Purpose**: 集成 Keycloak OIDC 登录

#### 文件
- `auth/oidc_client.h` - 客户端（待创建）
- `auth/oidc_client.cpp` - 客户端实现（待创建）
- `auth/auth_manager.cpp` - 认证管理器（已存在，需测试）

**关键功能**:
- OIDC Authorization Code + PKCE 登录
- Token 刷新机制
- 角色登出功能

### 3.2 会话管理模块
**Purpose**: 客户端会话管理和 UI 状态机

#### 文件
- `session/teleop_session.h` - 会话管理客户端接口
- `session/teleop_session.cpp` - 会话管理实现（待创建）
- `session/teleop_session_state.h` - 客户端状态机（待创建）

**关键功能**:
- 创建控制会话
- 会话状态切换显示
- 超时信息显示

### 3.3 视频播放模块
**Purpose**: 客户端 WebRTC 拉流器

#### 文件
- `video/webrtc_player.h` - VideoRTC 播放器（待创建）
- `video/webrtc_player.cpp` - 播放器实现（待创建）
- `media/video/stream_provider.h - 流提供者（待创建）

**关键功能**:
- WebRTC 连接建立
- 视频解码显示
- 性能指标显示

### 3.4 控制输入模块
**Purpose**: 键盘、方向盘、踏板、手柄输入映射

#### 文件
- `control/input_mapper.h` - 输入映射接口（待创建）
- `control/input_mapper.cpp` - 输入映射实现（待创建）

**关键功能**:
- 键盘映射（WASD、方向键）
- 方向盘映射
- 油门/踏板模拟
- 手柄按钮映射

### 3.5 遥测展示模块
**Purpose**: 遥测数据实时显示

#### 文件
- `telemetry/telemetry_display.h` - 遥测展示接口（待创建）
- `telemetry/telemetry_display.cpp` - 遥测展示实现（待创建）
- `telemetry/telemetry_charts/telemetry_data_series.json` - Grafana 面板配置（待创建）

**关键功能**:
- 实时数据显示
- 历史数据查询
- 阈列图表显示

---

## 4. 依赖关系

```mermaid
graph TB
    subgraph Backend["后端 C++"]
        MAIN[主程序]
    end
    
    subgraph auth["认证模块"]
        JWT[JWT 验证]
        AUTH权限检查[Permission检查]
        VIN_DB[VIN数据库:PostgreSQL]
    end
    
    subgraph session["会话管理模块"]
        SSM[状态机]
        SessionStore[会话存储:PostgreSQL]
    end
    
    subgraph telemetry["遥测数据处理"]
        Collector[收集器]-->|缓存|
        Processor[处理器]-->|数据库|
        Charts[图表:Grafana]
    end
    
    subgraph client["客户端 Qt/QML"]
        OIDC[OIDC客户端]
        Video[WebRTC播放器]
        Input[控制输入]
        Display[遥测展示]
    end
    
    subgraph vehicle["车端代理"]
        STATE[状态机]
        Validator[指令断验证]
        SECRET[Session会话密钥]
        WATCHDOG[看门狗模块]
    end
    
    subgraph security["安全与安全"]
        MESSAGE[消息签名]
        ANTI_REPLAY[防重放]
        PERMISSIONS[权限检查]
        WATCHDOG_TIMEOUT[看门狗超时]
    end
    
    JWT --|--> AUTH
    VIN_DB --| PERMISSIONS
    PERMISSIONS --| MESSAGE
    
    VIN_DB --| SECRET
    SECRET --| MESSAGE
    SECRET --| ANTI_REPLAY
    
    WATCHDOG_TIMEOUT --| STATE_MACHINE
    STATE_MACHINE --| SAFE_STOP
    
    MESSAGE --| SEVERITY_ERROR
    STATE_MACHINE --| FAULT_LOCKED
    SEVERITY_ERROR --| AUDIT_LOG
    AUDIT_LOG -->| AUDIT_DB
    
    ZLM[|"发布验证]-->|MESSAGE
    ZLM[|<"拉流验证]-->|MESSAGE
end

    AUTH -->|JWT
    PERMISSIONS -->|JWT
```

---

## 5. 技术栈和依赖

### 5.1 后端 C++

#### 核心依赖
- C++17
- PostgreSQL C++ (libpq++)
- OpenSSL 1.1+ (libssl, libcrypto)
- nlohmann_json (JSON 处理)
- httplib (HTTP REST API)
- spdlog (结构化日志)
- Google Test (测试框架)
- openssl/development (C++ 密码)
- catch2 (测试框架)

#### 确件构建
- CMake 3.20+
- Make

### 5.2 车端代理 C++
- C++17
- WebRTC客户端库（自研或libwebrtc）
- 网络通信

### 5.3 客户端 Qt6
- Qt6.8 (gcc + aqt 工具链)
- Qt WebRTC 组件（QWebRTC, QQuickWidget）

### 5.4 开发工具
- Docker + Docker Compose
- Git + Git
- CMake + Make
- GDB
- Valgrind
- clang-format / clangd

---

## 6. 文件统计

### 6.1 后端文件

```
backend/
├── include/
│   ├── protocol/          # 消息协议（3个h文件）
│   ├── auth/             # 认证授权（2个h文件）
│   ├── session/          # 会话管理（3个h文件）
│   ├── api/              # API 控制器（6个h文件）
│   ├── db/               # 数据库访问（2个h文件）
│   ├── hook/              # Hook接口（2个h文件）
│   ├── telemetry/            # 遥测数据处理（3个h文件）
│   ├── tests/             # 单元测试（待创建）
│   └── tests/
├── CMakeLists.txt
├── Dockerfile
└── README.md
```

### 6.2 车端代理文件
```
Vehicle-side/
├── include/
│   └── protocol/
├── session/
│   ├── teleop_state.h
├── CMakeLists.txt
├── dockerfile
└── README.md
```

### 6.3 客户端文件
```
client/
├── auth/            # 认证模块（1个文件）
├── control/            # 控制输入（待创建）
├── video/            # 视频播放（待创建）
├── telemetry/          # 遥测显示（待创建）
├── qml/              # UI 界面（已有）
├── src/              # C++ 源代码
├── CMakeLists.txt
└── README.md
```

---

## 7. 开发流程

### 7.1 开发环境设置
- Docker Compose for services
- Postgres:15 with migrations
- Keycloak: 24.0 with realm config
- ZLMediaKit: master with WebRTC
- GraphiteDB: PostgreSQL 推荐用于历史遥测

### 7.2 构建命令

```bash
cd backend
mkdir -p build && cd build
cmake ..
make

# 运行测试
make test

# 调试
gdb backend/tests/test
```

### 7.3 测试命令

```bash
# 单元测试
make test

# 集成测试
make test-integration

# E2E 测试
bash scripts/e2e.sh

# 检查
bash scripts/check.sh
```

---

## 8. 部署步骤

### 8.1 开发模式启动
```bash
docker-compose -f docker-compose.yml up -d postgres keycloak zlmediakit coturn
cd scripts
bash dev.sh
```

### 8.2 生产环境部署
```bash
docker-compose -f docker-compose.yml up -d postgres keycloak zlmediakit coturn
```

---

## 9. 风险注意事项

### 9.1 安全风险
- 会话密钥轮换：周期性更换 session secret，保证安全
- CORS 配置：只允许可信源域名
- SQL 注入：使用参数化查询，过滤用户输入

### 9.2 性能风险
- WebRTC 数据量影响时延：动态调整码率和分辨率
- PostgreSQL 连接池大小：根据应用需求调整
- 查询复杂度：优化慢查询和关联查询

### 9.3 数据风险
- 数据丢失：定期备份
- 数据迁移失败：先在测试环境验证
- 并发冲突：使用乐观锁

---

## 10. 下一步操作

1. 检查是否需要生成任何配置脚本
2. 是否需要更新 Docker 镜像版本
3. 是否需要预先创建数据库表
4. 是否需要创建 Docker Volumes

请在开始 M1 阶段开发前确认以上事项。
