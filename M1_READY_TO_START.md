# M0 阶段服务启动验证指南（无 root 权限版）

**当前状态**: M0 已通过。postgres/keycloak/coturn/zlmediakit 已用本地镜像启动，`bash scripts/e2e.sh` 18/18 通过。可进入 M1 开发（需先通过 GATE A 提案并确认）。

---

## 快速启动步骤

### 1. 检查并启动 Docker

```bash
# 启动 Docker daemon（需要管理员权限）
sudo systemctl start docker

# 验证 Docker 运行
docker ps
```

### 2. 启动 M0 服务

```bash
cd /home/wqs/bigdata/Remote-Driving
docker-compose up -d
```

### 3. 验证服务

```bash
cd /home/wqs/bigdata/Remote-Driving/scripts
bash e2e.sh
```

---

## 常见问题

### Docker daemon 无法启动

```bash
# 检查 Docker 服务
sudo systemctl status docker

# 查看 Docker 日志
sudo journalctl -u docker -xe

# 重启 Docker
sudo systemctl restart docker
```

### 服务启动失败

```bash
# 查看详细日志
docker-compose logs [service_name]

# 重启服务
docker-compose restart [service_name]

# 删除并重新创建
docker-compose down -v
docker-compose up -d
```

### 端口被占用

```bash
# 检查端口占用
netstat -tuln | grep -E '5432|8080|80|3000|3478'

# 修改 docker-compose.yml 中的端口映射
```
---

**如果 Docker daemon 仍无法启动，请先执行上述修复步骤。**
