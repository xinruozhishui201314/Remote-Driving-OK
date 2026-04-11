# WebRTC 和 MQTT 集成说明

## Executive Summary

已完整集成 WebRTC（libdatachannel）和 MQTT（Paho MQTT C++）库，并添加了登录认证和车辆选择功能。所有代码遵循 Qt6 现代语法，可在 Dev Container 中编译运行。

---

## 1. 依赖库安装

### 1.1 libdatachannel（WebRTC）

```bash
# 克隆仓库
git clone https://github.com/paullouisageneau/libdatachannel.git
cd libdatachannel

# 编译安装
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
sudo cmake --install build
```

### 1.2 Paho MQTT C++

```bash
# 克隆仓库
git clone https://github.com/eclipse/paho.mqtt.cpp.git
cd paho.mqtt.cpp

# 编译安装
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
sudo cmake --install build
```

### 1.3 在 Dev Container 中安装

```bash
# 在容器内执行
cd /workspace
git clone https://github.com/paullouisageneau/libdatachannel.git
cd libdatachannel
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
sudo cmake --install build

cd ..
git clone https://github.com/eclipse/paho.mqtt.cpp.git
cd paho.mqtt.cpp
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
sudo cmake --install build
```

---

## 2. 编译配置

### 2.1 CMake 配置

项目已配置为自动检测库：

```cmake
find_package(libdatachannel QUIET)
find_package(PahoMqttCpp QUIET)
```

如果库未找到，会显示警告但不会阻止编译（使用模拟实现）。

### 2.2 编译命令

```bash
cd client
mkdir -p build && cd build
cmake .. \
  -DCMAKE_PREFIX_PATH=/opt/Qt/6.8.0/gcc_64 \
  -DCMAKE_BUILD_TYPE=Debug
make -j$(nproc)
```

---

## 3. 功能说明

### 3.1 登录认证

**流程：**
1. 启动应用 → 显示登录对话框
2. 输入服务器地址、用户名、密码
3. 点击登录 → 调用 `/api/auth/login` API
4. 登录成功 → 保存 token，打开车辆选择对话框

**API 格式：**
```json
POST /api/auth/login
{
  "username": "user123",
  "password": "password123"
}

Response:
{
  "code": 0,
  "token": "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9...",
  "user": {
    "username": "user123",
    "role": "operator"
  }
}
```

### 3.2 车辆选择

**流程：**
1. 登录成功后自动加载车辆列表
2. 显示所有可用车辆（VIN、名称、状态）
3. 选择车辆 → 设置当前 VIN
4. 确认 → 打开连接对话框

**API 格式：**
```json
GET /api/vehicles
Headers: Authorization: Bearer <token>

Response:
{
  "code": 0,
  "data": [
    {
      "vin": "LSGBF53M8DS123456",
      "name": "测试车辆1",
      "model": "Model X",
      "status": "online",
      "metadata": {}
    }
  ]
}
```

### 3.3 WebRTC 视频流

**实现：**
- 使用 libdatachannel 创建 PeerConnection
- 生成 SDP Offer 并发送到 ZLMediaKit
- 处理 SDP Answer 并建立连接
- 接收视频轨道并渲染

**URL 格式：**
```
http://<server>/index/api/webrtc?app=<app>&stream=<vin>&type=play
```

### 3.4 MQTT 控制指令

**实现：**
- 使用 Paho MQTT C++ 连接 Broker
- 自动在控制指令中添加 VIN
- 订阅车辆状态主题

**控制指令格式：**
```json
{
  "vin": "LSGBF53M8DS123456",
  "type": "drive",
  "steering": 0.5,
  "throttle": 0.3,
  "brake": 0.0,
  "gear": 1,
  "timestamp": 1234567890
}
```

**MQTT 主题：**
- 控制：`vehicle/control`（所有车辆）或 `vehicle/<vin>/control`（特定车辆）
- 状态：`vehicle/status`（所有车辆）或 `vehicle/<vin>/status`（特定车辆）

---

## 4. UI 流程

```
启动应用
    ↓
登录对话框（LoginDialog）
    ↓
输入用户名/密码 → 登录
    ↓
车辆选择对话框（VehicleSelectionDialog）
    ↓
选择车辆 VIN → 确认
    ↓
连接对话框（ConnectionsDialog）
    ↓
配置 WebRTC/MQTT → 连接
    ↓
主界面（视频 + 控制面板）
```

---

## 5. 代码结构

### 新增文件

| 文件 | 说明 |
|------|------|
| `src/authmanager.h/cpp` | 登录认证管理 |
| `src/vehiclemanager.h/cpp` + `src/services/vehiclecatalogclient.*` + `src/services/remotesessionclient.*` + `src/services/vehicle_api_parsers.*` | 车辆列表、HTTP 建会话、VIN 状态（列表/会话 HTTP 子模块 + 可测解析） |
| `qml/LoginDialog.qml` | 登录界面 |
| `qml/VehicleSelectionDialog.qml` | 车辆选择界面 |

