# 快速启动与操作指南

## 一键启动

```bash
make e2e-full
```

或

```bash
bash scripts/start-full-chain.sh manual
```

---

## 客户端操作步骤

### 1. 登录
- **用户名**：`123`
- **密码**：`123`
- 点击「登录」

### 2. 选择车辆
- 在车辆列表中选择一个车辆
- 点击「确认」

### 3. 连接车端
- 在驾驶界面顶部找到「**连接车端**」按钮
- 点击按钮
- 等待 2-5 秒

### 4. 验证视频
- 检查四路视频窗口是否显示画面
- 确认画面清晰流畅

---

## 关键按钮位置

**「连接车端」按钮**：
- 位置：驾驶界面顶部状态栏右侧
- 状态显示：
  - `连接车端` → 未连接
  - `MQTT已连接` → MQTT 已连接，等待视频流
  - `连接中...` → 正在连接
  - `已连接` → 视频流已连接 ✅

---

## 故障排查

### 客户端窗口未打开
```bash
export DISPLAY=:0
xhost +local:docker
bash scripts/run-e2e.sh client
```

### 视频无画面
```bash
# 检查车端日志
docker logs $(docker ps --format '{{.Names}}' | grep vehicle | head -1) | grep start_stream

# 检查推流进程
docker exec $(docker ps --format '{{.Names}}' | grep vehicle | head -1) ps aux | grep ffmpeg

# 检查 ZLMediaKit 流列表
curl "http://127.0.0.1:80/index/api/getMediaList?app=teleop"
```

---

## 详细文档

- `docs/START_FULL_SYSTEM_GUIDE.md` - 完整启动指南
- `docs/CLIENT_UI_OPERATION_GUIDE.md` - 客户端操作详细指南
