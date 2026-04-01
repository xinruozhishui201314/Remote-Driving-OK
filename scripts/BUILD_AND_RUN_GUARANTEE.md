# 编译和运行保证机制

## 概述

`start-full-chain.sh` 脚本已修改，确保：
1. **强制重新编译客户端**（确保使用最新代码）
2. **编译失败时立即停止执行**（不会继续运行）
3. **验证 QML 文件存在**（确保修改已保存）

## 关键修改

### 1. `ensure_client_built()` 函数

**修改前**：
- 如果客户端已编译，直接返回（可能使用旧代码）
- 编译失败时返回1，但主脚本使用 `|| true`，不会停止

**修改后**：
- **强制重新编译**（每次运行都重新编译）
- 使用 `/tmp/client-build` 目录确保干净编译
- 编译失败时调用 `exit 1`，**直接停止脚本执行**
- 验证 QML 文件存在和关键修改

### 2. `start_client()` 函数

**修改前**：
- 如果客户端已编译，直接使用（可能使用旧代码）
- 编译失败时使用 `|| { echo "客户端编译失败"; exit 1; }`，但可能被忽略

**修改后**：
- 在子shell中使用 `set -e`，**任何错误立即退出**
- 优先使用 `/tmp/client-build`（最新编译的）
- 验证 QML 文件存在
- 编译失败时调用 `exit 1`

### 3. 主脚本调用

**修改前**：
```bash
ensure_client_built || true  # 编译失败不会停止
```

**修改后**：
```bash
ensure_client_built  # 编译失败会直接退出（set -e）
```

## 运行流程

运行 `bash scripts/start-full-chain.sh manual` 时：

1. **启动容器和服务**
2. **强制重新编译客户端**（`ensure_client_built`）
   - 停止可能正在运行的客户端进程
   - 清理旧的构建目录
   - 强制重新编译（`cmake` + `make`）
   - 验证可执行文件存在
   - 验证 QML 文件存在
   - 验证关键修改（`Layout.preferredWidth: 35`）
   - **如果任何步骤失败，调用 `exit 1` 停止执行**
3. **启动客户端**（`start_client`）
   - 优先使用 `/tmp/client-build/RemoteDrivingClient`（最新编译的）
   - 验证 QML 文件存在
   - **如果编译失败，调用 `exit 1` 停止执行**

## 验证机制

### 编译时验证
- ✓ 可执行文件存在
- ✓ QML 文件存在
- ✓ 关键修改存在（`Layout.preferredWidth: 35`）

### 运行时验证
- ✓ QML 文件加载路径正确
- ✓ QML 文件修改时间最新
- ✓ 所有关键修改都已验证

## 错误处理

### 编译错误
```
❌ 客户端编译失败！
请检查编译错误信息 above，修复后重新运行。
```
→ **脚本立即停止**（`exit 1`）

### QML 文件不存在
```
❌ 错误: QML 文件不存在
```
→ **脚本立即停止**（`exit 1`）

### 可执行文件不存在
```
错误: 编译完成但可执行文件不存在
```
→ **脚本立即停止**（`exit 1`）

## 使用说明

### 正常使用
```bash
bash scripts/start-full-chain.sh manual
```

### 跳过编译（不推荐）
```bash
bash scripts/start-full-chain.sh manual no-build
```
⚠️ **警告**: 如果客户端编译失败，脚本仍会停止执行

### 验证修改是否生效
```bash
bash scripts/verify-qml-changes.sh
```

## 日志输出

运行时会看到详细的日志：
```
[QML_LOAD] ========== 开始查找 main.qml 文件 ==========
[QML_LOAD] ✓ 找到 QML 文件: QUrl("file:///workspace/client/qml/main.qml")
[QML_LOAD] ✓ DrivingInterface.qml 存在
[QML_LOAD] ========== 验证 DrivingInterface.qml 修改 ==========
[QML_LOAD] ✓ 找到 Layout.preferredWidth: 35 (水箱/垃圾箱宽度)
[QML_LOAD] ✓ 找到 spacing: 16 (按钮间距)
[QML_LOAD] ✓ 找到 width: 50; height: 42 (急停按钮)
[QML_LOAD] ✓ 找到 width: 90; height: 40 (目标速度输入框)
[QML_LOAD] ✓ 找到进度条长度缩短 (* 0.5)
[QML_LOAD] ========== 验证完成 ==========
```

## 总结

✅ **编译保证**: 每次运行都强制重新编译  
✅ **失败停止**: 编译失败时立即停止执行  
✅ **修改验证**: 自动验证所有关键修改  
✅ **日志完整**: 详细的日志输出便于调试
