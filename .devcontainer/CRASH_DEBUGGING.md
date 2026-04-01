# 启动崩溃调试记录

## 问题现象

程序在加载 QML 后立即崩溃（Segmentation fault），发生在打印字体信息之后。

## 已完成的修复

### 1. 修复了重复的 Component.onCompleted
- **问题**：main.qml 中有两个 `Component.onCompleted`，导致 "Property value set multiple times" 错误
- **修复**：合并为一个 `Component.onCompleted`

### 2. 添加了空值检查
- 为所有 context property 访问添加了 `typeof` 检查
- 修改的文件：
  - `main.qml` - title 延迟计算，所有 context property 访问添加检查
  - `VideoView.qml` - 所有 `webrtcClient` 访问添加检查
  - `StatusBar.qml` - 所有 context property 访问添加检查
  - `ControlPanel.qml` - 所有 context property 访问添加检查

### 3. 延迟初始化
- 增加了初始化延迟时间（从 100ms 到 500ms）
- 使用 `componentsReady` 属性延迟显示主组件

### 4. 添加了调试输出
- 在 `main.cpp` 中添加了详细的调试输出，确认：
  - ✅ 所有 C++ 对象创建成功
  - ✅ 所有信号连接成功
  - ✅ 所有 context properties 注册成功
  - ✅ QML 文件加载成功

## 当前状态

程序仍然在 QML 加载后崩溃，发生在：
1. QML 文件加载成功
2. 字体信息打印成功
3. 然后立即崩溃（Segmentation fault）

## 可能的原因

1. **组件创建时访问未初始化的属性**：虽然添加了空值检查，但某些组件可能在创建时立即访问了 context properties，导致崩溃。

2. **C++ 对象生命周期问题**：虽然使用了 `std::make_unique` 和 `QObject` 的 parent-child 关系，但可能存在某些对象的生命周期问题。

3. **QML 引擎内部问题**：可能是 Qt/QML 引擎的 bug 或配置问题。

## 下一步调试建议

### 方案 1：使用 gdb 调试
```bash
cd /workspaces/Remote-Driving/client/build
gdb ./RemoteDrivingClient
run
# 等待崩溃
bt  # 查看堆栈跟踪
```

### 方案 2：逐步注释组件
创建一个最小化的 `main.qml`，只包含 `ApplicationWindow` 和 `LoginDialog`，逐步添加其他组件，定位导致崩溃的组件。

### 方案 3：检查 Qt 版本兼容性
确认 Qt 6.8.0 与当前代码的兼容性，可能需要降级或升级 Qt 版本。

### 方案 4：使用 Qt Creator 调试
在 Qt Creator 中打开项目，使用调试器定位崩溃点。

## 临时解决方案

如果急需运行程序，可以：
1. 创建一个最小化的 QML 文件，只显示登录对话框
2. 登录成功后再加载主界面组件
3. 使用 `Loader` 动态加载组件，而不是直接创建

## 已修改的文件

1. `client/qml/main.qml` - 合并 Component.onCompleted，添加空值检查，延迟初始化
2. `client/qml/VideoView.qml` - 添加空值检查
3. `client/qml/StatusBar.qml` - 添加空值检查
4. `client/qml/ControlPanel.qml` - 添加空值检查
5. `client/src/main.cpp` - 添加调试输出

## 编译和运行

```bash
cd /workspaces/Remote-Driving/client
bash build.sh
bash run.sh
```

## 注意事项

- 所有修改已保存
- 编译成功，无错误
- 程序在运行时崩溃，需要进一步调试