### 更新的文件

| 文件 | 更新内容 |
|------|---------|
| `src/webrtcclient.h/cpp` | 集成 libdatachannel |
| `src/mqttcontroller.h/cpp` | 集成 Paho MQTT C++，添加 VIN 支持 |
| `src/main.cpp` | 注册新类型，连接信号 |
| `qml/main.qml` | 添加登录和车辆选择流程 |
| `CMakeLists.txt` | 添加库依赖检测 |

---

## 6. 配置示例

### 6.1 服务器配置

**认证服务器：**
- URL: `http://192.168.1.100:8080`
- 登录 API: `POST /api/auth/login`
- 车辆列表 API: `GET /api/vehicles`

**媒体服务器（ZLMediaKit）：**
- URL: `http://192.168.1.100:8080`
- WebRTC API: `/index/api/webrtc`

**MQTT Broker：**
- URL: `mqtt://192.168.1.100:1883`
- 控制主题: `vehicle/control`
- 状态主题: `vehicle/status`

### 6.2 环境变量（可选）

```bash
export REMOTE_DRIVING_SERVER=http://192.168.1.100:8080
export MQTT_BROKER=mqtt://192.168.1.100:1883
```

---

## 7. 测试

### 7.1 单元测试

```bash
# 测试认证
./test_authmanager

# 测试车辆管理
./test_vehiclemanager
```

### 7.2 集成测试

1. 启动认证服务器
2. 启动 ZLMediaKit
3. 启动 MQTT Broker
4. 运行客户端
5. 测试完整流程

### 7.3 端到端测试

1. 登录 → 验证 token 保存
2. 选择车辆 → 验证 VIN 设置
3. 连接视频 → 验证 WebRTC 连接
4. 发送控制指令 → 验证 MQTT 消息包含 VIN
5. 接收状态 → 验证状态更新

---

## 8. 故障排查

### Q1: libdatachannel 未找到

**错误**: `Could not find a package configuration file provided by "libdatachannel"`

**解决**:
```bash
# 安装 libdatachannel
git clone https://github.com/paullouisageneau/libdatachannel.git
cd libdatachannel
cmake -B build && cmake --build build && sudo cmake --install build
```

### Q2: Paho MQTT C++ 未找到

**错误**: `Could not find a package configuration file provided by "PahoMqttCpp"`

**解决**:
```bash
# 安装 Paho MQTT C++
git clone https://github.com/eclipse/paho.mqtt.cpp.git
cd paho.mqtt.cpp
cmake -B build && cmake --build build && sudo cmake --install build
```

### Q3: 登录失败

**检查**:
1. 服务器地址是否正确
2. API 端点是否正确（`/api/auth/login`）
3. 网络连接是否正常
4. 查看控制台错误信息

### Q4: 车辆列表为空

**检查**:
1. 是否已登录
2. Token 是否有效
3. API 端点是否正确（`/api/vehicles`）
4. 服务器是否有可用车辆

### Q5: WebRTC 连接失败

**检查**:
1. ZLMediaKit 是否运行
2. 流名称是否使用 VIN
3. 网络连接是否正常
4. 查看 WebRTC 日志

### Q6: MQTT 连接失败

**检查**:
1. MQTT Broker 是否运行
2. Broker 地址和端口是否正确
3. 网络连接是否正常
4. 防火墙设置

---

## 9. 安全考虑

### 9.1 认证安全

- ✅ Token 存储在本地（QSettings）
- ⚠️ 建议：Token 加密存储
- ⚠️ 建议：Token 自动刷新机制

### 9.2 通信安全

- ⚠️ 建议：MQTT over TLS/SSL
- ⚠️ 建议：WebRTC over DTLS
- ⚠️ 建议：控制指令签名验证

### 9.3 数据安全

- ✅ VIN 验证（确保控制正确的车辆）
- ⚠️ 建议：防止重放攻击
- ⚠️ 建议：敏感数据加密

---

## 10. 后续优化

### 高优先级

- [ ] Token 自动刷新
- [ ] 连接重试机制
- [ ] 错误处理和用户提示优化

### 中优先级

- [ ] 车辆状态缓存
- [ ] 离线模式支持
- [ ] 配置文件管理

### 低优先级

- [ ] 多账户支持
- [ ] 车辆收藏功能
- [ ] 历史记录查看

---

## 11. 参考资源

- [libdatachannel 文档](https://github.com/paullouisageneau/libdatachannel)
- [Paho MQTT C++ 文档](https://github.com/eclipse/paho.mqtt.cpp)
- [ZLMediaKit WebRTC API](https://github.com/ZLMediaKit/ZLMediaKit/wiki/WebRTC)
- [Qt6 QML 文档](https://doc.qt.io/qt-6/qtqml-index.html)
