# 5 Why 分析 - 可视化图表

## 问题: 客户端四个视图无法显示（100% 黑屏）

```
┌─────────────────────────────────────────────────────────────────────┐
│                           SYMPTOM (症状)                             │
│                    四个视频窗口全部黑屏无内容                          │
│                 codecOpen=false, emitted=0, wxh=0x0                 │
│                                                                       │
│  症状表现: 应用启动后立即无法显示任何视频, 持续无法恢复              │
│  影响范围: 四路同时失败 (cam_front, cam_rear, cam_left, cam_right)  │
│  严重级别: P0 (100% 功能失效)                                        │
└──────────────────────────┬──────────────────────────────────────────┘
                           │ Why 1?
                           ↓
┌─────────────────────────────────────────────────────────────────────┐
│                        WHY 1 (第1层)                                 │
│                    为什么视频无法显示?                               │
│                                                                       │
│ 直接原因:                                                            │
│   └─ 视频解码器初始化失败 (H264Decoder::onRtpFrameComplete)        │
│                                                                       │
│ 日志证据:                                                            │
│   └─ [CRIT] [H264][...][HW-REQUIRED] 硬解未激活                   │
│   └─ 重复 100+ 次, 所有四路同时发生                                 │
│                                                                       │
│ 代码路径:                                                            │
│   h264decoder.cpp line ~1500                                        │
│   ```                                                               │
│   if (!m_webrtcHw->tryOpen(...)) {                                 │
│     if (requireHardware && !hardwareAvailable) {                  │
│       qCritical("[HW-REQUIRED] 硬解未激活");                       │
│       return;  // ← 不创建解码器                                    │
│     }                                                              │
│   }                                                                │
│   ```                                                               │
└──────────────────────────┬──────────────────────────────────────────┘
                           │ Why 2?
                           ↓
┌─────────────────────────────────────────────────────────────────────┐
│                        WHY 2 (第2层)                                 │
│            为什么硬解打开失败且不能软解?                            │
│                                                                       │
│ 直接原因:                                                            │
│   ├─ 硬解初始化失败 (H264WebRtcHwBridge::tryOpen)                  │
│   └─ 配置强制硬解模式 (requireHardwareDecode=true)                 │
│                                                                       │
│ 日志证据:                                                            │
│   ├─ [DEBUG] VAAPI: vaInitialize failed -1                        │
│   ├─ [INFO] availableDecoders= QList("FFmpeg(CPU)")               │
│   ├─ [INFO] override from env:                                    │
│   │         "CLIENT_MEDIA_REQUIRE_HARDWARE_DECODE" = "1"         │
│   └─ media.require_hardware_decode=true                            │
│                                                                       │
│ 代码路径:                                                            │
│   core/configuration.cpp                                           │
│   ```                                                               │
│   if (qEnvironmentVariableIsSet("...REQUIRE_HARDWARE_DECODE")) {  │
│     m_requireHardwareDecode = (qgetenv(...) == "1");              │
│     // ← 被设置为 true                                              │
│   }                                                                │
│   ```                                                               │
│                                                                       │
│ 关键设置:                                                            │
│   ├─ 配置文件: require_hardware_decode = false (默认)              │
│   ├─ 环境变量: CLIENT_MEDIA_REQUIRE_HARDWARE_DECODE = 1           │
│   └─ 结果: true (环境变量优先级更高)                                │
└──────────────────────────┬──────────────────────────────────────────┘
                           │ Why 3?
                           ↓
┌─────────────────────────────────────────────────────────────────────┐
│                        WHY 3 (第3层)                                 │
│             为什么 VAAPI 硬解初始化失败?                            │
│                                                                       │
│ 直接原因:                                                            │
│   └─ DRM 设备不可用或驱动不匹配                                      │
│                                                                       │
│ 日志证据:                                                            │
│   └─ [DEBUG] [DecoderFactory] VAAPI: vaInitialize failed -1        │
│                                     on /dev/dri/renderD128        │
│                                                                       │
│ FFmpeg/libva 层:                                                     │
│   ```                                                               │
│   // libva 初始化                                                    │
│   VAStatus status = vaInitialize(dpy, &major, &minor);             │
│   if (status != VA_STATUS_SUCCESS) {                              │
│     return error;  // ← 失败返回                                     │
│   }                                                                │
│   ```                                                               │
│                                                                       │
│ 环境上下文:                                                          │
│   ├─ 应用运行在 Docker 容器中 (likelyContainer=true)               │
│   ├─ 设备路径: /dev/dri/renderD128                                │
│   ├─ 可能原因:                                                      │
│   │  ├─ 设备未被挂载到容器                                          │
│   │  ├─ 权限不足                                                    │
│   │  ├─ 宿主 GPU 驱动版本不兼容                                    │
│   │  └─ 容器 VAAPI 库版本不兼容                                    │
│   └─ 确认: availableDecoders = FFmpeg(CPU only)                   │
│            (系统只检测到软解)                                       │
└──────────────────────────┬──────────────────────────────────────────┘
                           │ Why 4?
                           ↓
┌─────────────────────────────────────────────────────────────────────┐
│                        WHY 4 (第4层)                                 │
│         为什么系统中没有可用的硬解?                                 │
│                                                                       │
│ 直接原因:                                                            │
│   └─ Docker 容器中未配置 GPU 设备映射                              │
│                                                                       │
│ 环境变量优先级问题:                                                  │
│   ├─ 配置文件: require_hardware_decode = false                    │
│   ├─ 但被环境变量覆盖: CLIENT_MEDIA_REQUIRE_HARDWARE_DECODE=1     │
│   └─ 结果: 期望硬解 but 硬解不存在                                  │
│                                                                       │
│ 容器配置问题:                                                        │
│   docker-compose.yml:                                              │
│   ```yaml                                                           │
│   services:                                                        │
│     client:                                                        │
│       environment:                                                 │
│         CLIENT_MEDIA_REQUIRE_HARDWARE_DECODE: "1"  # ← 要求硬解  │
│       # 但没有:                                                     │
│       # devices:                                                    │
│       #   - /dev/dri:/dev/dri  # ← 这行缺失                       │
│   ```                                                               │
│                                                                       │
│ 设计问题:                                                            │
│   └─ 配置强制硬解时, 无检查该环境是否真的有硬解                    │
└──────────────────────────┬──────────────────────────────────────────┘
                           │ Why 5? (ROOT CAUSE)
                           ↓
┌─────────────────────────────────────────────────────────────────────┐
│                    WHY 5 (根本原因 - ROOT CAUSE)                     │
│                                                                       │
│ ╔════════════════════════════════════════════════════════════════╗ │
│ ║  配置与实际环境能力的完全冲突                                    ║ │
│ ║                                                                  ║ │
│ ║  期望 (配置):          硬解必须可用                              ║ │
│ ║  现实 (运行环境):      硬解完全不可用                            ║ │
│ ║                                                                  ║ │
│ ║  导致:                 无法满足的约束 → 拒绝启动 → 黑屏         ║ │
│ ╚════════════════════════════════════════════════════════════════╝ │
│                                                                       │
│ 根本原因链:                                                          │
│   1. 环境变量设置为 "1" (强制硬解)                                  │
│      └─ 来自: docker-compose 或部署脚本                            │
│                                                                       │
│   2. 配置被环境变量覆盖                                              │
│      └─ 优先级: env var > config file                              │
│                                                                       │
│   3. 系统只有软解可用 (VAAPI 不可用)                               │
│      └─ 来自: 容器未映射 /dev/dri 设备                             │
│                                                                       │
│   4. 代码拒绝软解降级                                               │
│      └─ 逻辑: if requireHW && !hwAvailable → return (fail)        │
│                                                                       │
│   5. 所有解码器无法创建                                             │
│      └─ 结果: 4/4 解码器失败 → 四视图黑屏                          │
│                                                                       │
│ 可以总结为:                                                         │
│  "配置冲突导致了一个'软硬兼得'的不可能需求"                        │
│                                                                       │
│  前置条件 A (配置): requireHardwareDecode = true                   │
│  前置条件 B (环境): hardwareDecoderAvailable = false               │
│                                                                       │
│  后果: A ∧ B = 无解 → 系统崩溃 (黑屏)                             │
└─────────────────────────────────────────────────────────────────────┘
```

