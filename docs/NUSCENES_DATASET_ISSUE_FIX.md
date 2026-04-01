# NuScenes 数据集推流问题分析与修复

## Executive Summary

**问题**：使用 NuScenes 数据集推流时，客户端连接后 ZLMediaKit 返回 "stream not found"。

**根本原因**：
1. 数据集目录未挂载或为空（`/data/sweeps` 目录不存在相机子目录）
2. 推流脚本跳过所有流但没有报错退出
3. 没有启动任何 FFmpeg 进程，因此 ZLMediaKit 上没有流

**解决方案**：
1. ✅ 改进推流脚本：检查是否至少启动了一个流，否则报错退出
2. ✅ 改进车端代码：检查推流脚本执行结果并输出警告
3. ✅ 修改默认配置：使用测试图案脚本作为默认（不依赖数据集）

---

## 1. 问题分析

### 1.1 症状

- 客户端发送 `start_stream` 消息
- 车端收到消息并执行推流脚本
- 但 ZLMediaKit 上没有流
- 客户端连接时返回 `-400 "stream not found"`

### 1.2 根本原因

**推流脚本执行流程**：
```bash
# 脚本检查每个相机目录
for stream_id in cam_front cam_rear cam_left cam_right; do
  cam_path="${SWEEPS_PATH}/${cam_dir}"
  if [[ ! -d "$cam_path" ]]; then
    echo "WARN: skip $stream_id"  # 只是警告，继续执行
    continue
  fi
  # 启动 FFmpeg...
done
```

**问题**：
- 如果所有相机目录都不存在，所有流都被跳过
- 脚本没有检查是否至少启动了一个流
- 脚本正常退出（返回码 0），但没有任何推流进程
- 车端代码没有检查推流是否成功启动

### 1.3 验证

```bash
# 检查数据集目录
docker exec remote-driving-vehicle-1 ls -la /data/sweeps/
# 结果：目录为空（只有 . 和 ..）

# 手动执行推流脚本
docker exec remote-driving-vehicle-1 bash /app/scripts/push-nuscenes-cameras-to-zlm.sh
# 结果：所有流都被跳过，没有启动 FFmpeg

# 检查推流进程
docker exec remote-driving-vehicle-1 ps aux | grep ffmpeg
# 结果：没有 FFmpeg 进程
```

---

## 2. 修复方案

### 2.1 改进推流脚本（已修复）

**文件**：`scripts/push-nuscenes-cameras-to-zlm.sh`

**修改**：
1. 添加流启动计数
2. 检查目录中是否有图片文件
3. 如果所有流都被跳过，报错退出

```bash
STREAMS_STARTED=0
for stream_id in cam_front cam_rear cam_left cam_right; do
  # ... 检查目录和文件 ...
  
  if [[ ! -d "$cam_path" ]]; then
    echo "WARN: skip $stream_id (目录不存在: $cam_path)"
    continue
  fi
  
  # 检查目录中是否有图片文件
  if ! ls "${cam_path}"/*.jpg >/dev/null 2>&1; then
    echo "WARN: skip $stream_id (目录中没有 .jpg 文件: $cam_path)"
    continue
  fi
  
  # 启动 FFmpeg...
  STREAMS_STARTED=$((STREAMS_STARTED + 1))
done

# 检查是否至少启动了一个流
if [ $STREAMS_STARTED -eq 0 ]; then
  echo "ERROR: 没有启动任何推流（数据集目录不存在或为空）"
  echo "  请检查:"
  echo "    1. SWEEPS_PATH=$SWEEPS_PATH 是否存在"
  echo "    2. 目录结构: SWEEPS_PATH/{CAM_FRONT,CAM_BACK,CAM_FRONT_LEFT,CAM_FRONT_RIGHT}/*.jpg"
  echo "    3. 或使用测试图案脚本: push-testpattern-to-zlm.sh"
  exit 1
fi
```

**效果**：
- ✅ 如果数据集不存在，脚本会报错退出
- ✅ 车端可以检测到推流脚本失败
- ✅ 提供清晰的错误提示

### 2.2 改进车端代码（已修复）

**文件**：`Vehicle-side/src/control_protocol.cpp`

**修改**：检查推流脚本执行结果并输出警告

```cpp
int r = std::system(cmd.c_str());
std::cout << "[Control] system() 返回: " << r << std::endl;
if (r != 0) {
    std::cerr << "[Control] 警告: 推流脚本执行可能失败 (返回码: " << r << ")" << std::endl;
    std::cerr << "[Control] 提示: 如果使用 NuScenes 数据集，请检查数据集路径是否正确挂载" << std::endl;
    std::cerr << "[Control] 提示: 或使用测试图案脚本: VEHICLE_PUSH_SCRIPT=/app/scripts/push-testpattern-to-zlm.sh" << std::endl;
}
```

**效果**：
- ✅ 车端日志会显示推流脚本执行结果
- ✅ 如果失败，会输出清晰的提示信息

### 2.3 修改默认配置（已修复）

**文件**：`docker-compose.vehicle.dev.yml`

**修改**：默认使用测试图案脚本（不依赖数据集）

