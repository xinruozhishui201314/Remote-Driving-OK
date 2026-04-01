# 启动崩溃修复说明（GDB/backtrace 定位）

## 问题现象

客户端启动后打印 "Using Chinese font: WenQuanYi Zen Hei" 即发生 Segmentation fault。

## 定位方式

因容器内 apt 安装 gdb 超时，在 `main.cpp` 中增加了 **SIGSEGV 时打印 backtrace** 的逻辑（`execinfo.h` + `backtrace_symbols`），并链接 `-rdynamic` 以便符号可读。

运行一次 `run.sh` 后得到堆栈：

```
=== Segmentation fault - backtrace ===
...
2: libQt6Network.so.6  QNetworkReply::error()
3: VehicleManager::onVehicleListReply(QNetworkReply*)
...
```

## 根因

- `VehicleManager::refreshVehicleList` 中：
  - 连接 `finished` 时用 lambda `[this]() { onVehicleListReply(m_currentReply); }`
  - 连接 `errorOccurred` 时在回调里执行 `m_currentReply->deleteLater(); m_currentReply = nullptr;`
- 出错时先触发 `errorOccurred`，把 `m_currentReply` 置为 `nullptr`，再触发 `finished`。
- `finished` 的 lambda 里仍用 `m_currentReply`，于是调用 `onVehicleListReply(nullptr)`，在 `reply->error()` 处崩溃。

此外，若在请求未完成时再次调用 `refreshVehicleList`，会 abort 旧请求并 `deleteLater`，此时旧请求的 `finished` 若仍用当前的 `m_currentReply`，会误把**新**请求的 reply 传给 `onVehicleListReply`，也会导致逻辑错误。

## 修改内容

### 1. `client/src/vehiclemanager.cpp`

- **finished 回调**：用“当前这次请求的 reply”作为捕获，不再用 `m_currentReply`：
  - `QNetworkReply *reply = m_currentReply;`
  - `connect(reply, &QNetworkReply::finished, this, [this, reply]() { onVehicleListReply(reply); });`
- **errorOccurred 回调**：同样用本次的 `reply` 做捕获，并只在“仍是当前请求”时清空 `m_currentReply`：
  - `connect(reply, &QNetworkReply::errorOccurred, this, [this, reply](...) { ... reply->deleteLater(); if (m_currentReply == reply) m_currentReply = nullptr; });`
- **onVehicleListReply**：开头增加空指针检查：
  - `if (!reply) return;`

### 2. 其他相关修改（此前已做）

- **qmldir**：去掉对不存在的 `webrtcclient.qml` 等文件的引用，避免模块加载异常。
- **main.cpp**：为 QML 引擎设置 `addImportPath(qmlDir)`，保证 `import RemoteDriving 1.0` 能解析。
- **main.cpp**：增加 SIGSEGV 时 backtrace 打印及 `-rdynamic` 链接，便于日后无 gdb 时也能定位崩溃。

## 验证

```bash
cd /workspaces/Remote-Driving/client
bash build.sh
timeout 6 bash run.sh   # 6 秒内未崩溃即视为通过（exit 124 为超时）
```

预期：能正常进入界面，不再出现 Segmentation fault。

## GDB 使用（若已安装 gdb）

```bash
cd /workspaces/Remote-Driving/client
./debug.sh              # 交互式 GDB
./debug.sh --bt         # 自动运行到崩溃并打印 bt full
```

`.devcontainer/setup.sh` 中已加入 `gdb` 安装，重建容器后即可使用。
