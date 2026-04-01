# ZLMediaKit DataChannel 能力分析

## Executive Summary

**结论**：ZLMediaKit **具有 WebRTC DataChannel 能力**，但**需要额外开发转发逻辑**才能实现"车端→ZLMediaKit→客户端"的底盘数据转发。

**当前状态**：
- ✅ 支持 WebRTC DataChannel (SCTP)
- ✅ 支持发送和接收 DataChannel 消息
- ✅ 支持 Hook 机制监听 DataChannel 消息
- ⚠️ **不支持**自动转发（从一个WebRTC会话转发到另一个）
- ⚠️ 当前主要是点对点通信

---

## 1. ZLMediaKit DataChannel 能力

### 1.1 核心功能

#### ✅ 发送 DataChannel 消息

**API**：`mk_rtc_send_datachannel`
```cpp
API_EXPORT void API_CALL mk_rtc_send_datachannel(
    const mk_rtc_transport ctx, 
    uint16_t streamId, 
    uint32_t ppid, 
    const char *msg, 
    size_t len
);
```

**实现位置**：`media/ZLMediaKit/webrtc/WebRtcTransport.cpp:571`
```cpp
void WebRtcTransport::sendDatachannel(uint16_t streamId, uint32_t ppid, const char *msg, size_t len) {
    if (_sctp) {
        RTC::SctpStreamParameters params;
        params.streamId = streamId;
        _sctp->SendSctpMessage(params, ppid, (uint8_t *)msg, len);
    }
}
```

#### ✅ 接收 DataChannel 消息

**回调**：`OnSctpAssociationMessageReceived`
```cpp
void WebRtcTransport::OnSctpAssociationMessageReceived(
    RTC::SctpAssociation *sctpAssociation, 
    uint16_t streamId, 
    uint32_t ppid, 
    const uint8_t *msg, 
    size_t len
) {
    // 触发Hook事件
    NOTICE_EMIT(BroadcastRtcSctpReceivedArgs, Broadcast::kBroadcastRtcSctpReceived, 
                *this, streamId, ppid, msg, len);
}
```

#### ✅ Hook 机制

**事件名称**：`kBroadcastRtcSctpReceived`

**Hook注册**：`media/ZLMediaKit/api/source/mk_events.cpp:211`
```cpp
NoticeCenter::Instance().addListener(&s_tag, Broadcast::kBroadcastRtcSctpReceived,
    [](BroadcastRtcSctpReceivedArgs){
        // 可以在这里处理接收到的DataChannel消息
    });
```

### 1.2 支持的数据类型

- **PPID 51**：文本字符串（Text string）
- **PPID 53**：二进制数据（Binary）
- **PPID 50**：WebRTC DataChannel Control（被忽略）

### 1.3 配置选项

**配置文件**：`deploy/zlm/config.ini`
```ini
# WebRTC datachannel是否回显数据，测试用（远程驾驶控制通道）
datachannel_echo=0
```

**回显模式**：当 `datachannel_echo=1` 时，接收到的消息会回显给发送者（用于测试）。

---

## 2. 当前限制

### 2.1 不支持自动转发

**问题**：ZLMediaKit 当前**不支持**从一个 WebRTC 会话自动转发 DataChannel 消息到另一个 WebRTC 会话。

**原因**：
- DataChannel 消息处理是**点对点**的
- 没有内置的"会话间转发"机制
- Hook 机制只能监听，不能自动转发

### 2.2 需要额外开发

要实现"车端→ZLMediaKit→客户端"的转发，需要：

1. **Hook 处理**：监听车端发送的 DataChannel 消息
2. **会话管理**：维护车端和客户端的 WebRTC 会话映射
3. **转发逻辑**：将消息从车端会话转发到客户端会话

---

## 3. 实现方案

### 方案A：通过 Hook + 后端服务转发（推荐）

```
车端 (WebRTC Push)
    ↓ (DataChannel发送底盘数据)
ZLMediaKit (接收，触发Hook)
    ↓ (Hook回调到后端服务)
后端服务 (转发逻辑)
    ↓ (通过API发送到客户端会话)
ZLMediaKit (发送DataChannel到客户端)
    ↓ (DataChannel接收)
客户端 (WebRTC Play)
```

**实现步骤**：

1. **配置 Hook**：
```ini
# config.ini
[hook]
on_rtc_sctp_received=http://backend:8080/api/hook/rtc_sctp_received
```

2. **后端服务处理**：
```python
# 接收Hook回调
@app.post("/api/hook/rtc_sctp_received")
def handle_rtc_sctp_received(request):
    # 解析消息
    session_id = request.json["id"]
    stream_id = request.json["streamId"]
    ppid = request.json["ppid"]
    msg = request.json["msg"]
    
    # 判断是车端还是客户端
    if is_vehicle_session(session_id):
        # 查找对应的客户端会话
        client_session = find_client_session(session_id)
        if client_session:
            # 转发到客户端
            zlm_api.send_datachannel(client_session, stream_id, ppid, msg)
```

3. **ZLMediaKit API调用**（需要通过C API或Hook实现）：
   - ZLMediaKit 提供 C API：`mk_rtc_send_datachannel`
   - 需要通过后端服务调用C API，或通过Hook机制实现转发
   - **注意**：当前版本可能没有直接的HTTP API接口发送DataChannel消息

### 方案B：修改 ZLMediaKit 源码（不推荐）

直接在 ZLMediaKit 内部实现转发逻辑，但需要：
- 修改源码
- 维护会话映射
- 增加复杂度

### 方案C：继续使用 MQTT（当前方案）