```yaml
# 默认使用测试图案（不依赖数据集）
- VEHICLE_PUSH_SCRIPT=/app/scripts/push-testpattern-to-zlm.sh
# - VEHICLE_PUSH_SCRIPT=/app/scripts/push-nuscenes-cameras-to-zlm.sh  # 取消注释以使用 NuScenes 数据集
# - SWEEPS_PATH=/data/sweeps  # 仅在使用 NuScenes 数据集时需要
```

**效果**：
- ✅ 默认配置可以直接使用（不依赖数据集）
- ✅ 如果需要使用 NuScenes 数据集，只需取消注释并挂载数据集卷

---

## 3. 使用说明

### 3.1 使用测试图案（默认，推荐）

**配置**：`docker-compose.vehicle.dev.yml` 中已默认配置

```yaml
environment:
  - VEHICLE_PUSH_SCRIPT=/app/scripts/push-testpattern-to-zlm.sh
```

**优点**：
- ✅ 不依赖数据集
- ✅ 快速启动
- ✅ 适合开发和测试

### 3.2 使用 NuScenes 数据集

**步骤 1**：挂载数据集卷

修改 `docker-compose.vehicle.dev.yml`：
```yaml
volumes:
  - /实际路径/nuscenes-mini/sweeps:/data/sweeps:ro
```

**步骤 2**：配置推流脚本

修改 `docker-compose.vehicle.dev.yml`：
```yaml
environment:
  - VEHICLE_PUSH_SCRIPT=/app/scripts/push-nuscenes-cameras-to-zlm.sh
  - SWEEPS_PATH=/data/sweeps
```

**步骤 3**：重启车端容器

```bash
docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml restart vehicle
```

**验证**：
```bash
# 检查数据集目录
docker exec $(docker ps --format '{{.Names}}' | grep vehicle | head -1) ls -la /data/sweeps/

# 应该看到：
# CAM_FRONT/
# CAM_BACK/
# CAM_FRONT_LEFT/
# CAM_FRONT_RIGHT/
```

---

## 4. 故障排查

### 4.1 推流脚本报错退出

**错误信息**：
```
ERROR: 没有启动任何推流（数据集目录不存在或为空）
```

**排查步骤**：

1. **检查数据集路径**
   ```bash
   docker exec $(docker ps --format '{{.Names}}' | grep vehicle | head -1) ls -la /data/sweeps/
   ```

2. **检查数据集结构**
   ```bash
   docker exec $(docker ps --format '{{.Names}}' | grep vehicle | head -1) \
     bash -c 'for dir in CAM_FRONT CAM_BACK CAM_FRONT_LEFT CAM_FRONT_RIGHT; do
       echo "=== $dir ==="
       ls /data/sweeps/$dir/*.jpg 2>&1 | head -3
     done'
   ```

3. **检查环境变量**
   ```bash
   docker exec $(docker ps --format '{{.Names}}' | grep vehicle | head -1) \
     env | grep -E "SWEEPS_PATH|VEHICLE_PUSH_SCRIPT"
   ```

**解决**：
- 如果数据集不存在，使用测试图案脚本
- 如果数据集路径错误，修改 `docker-compose.vehicle.dev.yml` 中的 volumes 配置

### 4.2 车端日志显示警告

**日志信息**：
```
[Control] 警告: 推流脚本执行可能失败 (返回码: 1)
[Control] 提示: 如果使用 NuScenes 数据集，请检查数据集路径是否正确挂载
```

**解决**：
- 检查推流脚本的错误输出
- 检查数据集路径和结构
- 或切换到测试图案脚本

### 4.3 客户端仍然无法连接

**排查**：
```bash
# 检查 ZLMediaKit 流列表
curl "http://127.0.0.1:80/index/api/getMediaList?app=teleop"

# 检查推流进程
docker exec $(docker ps --format '{{.Names}}' | grep vehicle | head -1) ps aux | grep ffmpeg

# 检查车端日志
docker logs $(docker ps --format '{{.Names}}' | grep vehicle | head -1) | tail -50
```

---

## 5. 相关文件

- `scripts/push-nuscenes-cameras-to-zlm.sh` - NuScenes 推流脚本（已修复）
- `scripts/push-testpattern-to-zlm.sh` - 测试图案推流脚本
- `Vehicle-side/src/control_protocol.cpp` - 车端控制协议（已修复）
- `docker-compose.vehicle.dev.yml` - 车端开发配置（已修复）
- `docs/NUSCENES_DATASET_ISSUE_FIX.md` - 本文档

---

## 6. 总结

**已修复**：
1. ✅ 推流脚本检查是否启动流，否则报错退出
2. ✅ 车端代码检查推流脚本执行结果并输出警告
3. ✅ 默认配置使用测试图案脚本（不依赖数据集）

**推荐做法**：
- **开发和测试**：使用测试图案脚本（默认配置）
- **实际验证**：使用 NuScenes 数据集（需挂载数据集卷）

**下一步**：
1. 重启车端容器应用新配置
2. 验证推流是否正常启动
3. 客户端连接验证视频流