---

## 💡 关键决策节点

```
                    ┌─────────────────────────────┐
                    │ 应用启动                      │
                    └──────────────┬────────────────┘
                                   │
                    ┌──────────────▼──────────────┐
                    │ 读取配置                     │
                    │ require_hw = false (默认)    │
                    └──────────────┬────────────────┘
                                   │
          ┌────────────────────────▼─────────────────────────┐
          │ 读取环境变量: CLIENT_MEDIA_...=1                │
          │ 覆盖配置: require_hw = true                     │
          └────────────────────────┬─────────────────────────┘
                                   │
                    ┌──────────────▼──────────────┐
                    │ 尝试打开硬解 (VAAPI)        │
                    └──────────────┬────────────────┘
                                   │
           ╔═════════════════════════════════════════════════╗
           ║ 【决策点1】硬解打开成功?                         ║
           ╚════════╤════════════════════════════════════╤══╝
                    │                                    │
               ✓ YES                                   ✗ NO
                    │                                    │
        ┌──────────▼──────────┐         ┌──────────────▼──────────┐
        │ 使用硬解 VAAPI      │         │ 硬解初始化失败           │
        │ 解码器工作正常      │         │ (VAAPI: vaInitialize   │
        │ ✓ 视频显示         │         │  failed -1)            │
        └─────────────────────┘         └───────────┬────────────┘
                                                     │
          ╔════════════════════════════════════════════════════════╗
          ║【决策点2】require_hardware_decode = true?               ║
          ╚════════╤════════════════════════════════════════════╤═╝
                   │                                            │
              ✓ YES                                          ✗ NO
                   │                                            │
       ┌──────────▼──────────┐          ┌─────────────────────▼──────┐
       │ 【决策点3】检查     │          │ 使用软解 (FFmpeg)          │
       │ 是否有软解?         │          │ 解码器自动降级             │
       │                    │          │ ✓ 视频显示               │
       │ if (softAvailable) │          └────────────────────────────┘
       │   -> 软解降级       │
       │ else               │
       │   -> FAIL (黑屏)   │  ← 【实际路径】
       └────────────────────┘
            │
       ✗ 拒绝降级 (当前代码)
            │
       ┌────▼──────────────────────────────┐
       │ qCritical:                         │
       │ "[HW-REQUIRED] 硬解未激活...       │
       │  禁止退回软解"                     │
       │                                   │
       │ return;  // ← 不创建解码器        │
       └────┬──────────────────────────────┘
            │
       ┌────▼──────────────────────┐
       │ H264Decoder = NULL (0/4)  │
       │ 四个视频流都无法工作      │
       │ ✗ 四视图黑屏 (100%)       │
       └───────────────────────────┘
```

