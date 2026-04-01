# 切换到 NuScenes 数据集推流

## Executive Summary

**已完成**：已将推流脚本从 `push-testpattern-to-zlm.sh` 切换为 `push-nuscenes-cameras-to-zlm.sh`。

**修改内容**：
1. ✅ 修改 `docker-compose.vehicle.dev.yml` 中的 `VEHICLE_PUSH_SCRIPT` 环境变量
2. ✅ 启用 `SWEEPS_PATH` 环境变量
3. ✅ 更新数据集卷挂载路径为实际路径
4. ✅ 重启车端容器应用新配置

---

## 1. 配置修改

### 1.1 推流脚本配置

**文件**：`docker-compose.vehicle.dev.yml`

**修改前**：
```yaml
- VEHICLE_PUSH_SCRIPT=/app/scripts/push-testpattern-to-zlm.sh
# - VEHICLE_PUSH_SCRIPT=/app/scripts/push-nuscenes-cameras-to-zlm.sh
# - SWEEPS_PATH=/data/sweeps
```

**修改后**：
```yaml
- VEHICLE_PUSH_SCRIPT=/app/scripts/push-nuscenes-cameras-to-zlm.sh
- SWEEPS_PATH=/data/sweeps
```

### 1.2 数据集卷挂载

**修改前**：
```yaml
volumes:
  - /path/to/nuscenes-mini/sweeps:/data/sweeps:ro
```

**修改后**：
```yaml
volumes:
  - /home/wqs/bigdata/data/nuscenes-mini/sweeps:/data/sweeps:ro
```

---

## 2. 验证配置

### 2.1 检查环境变量

```bash
docker exec remote-driving-vehicle-1 env | grep -E "VEHICLE_PUSH_SCRIPT|SWEEPS_PATH"
```

**预期输出**：
```
VEHICLE_PUSH_SCRIPT=/app/scripts/push-nuscenes-cameras-to-zlm.sh
SWEEPS_PATH=/data/sweeps
```

### 2.2 检查数据集目录

```bash
docker exec remote-driving-vehicle-1 ls -la /data/sweeps/
```

**预期输出**：
```
CAM_FRONT/
CAM_BACK/
CAM_FRONT_LEFT/
CAM_FRONT_RIGHT/
...
```

### 2.3 检查数据集文件

```bash
docker exec remote-driving-vehicle-1 bash -c 'for dir in CAM_FRONT CAM_BACK CAM_FRONT_LEFT CAM_FRONT_RIGHT; do
  echo "=== $dir ==="
  ls /data/sweeps/$dir/*.jpg 2>&1 | head -2
done'
```

**预期输出**：每个目录都应该有 `.jpg` 文件

---

## 3. 测试推流

### 3.1 手动测试推流脚本

```bash
# 在车端容器内手动执行推流脚本
docker exec remote-driving-vehicle-1 bash /app/scripts/push-nuscenes-cameras-to-zlm.sh
```

**预期结果**：
- ✅ 脚本正常启动
- ✅ 启动 4 个 FFmpeg 进程（cam_front, cam_rear, cam_left, cam_right）
- ✅ 输出类似：`All running (4 streams started). Ctrl+C to stop.`

### 3.2 通过客户端触发推流

1. **启动客户端**：
   ```bash
   bash scripts/start-full-chain.sh manual
   ```

2. **操作流程**：
   - 登录（123/123）
   - 选择车辆
   - 点击「连接车端」

3. **验证推流**：
   ```bash
   # 检查推流进程
   docker exec remote-driving-vehicle-1 ps aux | grep ffmpeg
   
   # 检查 ZLMediaKit 流列表
   curl "http://127.0.0.1:80/index/api/getMediaList?app=teleop"
   ```

---

## 4. 故障排查

### 4.1 推流脚本报错退出

**错误信息**：
```
ERROR: 没有启动任何推流（数据集目录不存在或为空）
```

**排查**：
```bash
# 检查数据集目录
docker exec remote-driving-vehicle-1 ls -la /data/sweeps/

# 检查环境变量
docker exec remote-driving-vehicle-1 env | grep SWEEPS_PATH

# 检查数据集结构
docker exec remote-driving-vehicle-1 bash -c 'ls /data/sweeps/CAM_FRONT/*.jpg 2>&1 | head -3'
```

**解决**：
- 确认数据集路径正确
- 确认数据集目录结构正确
- 确认卷挂载正确

### 4.2 推流进程未启动

**排查**：
```bash
# 检查车端日志
docker logs remote-driving-vehicle-1 | grep -E "start_stream|push|ffmpeg"

# 检查推流进程
docker exec remote-driving-vehicle-1 ps aux | grep ffmpeg

# 手动执行推流脚本查看错误
docker exec remote-driving-vehicle-1 bash /app/scripts/push-nuscenes-cameras-to-zlm.sh
```

---

## 5. 切换回测试图案

如果需要切换回测试图案脚本：

**修改** `docker-compose.vehicle.dev.yml`：
```yaml
environment:
  - VEHICLE_PUSH_SCRIPT=/app/scripts/push-testpattern-to-zlm.sh
  # - VEHICLE_PUSH_SCRIPT=/app/scripts/push-nuscenes-cameras-to-zlm.sh
  # - SWEEPS_PATH=/data/sweeps  # 不需要数据集时注释掉
```

**重启车端**：
```bash
docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml restart vehicle
```

---

## 6. 相关文件

- `docker-compose.vehicle.dev.yml` - 车端开发配置（已修改）
- `scripts/push-nuscenes-cameras-to-zlm.sh` - NuScenes 推流脚本
- `scripts/push-testpattern-to-zlm.sh` - 测试图案推流脚本
- `docs/SWITCH_TO_NUSCENES_STREAMING.md` - 本文档

---

## 7. 总结

**已完成**：
1. ✅ 推流脚本已切换为 `push-nuscenes-cameras-to-zlm.sh`
2. ✅ 数据集路径已配置并挂载
3. ✅ 车端容器已重启，新配置已生效

**下一步**：
1. 客户端连接车端触发推流
2. 验证四路视频流是否正常显示
3. 检查视频质量和延迟

**数据集路径**：`/home/wqs/bigdata/data/nuscenes-mini/sweeps`
