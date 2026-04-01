# Dev Container 重建与网络验证指南

## Executive Summary

本文档提供 Dev Container 重建步骤和网络连接验证方法。当前容器已配置 `--network=host` 模式，网络基本可用但需要验证。

---

## 1. 重建 Dev Container

### 方法一：通过命令面板（推荐）

1. **在 Cursor 中按 `F1` 或 `Ctrl+Shift+P`**
2. **输入并选择**：`Dev Containers: Rebuild Container`
3. **等待重建完成**（首次可能需要几分钟下载镜像）

### 方法二：通过命令面板（清理重建）

1. **按 `F1` 或 `Ctrl+Shift+P`**
2. **输入并选择**：`Dev Containers: Rebuild Container Without Cache`
3. **等待重建完成**

### 方法三：通过通知

如果 Cursor 检测到 `.devcontainer` 配置变更，会弹出通知：
- 点击 **"Rebuild Container"** 按钮

---

## 2. 验证网络配置

### 2.1 自动验证（容器启动后）

容器启动后会自动运行 `verify-network.sh`，检查：
- ✅ 网络模式（host vs bridge）
- ✅ 路由表配置
- ✅ DNS 配置
- ✅ 网络接口
- ✅ 网络连通性

### 2.2 手动验证

在容器内运行：

```bash
# 运行完整网络诊断
bash .devcontainer/verify-network.sh

# 测试基本网络连接
ping -c 3 8.8.8.8
ping -c 3 127.0.0.1

# 测试 DNS 解析
nslookup google.com
# 或
host google.com

# 测试 HTTP/HTTPS 连接
curl -I https://www.google.com
wget -O- https://www.google.com 2>&1 | head -5
```

### 2.3 检查网络接口

```bash
# 查看网络接口（应该看到主机的接口）
ip addr show
# 或
ls /sys/class/net/

# 检查路由表
cat /proc/net/route
```

---

## 3. 网络配置验证清单

### ✅ 应该看到的结果

1. **网络模式**
   ```
   ✓ No eth0 interface found (consistent with host network mode)
   ✓ Found X host network interface(s)
   ```

2. **路由表**
   ```
   ✓ Routing table found (X routes)
   ```

3. **DNS**
   ```
   ✓ DNS servers configured (X servers)
   ```

4. **网络连通性**
   ```
   ✓ Localhost (127.0.0.1) reachable
   ✓ Gateway (X.X.X.X) reachable
   ✓ External network (8.8.8.8) reachable
   ✓ DNS resolution working
   ```

### ⚠️ 如果看到警告

1. **"Found eth0 interface"**
   - 可能未使用 host 网络模式
   - 检查 `devcontainer.json` 中的 `runArgs` 是否包含 `--network=host`

2. **"External network not reachable"**
   - 检查宿主机网络连接
   - 检查防火墙设置
   - 验证 Docker 是否支持 host 网络模式

3. **"DNS resolution may not be working"**
   - 检查 `/etc/resolv.conf`
   - 测试 DNS 服务器：`nslookup google.com 8.8.8.8`

---

## 4. 常见问题排查

### Q1: 容器重建后网络仍然不工作

**检查项：**
```bash
# 1. 检查 devcontainer.json 配置
cat .devcontainer/devcontainer.json | grep -A 5 "runArgs"

# 2. 检查容器实际网络模式（在宿主机上）
docker inspect <container_id> | grep -A 10 "NetworkSettings"

# 3. 检查宿主机网络
ping -c 3 8.8.8.8  # 在宿主机上运行
```

### Q2: 网络工具缺失（ping, ip 等）

**解决：**
```bash
# 在容器内安装网络工具
apt-get update
apt-get install -y iputils-ping iproute2 curl wget dnsutils
```

### Q3: DNS 解析失败

**解决：**
```bash
# 检查 DNS 配置
cat /etc/resolv.conf

# 测试 DNS
nslookup google.com 8.8.8.8  # 使用 Google DNS

# 如果问题持续，手动设置 DNS
echo "nameserver 8.8.8.8" > /etc/resolv.conf
```

### Q4: 端口访问问题

**说明：**
- 使用 `--network=host` 时，容器内的服务直接绑定到主机端口
- 无需端口映射，直接使用 `localhost:端口` 访问

**验证：**
```bash
# 在容器内启动服务（例如监听 8080）
# 在宿主机访问：curl http://localhost:8080
```

---

## 5. 网络性能测试

### 5.1 延迟测试
```bash
# ICMP 延迟
ping -c 10 8.8.8.8

# HTTP 延迟
curl -o /dev/null -s -w "Time: %{time_total}s\n" https://www.google.com
```

### 5.2 带宽测试
```bash
# 下载测试
wget -O /dev/null https://speedtest.tele2.net/10MB.zip

# 或使用 curl
curl -o /dev/null https://speedtest.tele2.net/10MB.zip
```

---

## 6. 验证清单

重建容器后，确认以下项目：

- [ ] 容器成功启动
- [ ] 网络诊断脚本自动运行
- [ ] 能看到主机的网络接口（不是 eth0）
- [ ] localhost (127.0.0.1) 可达
- [ ] 外部网络 (8.8.8.8) 可达
- [ ] DNS 解析正常
- [ ] HTTP/HTTPS 连接正常
- [ ] 路由表配置正确

---

## 7. 下一步

网络验证通过后，可以：

1. **编译项目**
   ```bash
   cd /workspace/client
   ./build.sh
   ```

2. **运行应用**
   ```bash
   cd /workspace/client
   ./run.sh
   ```

3. **测试网络服务**
   - MQTT Broker: `localhost:1883`
   - ZLMediaKit: `http://localhost:8080`
   - WebRTC: `http://localhost:8080/index/api/webrtc`

---

## 8. 参考

- [Dev Container 配置说明](./README.md)
- [网络诊断脚本](./verify-network.sh)
- [Docker 网络模式文档](https://docs.docker.com/network/network-tutorial-host/)