---

## 📊 时间序列图

```
时间线 (2026-04-11T07:48)

07:48:35.600
  ↓ 应用启动
  ├─ [INFO] Configuration initialized
  ├─ [INFO] override from env: CLIENT_MEDIA_REQUIRE_HARDWARE_DECODE=1
  └─ 确认: require_hardware_decode 被设为 true

07:48:35.739
  ↓ 解码器工厂初始化
  ├─ [DEBUG] [DecoderFactory] VAAPI: vaInitialize failed -1
  ├─ [INFO] availableDecoders = QList("FFmpeg(CPU)")
  └─ 确认: 系统只有软解，无硬解

07:48:56 (约21秒后)
  ↓ 视频流建立并尝试初始化
  ├─ [CRIT] cam_front: HW-REQUIRED 硬解未激活...
  ├─ [CRIT] cam_rear:  HW-REQUIRED 硬解未激活...
  ├─ [CRIT] cam_left:  HW-REQUIRED 硬解未激活...
  ├─ [CRIT] cam_right: HW-REQUIRED 硬解未激活...
  └─ 重复 100+ 次

结果
  ↓ 四个视频窗口黑屏
  └─ 无解码器 = 无图像 = 黑屏
```

---

## 🔧 修复决策树

```
客户端四视图黑屏
    │
    ├─ 问题: requireHardwareDecode=true 但硬解不可用
    │
    ├─【快速修复】(立即恢复)
    │   │
    │   ├─ 选项A: 环境变量 (最快)
    │   │   export CLIENT_MEDIA_REQUIRE_HARDWARE_DECODE=0
    │   │   重启客户端 → ✓ 视频显示
    │   │
    │   ├─ 选项B: Docker Compose (长期)
    │   │   修改: CLIENT_MEDIA_REQUIRE_HARDWARE_DECODE: "0"
    │   │   重建: docker-compose up -d
    │   │   → ✓ 视频显示
    │   │
    │   └─ 选项C: 配置文件 (备选)
    │       修改: require_hardware_decode: false
    │       重启 → ✓ 视频显示
    │
    └─【长期改进】(预防问题)
        │
        ├─ 代码级防御
        │   ├─ 实现硬解失败后自动软解降级
        │   ├─ 移除"禁止软解"的逻辑
        │   └─ → 永远有可用的解码器
        │
        ├─ 启动门禁
        │   ├─ 在启动时检查硬解可用性
        │   ├─ 如果 require=true 但硬解不可用，明确错误提示
        │   └─ → 快速诊断，无神秘黑屏
        │
        ├─ 文档化
        │   ├─ 创建 .env.example 说明含义
        │   ├─ 标注容器与宿主的差异
        │   └─ → 减少误配置
        │
        └─ 自适应模式
            ├─ 实现 "auto" 模式自动检测
            ├─ 替代 true/false 的二元决策
            └─ → 适应多种环境
```

---

## 📋 证据总结表

| 层级 | 症状 | 直接原因 | 代码位置 | 日志证据 | 修复方式 |
|-----|------|--------|---------|---------|---------|
| 1 | 黑屏 | 解码器初始化失败 | h264decoder.cpp ~1500 | [HW-REQUIRED] CRIT | - |
| 2 | 硬解失败 + 拒绝软解 | 配置冲突 + 无降级 | h264decoder.cpp | [HW-REQUIRED] 禁止退回 | 改环境变量 |
| 3 | VAAPI 不可用 | 驱动初始化失败 | FFmpeg/libva | vaInitialize failed -1 | - |
| 4 | 硬解不存在 | 容器配置错误 | docker-compose | availableDecoders=FFmpeg | 设置 =0 |
| 5 **根本** | 配置-环境冲突 | 环境变量强制硬解 | configuration.cpp | override from env:...=1 | 改 env var |

---

**视觉分析完成**  
**根本原因**: 环境变量硬解要求 vs 容器硬解不可用  
**快速修复**: `export CLIENT_MEDIA_REQUIRE_HARDWARE_DECODE=0`
