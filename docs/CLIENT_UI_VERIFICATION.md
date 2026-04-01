# 客户端 UI 功能验证指南

## 概述

本文档描述如何通过 UI 界面操作验证客户端的完整功能流程。

## 验证流程

### 1. 准备工作

#### 1.1 确保服务运行

```bash
# 检查后端服务
curl http://localhost:8081/health

# 检查 Keycloak 服务
curl http://localhost:8080/realms/teleop

# 如果未运行，启动服务
docker compose -f docker-compose.yml -f docker-compose.dev.yml up -d backend
docker compose up -d keycloak postgres
```

#### 1.2 编译客户端

```bash
# 启动客户端容器
docker compose up -d client-dev

# 编译客户端
docker compose exec client-dev bash -c "cd /workspace/client && bash build.sh"
```

### 2. UI 验证步骤

#### 步骤 1：启动客户端

```bash
# 方式 A：在容器内运行（需要 X11 支持）
xhost +local:docker
docker compose exec -e DISPLAY=$DISPLAY client-dev bash -c "cd /workspace/client && bash run.sh"

# 方式 B：使用验证脚本
bash scripts/test-client-ui.sh
```

#### 步骤 2：登录验证

1. **打开登录对话框**
   - 客户端启动后应该自动显示登录对话框
   - 如果没有显示，点击菜单栏"文件" → "退出登录"后重新打开

2. **输入登录信息**
   - 用户名：`e2e-test`
   - 密码：`e2e-test-password`
   - 服务器地址：`http://localhost:8081`（如果未自动填充）

3. **点击登录按钮**
   - ✅ 应该显示"登录成功"提示
   - ✅ 登录对话框应该自动关闭
   - ✅ 应该自动打开车辆选择对话框

#### 步骤 3：车辆选择验证

1. **查看车辆列表**
   - ✅ 应该能看到至少一个 VIN：`E2ETESTVIN0000001`
   - ✅ 如果没有显示，点击"刷新列表"按钮

2. **选择车辆**
   - ✅ 点击车辆列表中的"选择"按钮
   - ✅ 或者点击车辆行
   - ✅ "当前车辆"区域应该显示选中的 VIN

3. **创建会话**
   - ✅ 点击"创建会话"按钮
   - ✅ 按钮应该显示加载状态（禁用）
   - ✅ 等待 1-2 秒后，应该显示会话信息：
     - Session ID（UUID 格式）
     - WHIP URL（格式：`whip://zlmediakit:80/index/api/webrtc?...`）
     - WHEP URL（格式：`whep://zlmediakit:80/index/api/webrtc?...`）
     - 控制协议：`HMAC-SHA256`

4. **验证会话信息显示**
   - ✅ "会话信息" GroupBox 应该显示
   - ✅ 所有字段都应该有值
   - ✅ URL 格式正确

#### 步骤 4：进入主界面

1. **确认并进入驾驶**
   - ✅ 点击"确认并进入驾驶"按钮
   - ✅ 车辆选择对话框应该关闭
   - ✅ 应该显示主驾驶界面（视频区域 + 控制面板）

2. **验证主界面**
   - ✅ 窗口标题应该显示：`远程驾驶客户端 - E2ETESTVIN0000001`
   - ✅ 左侧应该显示视频区域（VideoView）
   - ✅ 右侧应该显示控制面板（ControlPanel）
   - ✅ 顶部应该显示状态栏（StatusBar）

### 3. 功能验证清单

#### ✅ 登录功能

- [ ] 登录对话框正常显示
- [ ] 输入用户名和密码
- [ ] 点击登录按钮
- [ ] 登录成功提示
- [ ] 自动打开车辆选择对话框

#### ✅ 车辆列表功能

- [ ] 车辆列表正常显示
- [ ] 显示 VIN 信息
- [ ] "刷新列表"按钮正常工作
- [ ] 可以选择车辆
- [ ] "当前车辆"区域显示选中车辆

