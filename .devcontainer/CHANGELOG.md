# Dev Container 配置变更日志

## 2026-02-02 - 网络配置修复与持久化

### 修改的文件

1. **devcontainer.json**
   - ✅ 添加 `--ipc=host` 和 `--pid=host` 参数
   - ✅ 更新 `postCreateCommand` 自动安装工具和运行网络诊断
   - ✅ 已保存

2. **pre-start.sh**
   - ✅ 修复 Docker 服务检查失败导致脚本退出问题
   - ✅ 添加网络配置检查步骤
   - ✅ 已保存

3. **setup.sh**
   - ✅ 添加网络工具安装（iputils-ping, iproute2, dnsutils）
   - ✅ 确保每次容器启动时自动安装
   - ✅ 已保存

4. **verify-network.sh** (新增)
   - ✅ 创建网络诊断脚本
   - ✅ 已保存

### 持久化说明

**配置文件持久化**: ✅ 所有配置文件已保存到 `.devcontainer/` 目录
- devcontainer.json
- pre-start.sh
- setup.sh
- verify-network.sh

**容器内软件包持久化**: ✅ 通过 `postCreateCommand` 自动安装
- 每次容器启动时，`setup.sh` 会自动安装网络工具
- 无需手动安装，配置已持久化

### 验证方法

重建容器后，运行：
```bash
# 检查网络工具是否已安装
which ping ip nslookup

# 运行网络诊断
bash .devcontainer/verify-network.sh
```

### 下次重启容器

1. 配置文件会自动应用（devcontainer.json）
2. 网络工具会自动安装（setup.sh）
3. 网络诊断会自动运行（postCreateCommand）

**无需手动操作！** ✅
