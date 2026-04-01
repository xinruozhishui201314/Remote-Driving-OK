# MQTT Broker 部署变更日志

## 2026-02-09 - 初始部署

### 新增功能

1. **MQTT Broker Docker 配置**
   - 集成到 `docker-compose.yml`
   - 支持多协议（MQTT/TLS/WebSocket）
   - 自动初始化脚本

2. **配置文件**
   - `mosquitto.conf` - 主配置文件
   - `passwd.example` - 密码文件示例
   - `acl.example` - ACL 文件示例

3. **初始化脚本**
   - `init-mosquitto.sh` - 自动创建用户、ACL、证书

4. **监控脚本**
   - `monitor-mosquitto.sh` - 自动监控和重启

5. **文档**
   - `README.md` - 详细部署指南
   - `docs/MQTT_BROKER_DEPLOYMENT.md` - 部署方案文档

### 异常处理

- ✅ 连接异常（自动检测和重启）
- ✅ 认证失败（日志记录）
- ✅ 消息丢失（QoS 配置）
- ✅ 连接断开（Keep-Alive 配置）
- ✅ 性能问题（资源限制）
- ✅ 磁盘空间不足（监控和告警）

### 安全配置

- ✅ TLS 加密（端口 8883）
- ✅ WebSocket 支持（端口 9001）
- ✅ 用户名/密码认证
- ✅ ACL 访问控制
- ✅ 禁用匿名连接

### 监控功能

- ✅ 健康检查（Docker）
- ✅ 自动重启（监控脚本）
- ✅ 日志记录（结构化日志）
- ✅ 资源监控（CPU/内存/磁盘）

---

## 后续计划

- [ ] Prometheus 监控集成
- [ ] Grafana 仪表盘
- [ ] 集群部署（高可用）
- [ ] 消息持久化到外部存储
