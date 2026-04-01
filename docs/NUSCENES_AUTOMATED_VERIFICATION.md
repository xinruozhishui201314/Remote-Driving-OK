# NuScenes 推流脚本自动化验证说明

## Executive Summary

**目标**：自动化验证 NuScenes 推流脚本的配置正确性，确保低码率优化配置生效。

**验证内容**：
- ✅ 脚本语法检查
- ✅ 配置参数验证
- ✅ FFmpeg 命令构建验证
- ✅ 数据集路径检查
- ✅ Docker Compose 配置检查

---

## 1. 验证脚本

### 1.1 基础验证脚本

**文件**：`scripts/verify-nuscenes-streaming.sh`

**功能**：
- 检查脚本文件存在性
- 检查脚本语法
- 检查必要工具（ffmpeg, bash）
- 验证配置参数格式
- 检查 x264-params 构建逻辑
- 验证 FFmpeg 命令构建
- 检查数据集路径和文件
- 验证 Docker Compose 配置

**使用方法**：
```bash
bash scripts/verify-nuscenes-streaming.sh
```

### 1.2 修复与验证脚本

**文件**：`scripts/fix-and-verify-nuscenes.sh`

**功能**：
- 自动检查并修复常见问题
- 运行完整验证
- 提供下一步操作建议

**使用方法**：
```bash
bash scripts/fix-and-verify-nuscenes.sh
```

---

## 2. 验证项目详解

### 2.1 脚本语法检查

**检查项**：
- Bash 语法正确性
- 变量引用正确性
- 数组操作正确性

**方法**：
```bash
bash -n scripts/push-nuscenes-cameras-to-zlm.sh
```

### 2.2 配置参数验证

**检查项**：
- BITRATE 格式：`^[0-9]+[kmg]?$`
- MAXRATE 格式：`^[0-9]+[kmg]?$`
- BUFSIZE 格式：`^[0-9]+[kmg]?$`
- FPS 格式：`^[0-9]+$`

**默认值**：
- BITRATE: 200k
- MAXRATE: 250k
- BUFSIZE: 100k
- FPS: 10

### 2.3 x264-params 构建验证

**检查项**：
- 参数数组构建正确性
- IFS 分隔符使用正确性
- 变量替换正确性（去除 'k' 后缀）

**示例输出**：
```
slices=1:nal-hrd=cbr:force-cfr=1:vbv-bufsize=100:vbv-maxrate=250:me=dia:subme=1:...
```

### 2.4 FFmpeg 命令构建验证

**检查项**：
- 命令参数完整性
- 参数格式正确性
- FFmpeg 能否解析命令（dry-run）

**方法**：
```bash
ffmpeg [参数] -f null -t 0 - 2>&1
```

### 2.5 数据集路径检查

**检查项**：
- SWEEPS_PATH 目录存在性
- 相机目录存在性（CAM_FRONT, CAM_BACK, CAM_FRONT_LEFT, CAM_FRONT_RIGHT）
- 图片文件存在性（.jpg, .png）

### 2.6 Docker Compose 配置检查

**检查项**：
- 推流脚本路径配置
- 环境变量配置（NUSCENES_BITRATE, NUSCENES_MAXRATE, NUSCENES_BUFSIZE）
- 数据集挂载配置

---

## 3. 常见问题与修复

### 3.1 脚本语法错误

**问题**：Bash 语法错误

**修复**：
```bash
# 检查语法
bash -n scripts/push-nuscenes-cameras-to-zlm.sh

# 修复常见问题：
# - 变量引用：${VAR} 而不是 $VAR（在字符串中）
# - 数组操作：正确使用 [@] 和 [*]
# - 条件判断：[[ ]] 而不是 [ ]
```

### 3.2 x264-params 构建失败

**问题**：IFS 分隔符使用不当

