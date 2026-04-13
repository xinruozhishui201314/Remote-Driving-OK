# 客户端四路视频黑屏 — 快速修复指南

**问题**: 四个摄像头都无法显示视频（100% 黑屏）  
**根本原因**: `CLIENT_MEDIA_REQUIRE_HARDWARE_DECODE=1` 强制硬解，但容器中 VAAPI 硬解初始化失败，导致拒绝软解降级  
**修复时间**: < 1 分钟

---

## 🚀 **一键快速修复**

```bash
cd /home/wqs/Documents/github/Remote-Driving

# 执行自动修复脚本
bash fix_video_black_screen.sh
```

脚本会自动：
1. ✓ 清空 `CLIENT_MEDIA_REQUIRE_HARDWARE_DECODE` 环境变量
2. ✓ 修改配置文件 `media.require_hardware_decode: false`
3. ✓ 停止旧客户端进程
4. ✓ 启动新客户端实例
5. ✓ 验证修复结果

---

## 🔧 **手动修复**

### **方法1：清空环境变量（最快）**

```bash
# 清空环境变量
unset CLIENT_MEDIA_REQUIRE_HARDWARE_DECODE

# 验证已清空
echo "CLIENT_MEDIA_REQUIRE_HARDWARE_DECODE=$CLIENT_MEDIA_REQUIRE_HARDWARE_DECODE"

# 启动客户端
cd /home/wqs/Documents/github/Remote-Driving
./build/client
```

**预期结果**: 3-5 秒后四个视频面板显示实时画面

---

### **方法2：修改配置文件（永久修复）**

编辑 `client/config/client_config.yaml`:

```yaml
# 找到这一行：
media:
  require_hardware_decode: true    # ← 改成 false

# 改为：
media:
  require_hardware_decode: false   # ← 已修改
```

然后启动客户端：
```bash
cd /home/wqs/Documents/github/Remote-Driving
./build/client
```

---

### **方法3：使用 Docker 环境变量**

编辑 `docker-compose.yml`:

```yaml
services:
  remote-driving-client:
    environment:
      # 改这一行：
      - CLIENT_MEDIA_REQUIRE_HARDWARE_DECODE=0  # 从 1 改为 0
```

然后：
```bash
docker-compose up -d remote-driving-client
```

---

## ✅ **验证修复**

### **方式1：查看客户端窗口**

启动后，四个视频面板应显示实时画面（可能有延迟或卡顿，因为使用 CPU 软解）

### **方式2：查看日志**

```bash
# 查看日志是否包含这些关键内容
tail -100 logs/client-*.log | grep -E "FFmpegSoftDecoder|codecOpen|HW-E2E.*ok"

# 应该看到类似内容：
# [INFO] [Client][DecoderFactory] selected FFmpegSoftDecoder (CPU) codec= "H264"
# [INFO] [H264][HW-E2E] ok backend= "FFmpegSoftDecoder"
# [INFO] stream=carla-sim-001_cam_left verdict=OK fps=15 decFrames=120 codecOpen=1
```

### **方式3：检查是否还有错误**

```bash
# 不应该看到这个错误
grep "HW-REQUIRED.*禁止退回软解" logs/client-*.log

# 若输出为空，说明修复成功 ✓
```

---

## 📊 **修复前后对比**

| 项目 | 修复前 | 修复后 |
|------|-------|--------|
| 视频显示 | ❌ 黑屏 | ✅ 正常 |
| `codecOpen` | false | true |
| 解码器后端 | 未初始化 | FFmpegSoftDecoder (CPU) |
| 日志错误 | `[HW-REQUIRED]` ×100 | 无此错误 |
| CPU 使用 | N/A | 可能较高（软解） |

---

## 🔍 **问题诊断**

### **症状：修复后仍然黑屏**

1. **检查环境变量是否真的被清空**：
   ```bash
   env | grep CLIENT_MEDIA
   # 应该什么都不输出
   ```

2. **检查是否在新 shell 中清空了**：
   ```bash
   # 若用了 source 或 . 加载环境变量，需要重开 shell
   bash
   cd /home/wqs/Documents/github/Remote-Driving
   ./build/client
   ```

3. **检查配置文件是否被正确修改**：
   ```bash
   grep -A 3 "media:" client/config/client_config.yaml
   # 应该显示：require_hardware_decode: false
   ```

4. **查看完整的错误日志**：
   ```bash
   tail -200 logs/client-*.log | tail -50
   ```

---

## 📚 **详细分析文档**

完整的根因分析请阅读：
- **主文档**: `FINAL_ROOT_CAUSE_ANALYSIS.md`
- **代码位置**: `client/src/h264decoder.cpp` 第 213-230 行
- **配置位置**: `client/config/client_config.yaml` → `media.require_hardware_decode`

---

## ⚠️ **常见问题**

**Q: 修复后视频很卡顿**  
A: 这是正常的。因为改用 CPU 软解而非硬解（GPU）。若要硬解，需在 Docker 中配置 GPU 设备映射。参考 `BUILD_GUIDE.md`。

**Q: 能否同时启用硬解和自动降级？**  
A: 可以，这正是我们的长期改进计划。参考 `FINAL_ROOT_CAUSE_ANALYSIS.md` 中的「方案 B」。

**Q: 如何检查是否真的用了硬解？**  
A: 查看日志中是否显示 `NvdecDecoder` 或 `VAAPIDecoder`。若显示 `FFmpegSoftDecoder` 说明使用的是软解。

---

## 🎯 **后续步骤**

1. **立即** ← 执行上述任一修复方法
2. **今天** ← 验证四路视频正常显示
3. **本周** ← 考虑是否需要硬解或可以接受软解
4. **本月** ← 在代码级实现硬解自动降级（参考方案 B）

---

**最后更新**: 2026-04-11  
**下一次行动**: 执行 `bash fix_video_black_screen.sh`
