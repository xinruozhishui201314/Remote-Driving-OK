# 客户端硬件解码故障排查指南

## 问题现象

客户端连接后所有4路视频（front/rear/left/right）均无法显示，日志显示：

```
[CRIT] [H264][ "carla-sim-001_cam_*" ][HW-REQUIRED] 
  硬解已编译但未激活（设备/驱动不可用），
  media.require_hardware_decode=true 禁止退回软解。
  检查 VA-API/NVDEC 设备可用性或设置 
  media.require_hardware_decode=false / CLIENT_MEDIA_REQUIRE_HARDWARE_DECODE=0
```

关键症状：
- VA-API 驱动加载失败：`va_openDriver() returns -1`
- 解码器创建失败：`ensureDecoder_failed drop_frame`
- 视频帧统计：fps=0, emitted=0, codecOpen=false

---

## 5 Why 根本原因分析

### 层次1：近因（症状）
**硬解不可用导致解码器创建失败**
- 所有4路流在解码初始化阶段失败
- RTP 包正常到达但全部被丢弃
- 客户端显示黑屏

### 层次2：直接原因
**requireHardwareDecode=true 配置禁止了软解降级**

```
当 requireHardwareDecode=true AND 硬解不可用时：
  → FFmpeg 软解被创建成功
  → 但代码中检查 m_dec->isHardwareAccelerated() 返回 false
  → 配置检查触发 m_dec.reset() 将解码器删除
  → 返回 null，初始化失败
```

**代码位置**：`client/src/media/H264WebRtcHwBridge.cpp:145-157`

### 层次3：硬解不可用的原因

**Why2：VA-API 驱动加载失败**

```
vaInitialize() 返回 -1（失败）的原因：
  libva 库无法找到或加载驱动模块：
  - /usr/lib/x86_64-linux-gnu/dri/iHD_drv_video.so (Intel)
  - /usr/lib/x86_64-linux-gnu/dri/i965_drv_video.so (AMD/Legacy)
```

**Why3：运行环境缺少 VA-API 驱动**

容器/虚拟机环境问题：
- 基础镜像缺少 `libva-drivers` / `intel-media-driver` / `mesa` 包
- `/dev/dri/*` 设备未正确暴露给容器
- LD_LIBRARY_PATH 配置不正确
- GPU 驱动与 libva 版本不匹配

**Why4：配置与环境不匹配**

可能的原因：
- 环境变量 `CLIENT_MEDIA_REQUIRE_HARDWARE_DECODE=1` 被设置（强制硬解）
- 但运行环境（开发/CI/容器）实际不支持硬解
- YAML 配置中 `require_hardware_decode: true` 但系统无 GPU/驱动

### 层次4 & 5：根本原因

**系统级根本原因链**：

```
┌─────────────────────────────────────────────────┐
│ 根本原因 Why5：容器化部署缺少硬件加速基础     │
└─────────────────────────────────────────────────┘
     │
     ├─ A. 构建时：硬解支持编译可能被禁用
     │      (CMake -DENABLE_VAAPI=OFF / -DENABLE_NVDEC=OFF)
     │
     ├─ B. 运行时：硬解环境不满足
     │      (1) VA-API 驱动库缺失
     │      (2) GPU 设备不可用
     │      (3) 驱动版本不兼容
     │
     └─ C. 配置管理：理论与实际矛盾
          (1) 配置指定"必须硬解"BUT 环境实际无硬解
          (2) 缺少容错降级机制
          (3) 环境变量优先级不清
```

---

## 诊断步骤

### 1. 检查日志中的关键信息

```bash
# 查看 VA-API 驱动加载尝试
grep -i "va_openDriver\|libva info" client.log

# 查看硬解初始化状态
grep -i "HW-E2E\|HW-REQUIRED\|hardware.*decode" client.log

# 查看选中的解码器
grep -i "DecoderFactory.*selected\|FFmpegSoftDecoder" client.log
```

### 2. 检查编译配置

```bash
# 查看硬解编译支持
cd client/build
cmake .. -DENABLE_VAAPI=ON -DENABLE_NVDEC=ON

# 或检查 CMakeLists.txt
grep -i "ENABLE_VAAPI\|ENABLE_NVDEC" CMakeLists.txt
```

### 3. 检查运行时环境

**在容器中执行**：

