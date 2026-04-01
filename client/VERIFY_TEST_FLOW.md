# 测试流程验证与 Debug 定位

## 测试账号与车辆

| 项目     | 值          |
|----------|-------------|
| 账户名   | `123`       |
| 密码     | `123`       |
| 测试 VIN | `123456789` |

登录使用 123/123 时，程序会注入一辆测试车辆（VIN: 123456789，名称: 测试车辆），在车辆选择界面可直接选择该车并进入远程驾驶主页面。

---

## 验证步骤（手动）

1. **启动客户端**（Debug 构建）
   ```bash
   cd /workspaces/Remote-Driving/client
   BUILD_TYPE=Debug bash build.sh
   bash run.sh
   ```

2. **登录**
   - 账户名输入 `123`，密码输入 `123`
   - 点击「登录」

3. **选择车辆**
   - 车辆选择对话框打开后，列表中应出现「测试车辆」VIN: 123456789
   - 点击该车辆行的「选择」，再点击「确认并进入驾驶」

4. **进入主页面**
   - 应进入远程驾驶主页面（视频区 + 控制面板 + 状态栏）
   - 标题栏应显示「远程驾驶客户端 - 测试车辆」

---

## 一键验证脚本

```bash
cd /workspaces/Remote-Driving/client
bash verify-and-run.sh
```

会先以 **Debug** 模式构建，再启动客户端，按上述步骤操作即可验证。

---

## Debug 模式精确定位问题

### 1. 保证 Debug 构建

```bash
cd /workspaces/Remote-Driving/client
export BUILD_TYPE=Debug
bash build.sh
```

或直接使用 `verify-and-run.sh`（内部已设置 `BUILD_TYPE=Debug`）。

### 2. 使用 GDB 运行（崩溃时看堆栈）

```bash
# 自动运行到崩溃并打印 bt full
bash verify-and-run.sh --gdb
```

或：

```bash
bash build.sh   # 确保 Debug 已构建
bash debug.sh --bt
```

崩溃时会输出 `bt full`，便于精确定位崩溃位置。

### 3. 交互式 GDB（单步、断点）

若已安装 gdb：

```bash
cd /workspaces/Remote-Driving/client
bash debug.sh
```

在 GDB 内可：

- `b VehicleManager::onVehicleListReply` 等设断点
- `run` 运行
- `bt` / `bt full` 查看堆栈
- `info locals` 查看局部变量

### 4. 程序内 backtrace（无 GDB 时）

当前已启用：发生 SIGSEGV 时会在 stderr 打印 backtrace（需 Debug 构建 + `-rdynamic`）。直接运行 `run.sh` 崩溃时也会看到堆栈。

---

## 常见问题

| 现象           | 可能原因                     | 处理                     |
|----------------|------------------------------|--------------------------|
| 车辆列表为空   | 未用 123/123 登录            | 用 123/123 登录          |
| 选车后无主页面 | 未点「确认并进入驾驶」       | 选车后点「确认并进入驾驶」 |
| 崩溃无符号     | 未用 Debug 构建              | `BUILD_TYPE=Debug bash build.sh` |
| 需精确定位     | 需 GDB 或 backtrace          | `verify-and-run.sh --gdb` 或看 stderr backtrace |
