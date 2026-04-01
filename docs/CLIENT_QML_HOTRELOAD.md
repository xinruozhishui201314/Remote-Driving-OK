# 客户端 QML 修改生效说明

## 1. 修改不生效的常见原因

| 原因 | 处理 |
|------|------|
| **未重启客户端** | QML 在启动时加载，修改后需关闭窗口并重新启动 |
| **Qt QML 缓存** | 可设置 `QML_DISABLE_DISK_CACHE=1` 禁用磁盘缓存 |

## 2. 修改后生效步骤

1. 保存宿主机上的 `client/qml/*.qml` 修改
2. **关闭**客户端窗口（若正在运行）
3. 重新启动客户端

## 3. C++ 修改

C++ 源码（如 `videorenderer.cpp`）修改后需**重新编译**：

```bash
docker compose exec client-dev bash -c 'cd /tmp/client-build && make -j4'
```