```bash
# 检查 VA-API 驱动文件
ls -la /usr/lib/x86_64-linux-gnu/dri/*drv_video.so

# 检查 GPU 设备
ls -la /dev/dri/renderD*

# 查看 DRM 信息
cat /sys/class/drm/*/device/vendor  # 应显示 0x8086(Intel) 或 0x1022(AMD)

# 测试 VA-API 库
ldd /usr/lib/x86_64-linux-gnu/libva.so.2

# 运行 vainfo 诊断工具
vainfo
```

### 4. 检查配置文件和环境变量

```bash
# 检查 YAML 配置
grep -A 10 "^media:" client/config/client_config.yaml

# 检查环境变量
echo "CLIENT_MEDIA_HARDWARE_DECODE=$CLIENT_MEDIA_HARDWARE_DECODE"
echo "CLIENT_MEDIA_REQUIRE_HARDWARE_DECODE=$CLIENT_MEDIA_REQUIRE_HARDWARE_DECODE"
echo "CLIENT_STRICT_HW_DECODE_REQUIRED=$CLIENT_STRICT_HW_DECODE_REQUIRED"
```

---

## 修复方案

### 修复1：短期（立即恢复服务）

**问题**：强制硬解导致黑屏

**解决**：允许软解降级

```bash
# 方案 A：环境变量覆盖
export CLIENT_MEDIA_REQUIRE_HARDWARE_DECODE=0

# 方案 B：修改配置文件
# 编辑 client/config/client_config.yaml
media:
  require_hardware_decode: false  # 改为 false
```

**验证**：
```bash
# 重启客户端后，应看到：
grep "FFmpegSoftDecoder\|software decoder fallback" client.log

# 视频应正常显示
```

### 修复2：中期（智能降级，保留硬解优势）

**问题**：上述修复牺牲了有 GPU 环境的硬解性能

**解决**：采用"优选硬解，自动降级"策略

```yaml
# client/config/client_config.yaml
media:
  hardware_decode: true          # 启用硬解尝试
  require_hardware_decode: true  # 表示"优选硬解"
```

**关键改进**（已在代码中实现）：
- 修改 `H264WebRtcHwBridge.cpp` 中的降级逻辑
- 当硬解不可用时，允许 FFmpeg 软解 fallback
- 只有设置 `CLIENT_STRICT_HW_DECODE_REQUIRED=1` 时才禁止 fallback
- 记录诊断信息帮助运维排查

**效果**：
- ✅ 有硬解的环境继续用硬解（最优性能）
- ✅ 无硬解的环境自动降级到软解（可用）
- ✅ 同时告知运维有硬解故障（可针对性修复）

### 修复3：长期（架构改进与生产就绪）

**目标**：建立完善的硬解管理框架

#### 3.1 容器镜像增强

```dockerfile
# Dockerfile.client-prod
FROM ubuntu:22.04

# 安装 VA-API 依赖
RUN apt-get update && apt-get install -y \
    libva2 \
    libva-drm2 \
    intel-media-driver \
    mesa-va-drivers \
    libva-dev

# 挂载 GPU 设备（docker-compose/k8s 中配置）
# devices:
#   - /dev/dri:/dev/dri:rw
```

#### 3.2 配置管理改进

```yaml
# client/config/client_config.yaml
media:
  # 【新增】硬解启用策略
  hardware_decode:
    enabled: true              # 全局开关
    prefer_mode: "auto"        # auto / always / never
    # auto: 尝试硬解，失败则降级
    # always: 要求硬解，失败则拒绝
    # never: 始终软解

  # 【新增】环境特异配置
  environment_profiles:
    development:
      prefer_mode: "auto"      # 开发环境允许降级
    production:
      prefer_mode: "always"    # 生产环境要求硬解
    ci:
      prefer_mode: "never"     # CI 环境禁用硬解
```

#### 3.3 健康检查与自愈

```cpp
// 在 DecoderFactory 中添加
class HardwareDecodeHealthCheck {
  void runDiagnostics();
  void suggestRemediationSteps();
  void emitMetrics();
};
```

#### 3.4 操作手册

创建 `docs/HARDWARE_DECODE_DEPLOYMENT.md`，包含：
- 各种容器编排平台的 GPU 配置（Docker、K8s、Docker Swarm）
- 常见故障快速排查表
- CI/CD 集成建议
- 性能基准测试

---

## 快速修复检查清单

