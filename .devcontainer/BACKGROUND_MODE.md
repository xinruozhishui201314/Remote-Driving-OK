# 后台运行模式说明

## 概述

容器启动后，所有初始化脚本在后台运行，不阻塞终端，立即可以使用容器。

---

## 工作流程

### 1. 容器启动时

1. **postCreateCommand** 自动执行 `start-background.sh`
2. **start-background.sh** 立即启动后台任务：
   - `setup.sh` - 安装工具和配置环境（后台运行）
   - `verify-network.sh` - 网络诊断（后台运行）
3. **脚本立即返回**，不等待任务完成
4. **终端立即可用**，可以开始开发工作

### 2. 后台任务

- ✅ 所有任务使用 `nohup` 在后台运行
- ✅ 输出重定向到日志文件
- ✅ 不阻塞终端或容器启动
- ✅ 即使终端关闭，任务也会继续执行

---

## 日志文件位置

所有后台任务的日志保存在：

```
/tmp/devcontainer-logs/
├── setup.log              # setup.sh 的输出日志
└── network-verify.log     # 网络诊断的输出日志
```

### 查看日志

```bash
# 查看 setup 日志
tail -f /tmp/devcontainer-logs/setup.log

# 查看网络诊断日志
tail -f /tmp/devcontainer-logs/network-verify.log

# 查看所有日志
tail -f /tmp/devcontainer-logs/*.log
```

---

## 优势

### ✅ 不阻塞终端
- 容器启动后立即可以使用
- 不需要等待初始化完成
- 终端不会显示大量输出

### ✅ 后台执行
- 初始化任务在后台继续运行
- 不影响开发工作
- 可以随时查看日志

### ✅ 错误处理
- 即使某个任务失败，也不影响容器使用
- 错误信息记录在日志文件中
- 可以稍后查看日志排查问题

---

## 验证后台任务

### 检查后台进程

```bash
# 查看后台任务
ps aux | grep -E "(setup.sh|verify-network.sh)"

# 查看日志文件
ls -lh /tmp/devcontainer-logs/
```

### 手动运行（如果需要）

如果需要在前台运行查看输出：

```bash
# 前台运行 setup
bash .devcontainer/setup.sh

# 前台运行网络诊断
bash .devcontainer/verify-network.sh
```

---

## 配置说明

### devcontainer.json

```json
"postCreateCommand": "bash .devcontainer/start-background.sh"
```

### start-background.sh

- 使用 `nohup` 确保进程独立
- 使用 `&` 后台运行
- 立即 `exit 0` 返回，不等待

---

## 故障排查

### 如果后台任务没有运行

1. **检查日志目录**
   ```bash
   ls -la /tmp/devcontainer-logs/
   ```

2. **手动运行脚本**
   ```bash
   bash .devcontainer/start-background.sh
   ```

3. **检查进程**
   ```bash
   ps aux | grep setup
   ```

### 如果工具未安装

后台任务可能还在运行中，等待完成后工具会自动安装。

查看日志确认：
```bash
tail -f /tmp/devcontainer-logs/setup.log
```

---

## 总结

✅ **容器启动后立即可用**
✅ **初始化任务在后台运行**
✅ **不阻塞终端**
✅ **日志文件记录所有输出**

**可以立即开始开发工作！** 🚀
