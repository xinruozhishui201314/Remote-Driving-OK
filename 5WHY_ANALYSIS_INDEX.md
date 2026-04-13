# 5 Why 分析 — 文档索引

**问题**: 客户端四个视图无法显示（100% 黑屏）  
**根本原因**: 环境变量强制硬解，但容器硬解不可用  
**修复**: `export CLIENT_MEDIA_REQUIRE_HARDWARE_DECODE=0`

---

## 📚 文档导航

### 🎯 快速入门 (3 分钟)

**推荐阅读顺序**:

1. **本文件** (2 分钟)
   - 了解文档结构
   - 找到适合的文档

2. **`5WHY_FINAL_SUMMARY.md`** (1 分钟)
   - 一句话根本原因
   - 立即修复方法

### 📖 详细文档 (15-30 分钟)

| 文档 | 用途 | 阅读时间 | 深度 |
|------|------|---------|------|
| `5WHY_QUICK_REFERENCE.md` | 快速查询 | 5 分钟 | L2 |
| `5WHY_DEEP_ANALYSIS.md` | 完整分析 | 20 分钟 | L5 |
| `5WHY_VISUAL_ANALYSIS.md` | 可视化理解 | 10 分钟 | L4 |
| `5WHY_FINAL_SUMMARY.md` | 总结结论 | 5 分钟 | L5 |

### 🛠️ 实操文档

| 文件 | 内容 | 场景 |
|------|------|------|
| `FIX_INSTRUCTIONS.sh` | 自动化修复脚本 | 一键修复 |
| `docker-compose.yml` | Docker 配置 | 容器部署 |
| `client_config.yaml` | 配置文件 | 应用配置 |

### 📊 原始诊断文档

这些文档记录了早期的诊断过程：

| 文件 | 内容 |
|------|------|
| `DIAGNOSTIC_REPORT.md` | 诊断报告 (含修复方案) |
| `ROOT_CAUSE_ANALYSIS_VIDEO_DECODE.md` | 详细根因分析 |
| `FIX_IMPLEMENTATION_SUMMARY.md` | 修复实现总结 |

---

## 🔍 按需求查找文档

### "我只想快速修复"
→ **`5WHY_FINAL_SUMMARY.md`** (看"立即修复"部分) + **`FIX_INSTRUCTIONS.sh`**
- 耗时: 5 分钟
- 效果: 恢复四视图显示

### "我想了解根本原因"
→ **`5WHY_QUICK_REFERENCE.md`**
- 耗时: 5 分钟
- 效果: 理解问题根源

### "我想深入理解整个问题"
→ **`5WHY_DEEP_ANALYSIS.md`**
- 耗时: 20 分钟
- 效果: 掌握所有细节

### "我是视觉学习者"
→ **`5WHY_VISUAL_ANALYSIS.md`**
- 包含: 流程图、决策树、时间序列
- 效果: 图形化理解

### "我想要一句话答案"
→ **`5WHY_FINAL_SUMMARY.md`** (第一部分)
- 效果: 快速理解关键点

### "我需要修改代码以防止问题"
→ **`5WHY_DEEP_ANALYSIS.md`** (修复方案部分) + **`DIAGNOSTIC_REPORT.md`**
- 耗时: 30-60 分钟
- 效果: 实现长期改进

---

## 📊 文档内容总览

### 5WHY_FINAL_SUMMARY.md
```
├─ 一句话结论
├─ 5 Why 分析结果 (Level 1-5)
├─ 问题链条
├─ 修复验证
├─ 关键证据 (3 条)
├─ 关键洞察
├─ 改进建议
└─ 最终诊断
```

### 5WHY_QUICK_REFERENCE.md
```
├─ 一句话根本原因
├─ 立即修复 (3 种方式)
├─ 验证修复
├─ 代码关键路径
├─ 快速检查清单
└─ 学习要点
```

### 5WHY_DEEP_ANALYSIS.md
```
├─ 问题陈述
├─ 5 Why 分析链 (第 1-5 层)
├─ 根本原因确认
├─ 代码位置及证据
├─ 具体修复方案 (A/B/C)
├─ 验证步骤
├─ 修复清单
└─ 下一步工作
```

### 5WHY_VISUAL_ANALYSIS.md
```
├─ ASCII 艺术图表 (症状→根因)
├─ 关键决策节点
├─ 时间序列图
├─ 修复决策树
└─ 证据总结表
```

---

## 🎯 问题速查表

| 问题 | 答案 | 位置 |
|------|------|------|
| 为什么黑屏? | 解码器初始化失败 | 5WHY_L1 |
| 为什么硬解失败? | vaInitialize failed -1 | 5WHY_L3 |
| 为什么不用软解? | 配置禁止软解降级 | 5WHY_L2 |
| 为什么强制硬解? | 环境变量覆盖配置 | 5WHY_L4 |
| 为什么环境变量是 1? | 部署配置错误或测试 | 5WHY_L5 |
| 如何快速修复? | 改环境变量或 docker-compose | SUMMARY |
| 代码在哪? | h264decoder.cpp ~1500 | DEEP_ANALYSIS |
| 日志在哪? | logs/client-*.log | DIAGNOSTIC |

