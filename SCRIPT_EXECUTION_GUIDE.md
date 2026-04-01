# 脚本执行指南

## 问题说明

如果遇到 `Permission denied` 错误，原因是**文件系统挂载了 `noexec` 选项**。

### 为什么不能像普通 Ubuntu 系统那样直接执行？

**普通 Ubuntu 系统**：
```bash
# 文件系统挂载没有 noexec
/dev/sda1 on /home type ext4 (rw,relatime)
./build.sh  ✅ 可以直接执行
```

**您的环境**：
```bash
# 文件系统挂载有 noexec（安全策略）
/dev/sdb9 on /workspaces/Remote-Driving type ext4 (rw,nosuid,nodev,noexec,relatime)
./build.sh  ❌ Permission denied（受 noexec 限制）
bash build.sh  ✅ 可以使用 bash 执行
```

**原因**：`noexec` 是宿主机文件系统的安全配置，不是容器的问题。它阻止直接执行脚本，但可以通过 `bash` 解释器执行。

### 检查文件系统挂载

```bash
mount | grep workspaces
# 如果看到 noexec，说明文件系统不允许直接执行脚本
```

---

## 解决方案

### 方法一：使用 bash 执行（推荐）

**不要使用** `./build.sh`，而是使用：

```bash
bash build.sh
bash run.sh
bash debug.sh
```

### 方法二：修复权限（已自动处理）

容器启动时，`setup.sh` 会自动修复所有脚本的执行权限。

如果需要手动修复：

```bash
# 修复单个脚本
chmod +x build.sh

# 修复所有脚本
find . -name "*.sh" -type f -exec chmod +x {} \;

# 或使用修复脚本
bash .devcontainer/fix-script-permissions.sh
```

---

## 各工程脚本执行方法

### Client 工程

```bash
cd /workspace/client

# 编译
bash build.sh

# 运行
bash run.sh

# 调试
bash debug.sh
```

### Media 工程

```bash
cd /workspace/media

# 编译
bash build.sh

# 运行
bash run.sh
```

### Vehicle-side 工程

```bash
cd /workspace/Vehicle-side

# 编译
bash build.sh

# 运行
bash run.sh mqtt://192.168.1.100:1883
```

---

## 自动修复

### 容器启动时自动修复

`setup.sh` 会在容器启动时自动修复所有脚本权限：

```bash
# 在 setup.sh 中
find /workspace -name "*.sh" -type f -exec chmod +x {} \; 2>/dev/null || true
```

### 手动运行修复脚本

```bash
bash .devcontainer/fix-script-permissions.sh
```

---

## 验证脚本权限

```bash
# 检查脚本权限
ls -l client/*.sh

# 应该看到 -rwxrwxr-x 或类似的可执行权限
```

---

## 常见问题

### Q1: 为什么 chmod +x 后还是不能执行？

**A**: 如果文件系统挂载了 `noexec`，即使有执行权限也不能直接执行。
**解决**: 使用 `bash script.sh` 而不是 `./script.sh`

### Q2: 如何检查文件系统是否支持执行？

```bash
# 检查挂载选项
mount | grep workspaces

# 如果看到 noexec，需要使用 bash 执行
```

### Q3: 能否移除 noexec 选项？

**不建议**: noexec 可能是出于安全考虑设置的。
**推荐**: 使用 `bash script.sh` 方式执行。

---

## 总结

✅ **推荐方式**: 使用 `bash script.sh` 执行脚本
✅ **自动修复**: 容器启动时自动修复权限
✅ **持久化**: 配置已保存，每次容器启动都会自动处理

---

**使用 `bash build.sh` 而不是 `./build.sh` 可以避免权限问题！** ✅