**修复**：
```bash
# 正确方式
X264_PARAMS_STR=$(IFS=:; echo "${X264_PARAMS[*]}")

# 错误方式（会导致参数连接）
X264_PARAMS_STR="${X264_PARAMS[*]}"  # 使用空格分隔
```

### 3.3 码率参数格式错误

**问题**：环境变量值格式不正确

**修复**：
```bash
# 正确格式
export NUSCENES_BITRATE=200k
export NUSCENES_MAXRATE=250k
export NUSCENES_BUFSIZE=100k

# 错误格式
export NUSCENES_BITRATE="200k"  # 引号会被包含
export NUSCENES_BITRATE=200     # 缺少单位
```

### 3.4 数据集路径不存在

**问题**：SWEEPS_PATH 未设置或路径错误

**修复**：
```bash
# 设置正确的数据集路径
export SWEEPS_PATH=/path/to/nuscenes-mini/sweeps

# 或在 Docker Compose 中配置
environment:
  - SWEEPS_PATH=/data/sweeps
volumes:
  - /path/to/nuscenes-mini/sweeps:/data/sweeps:ro
```

---

## 4. 验证结果解读

### 4.1 成功输出

```
==========================================
验证完成
==========================================
错误: 0
警告: 0

✅ 所有检查通过！
```

**含义**：
- 所有检查项通过
- 脚本配置正确
- 可以正常使用

### 4.2 警告输出

```
错误: 0
警告: 2

✅ 所有检查通过！
⚠️  有 2 个警告，请检查
```

**含义**：
- 核心功能正常
- 有非关键问题需要关注
- 可以继续使用，但建议修复警告

### 4.3 错误输出

```
错误: 3
警告: 1

❌ 发现 3 个错误，请修复后重试
```

**含义**：
- 存在关键问题
- 脚本可能无法正常运行
- 必须修复错误后才能使用

---

## 5. 持续集成（CI）集成

### 5.1 GitHub Actions 示例

```yaml
name: Verify NuScenes Streaming Script

on: [push, pull_request]

jobs:
  verify:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v2
      - name: Install FFmpeg
        run: sudo apt-get update && sudo apt-get install -y ffmpeg
      - name: Run Verification
        run: bash scripts/verify-nuscenes-streaming.sh
```

### 5.2 本地预提交钩子

```bash
#!/bin/bash
# .git/hooks/pre-commit

if git diff --cached --name-only | grep -q "scripts/push-nuscenes-cameras-to-zlm.sh"; then
    echo "验证推流脚本..."
    bash scripts/verify-nuscenes-streaming.sh || exit 1
fi
```

---

## 6. 验证检查清单

### 6.1 开发阶段

- [ ] 脚本语法检查通过
- [ ] 配置参数格式正确
- [ ] x264-params 构建正确
- [ ] FFmpeg 命令构建正确

### 6.2 部署前

- [ ] 数据集路径配置正确
- [ ] Docker Compose 配置正确
- [ ] 环境变量配置正确
- [ ] 实际推流测试通过

### 6.3 生产环境

- [ ] 码率监控正常
- [ ] 视频质量可接受
- [ ] 网络带宽充足
- [ ] CPU 占用正常

---

## 7. 相关文件

- `scripts/verify-nuscenes-streaming.sh` - 验证脚本
- `scripts/fix-and-verify-nuscenes.sh` - 修复与验证脚本
- `scripts/push-nuscenes-cameras-to-zlm.sh` - 推流脚本
- `docker-compose.vehicle.dev.yml` - Docker Compose 配置
- `docs/NUSCENES_AUTOMATED_VERIFICATION.md` - 本文档

---

## 8. 总结

**自动化验证优势**：
- ✅ 快速发现问题
- ✅ 确保配置正确
- ✅ 减少人工错误
- ✅ 提高开发效率

**使用建议**：
- 每次修改脚本后运行验证
- 在 CI/CD 中集成验证
- 部署前必须通过验证
- 定期运行验证检查