```
[ ] 1. 确认现象：所有视频黑屏 + 日志显示 HW-REQUIRED 错误
[ ] 2. 查看日志：grep "VA-API\|DecoderFactory\|HW-E2E" 
[ ] 3. 决定修复级别：
      [ ] 短期（立即修复）→ 设置 CLIENT_MEDIA_REQUIRE_HARDWARE_DECODE=0
      [ ] 中期（智能降级）→ 已在代码 H264WebRtcHwBridge.cpp 中实现
      [ ] 长期（架构改进）→ 安装硬解驱动、完善配置管理
[ ] 4. 实施修复后验证：
      [ ] 重启客户端
      [ ] grep "selected.*Decoder\|software decoder fallback" 应看到软解
      [ ] 视频显示正常
      [ ] 性能可接受（即使是软解）
[ ] 5. 如需硬解（生产环境）：
      [ ] 安装 libva-drivers / intel-media-driver
      [ ] 挂载 /dev/dri 到容器
      [ ] 验证 vainfo 命令正常
      [ ] 设置 CLIENT_STRICT_HW_DECODE_REQUIRED=1（可选，强制硬解）
```

---

## 参考资源

### 代码文件
- 硬解工厂：`client/src/infrastructure/media/DecoderFactory.cpp`
- WebRTC 硬解桥接：`client/src/media/H264WebRtcHwBridge.cpp`
- 配置管理：`client/src/core/configuration.cpp/h`
- H264 解码器：`client/src/h264decoder.cpp`

### 配置文件
- 客户端配置：`client/config/client_config.yaml`
- 构建配置：`client/CMakeLists.txt`

### 环境变量
- `CLIENT_MEDIA_HARDWARE_DECODE` - 启用/禁用硬解尝试
- `CLIENT_MEDIA_REQUIRE_HARDWARE_DECODE` - 硬解失败时的行为
- `CLIENT_STRICT_HW_DECODE_REQUIRED` - 启用严格硬解强制模式
- `CLIENT_FFMPEG_DECODE_THREADS` - FFmpeg 解码线程数

### 外部工具
```bash
# VA-API 诊断
vainfo                          # 显示 VA-API 能力
va_info --display drm --device /dev/dri/renderD128

# 硬件检查
glxinfo | grep "OpenGL vendor"  # 检查 GPU
lspci | grep VGA                # 列出视频设备

# Docker GPU 支持
docker run --gpus all nvidia/cuda:11.0-base nvidia-smi
```

---

## 常见问题 FAQ

### Q1：如何快速恢复视频显示？
**A**：在运行客户端前设置：
```bash
export CLIENT_MEDIA_REQUIRE_HARDWARE_DECODE=0
./RemoteDrivingClient
```

### Q2：我需要硬解来降低 CPU 使用率，怎么办？
**A**：
1. 在 Docker/容器中挂载 `/dev/dri`：`--device /dev/dri:/dev/dri:rw`
2. 安装硬解驱动包（见修复3.1）
3. 保持 `CLIENT_MEDIA_REQUIRE_HARDWARE_DECODE=1` 并验证：`grep "selected.*Decoder" client.log`

### Q3：FFmpeg 软解的性能如何？
**A**：
- CPU 使用率：H.264 1920x1080@30fps 约 50-80%（单核）
- 功耗：约 10-15W（相比硬解 2-3W）
- 延迟：<50ms（可接受）
- **结论**：开发/测试环境可用，生产建议硬解

### Q4：如何在生产环境中强制硬解？
**A**：
```bash
# 同时满足以下条件
export CLIENT_STRICT_HW_DECODE_REQUIRED=1
export CLIENT_MEDIA_REQUIRE_HARDWARE_DECODE=1
# + 确保硬解驱动已安装
# + 确保 GPU 设备已暴露
```

### Q5：如何诊断 VA-API 不可用的原因？
**A**：
```bash
# 1. 检查驱动文件
ls /usr/lib/x86_64-linux-gnu/dri/*drv_video.so

# 2. 运行诊断工具
vainfo

# 3. 查看 libva 日志
libva_debug=2 vainfo 2>&1 | head -20

# 4. 检查 GPU 设备
cat /sys/class/drm/card*/device/vendor
```

---

## 修复日期与版本记录

| 日期 | 版本 | 修复内容 |
|------|------|--------|
| 2026-04-11 | v1.0 | 初版：5 Why 分析 + 3 层修复方案实现 |
| - | - | 短期：允许软解降级 |
| - | - | 中期：智能自适应降级（代码实现） |
| - | - | 长期：架构改进指南（待实施） |