#### ✅ 会话创建功能

- [ ] "创建会话"按钮可用（已选择车辆时）
- [ ] 点击"创建会话"按钮
- [ ] 按钮显示加载状态
- [ ] 会话信息正确显示：
  - [ ] Session ID
  - [ ] WHIP URL
  - [ ] WHEP URL
  - [ ] 控制协议

#### ✅ 主界面功能

- [ ] 主界面正常显示
- [ ] 窗口标题正确
- [ ] 视频区域显示
- [ ] 控制面板显示
- [ ] 状态栏显示

### 4. 错误处理验证

#### 测试场景 1：未选择车辆时创建会话

- [ ] "创建会话"按钮应该禁用
- [ ] 提示信息应该显示"请先选择车辆"

#### 测试场景 2：网络错误

- [ ] 停止后端服务
- [ ] 尝试创建会话
- [ ] 应该显示错误信息："创建会话失败: 网络错误: ..."

#### 测试场景 3：认证失败

- [ ] 使用错误的 Token
- [ ] 尝试刷新车辆列表
- [ ] 应该显示错误信息

### 5. 验证脚本

使用自动化验证脚本：

```bash
# 检查环境和编译状态
bash scripts/test-client-ui.sh
```

### 6. 常见问题

#### Q1: 客户端无法启动

**检查**：
```bash
# 检查容器状态
docker compose ps client-dev

# 检查编译状态
docker compose exec client-dev bash -c "ls -lh /workspace/client/build/RemoteDrivingClient"

# 查看日志
docker compose logs client-dev
```

#### Q2: UI 无法显示

**解决**：
```bash
# 设置 X11 权限
xhost +local:docker

# 检查 DISPLAY 环境变量
echo $DISPLAY

# 使用 DISPLAY 环境变量运行
docker compose exec -e DISPLAY=$DISPLAY client-dev bash -c "cd /workspace/client && bash run.sh"
```

#### Q3: 无法连接到后端

**检查**：
```bash
# 检查后端服务
curl http://localhost:8081/health

# 检查网络连接
docker compose exec client-dev bash -c "curl http://host.docker.internal:8081/health"
```

#### Q4: 会话创建失败

**检查**：
```bash
# 检查后端日志
docker compose -f docker-compose.yml -f docker-compose.dev.yml logs backend --tail=20

# 手动测试 API
bash scripts/test-client-backend-integration.sh
```

### 7. 验证报告模板

```
## 客户端 UI 功能验证报告

**验证时间**：YYYY-MM-DD HH:MM:SS
**验证环境**：Docker 容器内运行
**Qt 版本**：6.8.0

### 验证结果

#### 登录功能
- [ ] 登录对话框显示正常
- [ ] 登录成功
- [ ] 自动打开车辆选择对话框

#### 车辆选择功能
- [ ] 车辆列表显示正常
- [ ] 车辆选择正常
- [ ] 刷新功能正常

#### 会话创建功能
- [ ] 创建会话按钮可用
- [ ] 会话创建成功
- [ ] 会话信息显示正确：
  - [ ] Session ID: <uuid>
  - [ ] WHIP URL: <url>
  - [ ] WHEP URL: <url>
  - [ ] 控制协议: HMAC-SHA256

#### 主界面功能
- [ ] 主界面显示正常
- [ ] 窗口标题正确
- [ ] 各组件正常显示

### 问题记录

1. <问题描述>
   - 原因：<原因>
   - 解决：<解决方案>

### 总结

✅ 所有功能验证通过 / ⚠️ 部分功能需要修复
```

## 相关文档

- `docs/CLIENT_SESSION_CREATION_STATUS.md`：会话创建功能开发状态
- `scripts/test-client-backend-integration.sh`：后端 API 验证脚本
- `scripts/test-client-ui.sh`：客户端 UI 验证脚本