---

## ⚡ 快速命令

### 查看根本原因
```bash
grep -A 20 "第5层" 5WHY_DEEP_ANALYSIS.md | head -30
```

### 查看快速修复
```bash
grep -A 10 "立即修复" 5WHY_FINAL_SUMMARY.md
```

### 运行修复脚本
```bash
bash FIX_INSTRUCTIONS.sh
```

### 查看关键代码
```bash
grep -n "requireHardwareDecode" client/src/h264decoder.cpp
```

### 查看关键日志
```bash
tail -f logs/client-*.log | grep -E "override from env|vaInitialize|HW-REQUIRED"
```

---

## 📈 阅读路径

### 路径1: "我只有 5 分钟"
```
5WHY_FINAL_SUMMARY.md (第1段) 
  ↓
FIX_INSTRUCTIONS.sh
```

### 路径2: "我有 15 分钟"
```
5WHY_QUICK_REFERENCE.md
  ↓
5WHY_FINAL_SUMMARY.md
  ↓
FIX_INSTRUCTIONS.sh
```

### 路径3: "我有 30 分钟"
```
5WHY_VISUAL_ANALYSIS.md (看图)
  ↓
5WHY_DEEP_ANALYSIS.md (读文)
  ↓
FIX_INSTRUCTIONS.sh (执行)
```

### 路径4: "我要成为专家"
```
5WHY_FINAL_SUMMARY.md (总览)
  ↓
5WHY_DEEP_ANALYSIS.md (深度)
  ↓
5WHY_VISUAL_ANALYSIS.md (图表)
  ↓
DIAGNOSTIC_REPORT.md (原始)
  ↓
ROOT_CAUSE_ANALYSIS_VIDEO_DECODE.md (技术细节)
```

---

## ✅ 文档版本

| 文件 | 版本 | 日期 | 作者 |
|------|------|------|------|
| 5WHY_FINAL_SUMMARY.md | 1.0 | 2026-04-11 | 根本原因分析 |
| 5WHY_DEEP_ANALYSIS.md | 1.0 | 2026-04-11 | 完整分析 |
| 5WHY_QUICK_REFERENCE.md | 1.0 | 2026-04-11 | 快速参考 |
| 5WHY_VISUAL_ANALYSIS.md | 1.0 | 2026-04-11 | 可视化 |
| FIX_INSTRUCTIONS.sh | 1.0 | 2026-04-11 | 修复脚本 |

---

## 🎓 关键概念词汇表

| 术语 | 含义 |
|------|------|
| **requireHardwareDecode** | 配置：是否强制硬解 |
| **CLIENT_MEDIA_REQUIRE_HARDWARE_DECODE** | 环境变量：覆盖配置强制硬解 |
| **vaInitialize failed** | VAAPI 初始化失败 (无硬解) |
| **HW-REQUIRED** | 硬解要求模式下的错误前缀 |
| **配置-环境冲突** | 期望的能力与实际环境不匹配 |
| **自动降级** | 硬解失败时自动使用软解 |
| **软解 (FFmpeg)** | CPU 基软件解码 |
| **硬解 (VAAPI)** | GPU 硬件加速解码 |

---

## 📞 常见问题 (FAQ)

**Q: 为什么会出现这个问题?**
A: 环境变量被设置为强制硬解，但容器中硬解不可用。见 5WHY_L5。

**Q: 会不会影响生产?**
A: 是的，如果生产环境用相同配置会有相同问题。建议改为 "auto" 模式。

**Q: 为什么这么隐蔽?**
A: 这是配置冲突而非代码 bug，症状是无法追踪的黑屏。

**Q: 如何防止再发生?**
A: 实现自动降级、启动门禁、更新文档。见改进建议。

**Q: 需要改代码吗?**
A: 立即修复不需要。长期改进需要代码级防御。

---

## 🚀 下一步

1. **立即** (5 分钟)
   - 阅读: `5WHY_FINAL_SUMMARY.md`
   - 执行: `bash FIX_INSTRUCTIONS.sh`

2. **今日** (30 分钟)
   - 阅读: `5WHY_DEEP_ANALYSIS.md`
   - 修改: docker-compose.yml

3. **本周** (2 小时)
   - 实现: 代码级防御 (自动降级)
   - 更新: 文档和示例配置

4. **本月** (4 小时)
   - 实现: 启动门禁
   - 新增: 健康检查端点

---

**最后更新**: 2026-04-11  
**分析方法**: 5 Why 根本原因分析  
**状态**: 完整分析，根本原因已确认 ✅