**优势**：
- ✅ 简单可靠
- ✅ 不依赖视频流连接
- ✅ 已有完整实现
- ✅ 易于调试和监控

**劣势**：
- ⚠️ 需要额外的 MQTT Broker
- ⚠️ 与视频流分离

---

## 4. 验证 ZLMediaKit DataChannel 能力

### 4.1 测试回显功能

```bash
# 1. 启用回显
# 修改 config.ini: datachannel_echo=1

# 2. 使用WebRTC测试页面
# 访问: http://localhost:8080/index/webrtc/index.html

# 3. 创建DataChannel并发送消息
# 应该能看到消息被回显
```

### 4.2 测试 Hook 机制

```bash
# 1. 配置Hook
# config.ini
[hook]
on_rtc_sctp_received=http://your-backend:8080/hook/rtc_sctp_received

# 2. 启动测试后端服务
# 接收Hook回调并打印消息

# 3. 通过WebRTC发送DataChannel消息
# 后端应该收到Hook回调
```

### 4.3 测试 API 发送

**注意**：ZLMediaKit 当前可能没有直接的 HTTP API 接口发送 DataChannel 消息。

**替代方案**：
1. **通过 C API**：需要编译后端服务调用 `mk_rtc_send_datachannel`
2. **通过 Hook**：在 Hook 服务中实现转发逻辑
3. **使用 WebRTC 测试页面**：`http://localhost:8080/index/webrtc/index.html` 可以直接测试 DataChannel

---

## 5. 推荐方案对比

| 方案 | 复杂度 | 可靠性 | 延迟 | 推荐度 |
|------|--------|--------|------|--------|
| **MQTT直接传输**（当前） | 低 | 高 | 低 | ⭐⭐⭐⭐⭐ |
| Hook + 后端转发 | 中 | 中 | 中 | ⭐⭐⭐ |
| 修改ZLMediaKit源码 | 高 | 中 | 低 | ⭐⭐ |

### 推荐：继续使用 MQTT

**理由**：
1. **简单可靠**：MQTT是成熟的消息队列协议
2. **独立于视频流**：即使视频流断开，数据流仍可用
3. **易于调试**：可以使用标准MQTT工具监控
4. **已有实现**：当前实现已经完整且稳定

**如果需要统一通道**：
- 可以考虑在**未来版本**中实现Hook转发
- 但当前MQTT方案已经满足需求

---

## 6. 如果必须通过ZLMediaKit转发

### 6.1 实现架构

```
┌─────────────┐         ┌──────────────┐         ┌─────────────┐
│   车端      │         │  ZLMediaKit  │         │   客户端    │
│ (WebRTC Push)│────────▶│              │────────▶│(WebRTC Play)│
└─────────────┘         └──────┬───────┘         └─────────────┘
                               │
                               │ Hook回调
                               ▼
                        ┌──────────────┐
                        │   后端服务   │
                        │ (转发逻辑)   │
                        └──────────────┘
```

### 6.2 实现步骤

1. **车端修改**：通过WebRTC DataChannel发送底盘数据
2. **Hook服务**：接收Hook回调，解析消息
3. **会话管理**：维护车端-客户端会话映射
4. **转发逻辑**：调用ZLMediaKit API转发消息
5. **客户端修改**：通过WebRTC DataChannel接收数据

### 6.3 代码示例

**Hook服务（Python示例）**：
```python
from flask import Flask, request, jsonify
import requests

app = Flask(__name__)

# 会话映射：{vehicle_session_id: client_session_id}
session_map = {}

@app.route('/hook/rtc_sctp_received', methods=['POST'])
def handle_rtc_sctp_received():
    data = request.json
    session_id = data['id']
    msg = data['msg']
    
    # 判断是车端还是客户端
    if session_id.startswith('vehicle_'):
        # 查找对应的客户端会话
        client_session = session_map.get(session_id)
        if client_session:
            # 转发到客户端
            forward_to_client(client_session, msg)
    
    return jsonify({'code': 0})

def forward_to_client(client_session_id, msg):
    # 方案1：通过C API（需要编译后端服务）
    # 调用 mk_rtc_send_datachannel C API
    
    # 方案2：通过Hook机制（推荐）
    # 在Hook服务中维护会话映射，直接调用C API
    
    # 方案3：如果ZLMediaKit有HTTP API（需要确认版本）
    # requests.post('http://zlm:8080/index/api/sendRtcDataChannel', data={
    #     'secret': 'your_secret',
    #     'id': client_session_id,
    #     'streamId': 0,
    #     'ppid': 51,
    #     'data': msg
    # })
    
    # 当前推荐：继续使用MQTT，或通过Hook+C API实现
    pass
```

---

## 7. 结论

### ZLMediaKit 具有 DataChannel 能力

- ✅ 支持发送和接收 DataChannel 消息
- ✅ 支持 Hook 机制监听消息
- ✅ 支持通过 API 发送消息

### 但不支持自动转发

- ⚠️ 需要额外开发转发逻辑
- ⚠️ 需要维护会话映射
- ⚠️ 需要后端服务支持

### 推荐方案

**当前**：继续使用 **MQTT 直接传输**（简单、可靠、已有实现）

**未来**：如果需要统一通道，可以实现 Hook + 后端转发方案

---

## 8. 相关文档

- `docs/VERIFY_CHASSIS_DATA_FLOW.md` - 数据流验证指南
- `docs/M1_GATE_X_CONTROL_VIA_ZLM.md` - 控制通道设计
- `Vehicle-side/src/zlm_control_channel.h` - ZLM控制通道占位类
