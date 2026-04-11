# 客户端视频显示故障快速参考

## 问题现象
- 四路摄像头完全无视频输出
- 日志显示 `DECODE_API_ERR` 和 `fps=0`
- RTP 包到达但被丢弃

## 5 Why 根本原因链

```
❌ 问题：视频无法显示 (fps=0, decFrames=0)
  ↓
❌ Why 1：所有帧都被丢弃
  ↓ 原因：硬解失败 + 配置禁止软解
  ↓
❌ Why 2：硬解初始化失败
  ↓ 原因：VAAPI 驱动加载失败
  ↓
❌ Why 3：VAAPI 不可用
  ↓ 原因：系统是 NVIDIA GPU，编译时只启用 VA-API
  ↓
❌ Why 4：编译配置不匹配硬件
  ↓ 原因：构建时 -DENABLE_NVDEC=OFF
  ↓
❌ Why 5：系统设计缺陷
  ↓ 原因：require_hardware_decode 过于刚硬，无故障降级
```

## 修复方案

### 快速修复（立即）✅

```yaml
# client/config/client_config.yaml
media:
  require_hardware_decode: false  # ← 默认允许软解降级
```

**效果：** 自动降级至 CPU 软解，视频恢复

### 完整修复（推荐）

三个文件的改动：

1. **H264WebRtcHwBridge.cpp** (行145-157)
   - 添加配置检查，允许软解降级

2. **h264decoder.cpp** (行207-226)
   - 改进硬解检查逻辑

3. **client_config.yaml** (行109-112)
   - 更新注释说明

## 验证清单

```bash
# 1. 重建
cd client && rm -rf build && mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release .. && make -j$(nproc)

# 2. 启动
cd ../.. && bash scripts/start-full-chain.sh

# 3. 检查日志
tail -f logs/*/client-*.log | grep -E "CodecHealth|HW-E2E|DecodePath"
```

**预期看到：**
```
[INFO] hardware decoder unavailable, allowing software fallback
[INFO] WebRTC=libavcodec CPU decode
[INFO] verdict=OK fps=29.97
```

## 环境变量

| 变量 | 默认 | 说明 |
|-----|------|------|
| `CLIENT_MEDIA_REQUIRE_HARDWARE_DECODE` | `0` | 硬解失败时是否禁止软解 |
| `CLIENT_MEDIA_HARDWARE_DECODE` | `1` | 是否尝试硬解 |
| `CLIENT_FFMPEG_DECODE_THREADS` | `1` | 软解线程数 |

## 关键文件位置

```
client/
├── config/client_config.yaml          ← 配置
├── src/
│   ├── h264decoder.cpp                ← H.264 解码器
│   └── media/
│       └── H264WebRtcHwBridge.cpp    ← WebRTC 硬解桥接
└── README.md                          ← 启动参数文档
```

## 进一步优化

### 编译多硬件支持（可选）
```bash
cmake -DENABLE_VAAPI=ON -DENABLE_NVDEC=ON ..
```

### UI 增强（可选）
- 添加解码器类型指示
- 显示 fps 和 CPU 占用

## 性能参考

**CPU 软解：**
- 单路：15-20% CPU
- 四路：50-70% CPU
- 延迟：150-250ms
- 帧率：20-30 fps

## 相关文档

- `docs/DIAGNOSIS_VIDEO_DISPLAY_FAILURE.md` - 详细分析
- `docs/FIX_SUMMARY_VIDEO_DISPLAY.md` - 修复总结
- `client/README.md` - 启动参数
