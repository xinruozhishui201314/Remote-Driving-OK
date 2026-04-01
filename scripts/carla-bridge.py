#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
CARLA Bridge Script
功能：连接到 CARLA 服务器，生成车辆和传感器（摄像机），并将其挂载。
改造目标：使用结构化 JSON 日志替代 print 输出。
"""

import logging
import json
import sys
import time
import os

# 尝试导入 CARLA 客户端
try:
    import carla
except ImportError:
    print("Error: carla package not found. Please install carla client library.")
    sys.exit(1)

# ==========================
# 1. 结构化日志配置 (符合 project_spec.md §11)
# ==========================

class StructuredFormatter(logging.Formatter):
    """
    自定义格式化器，将日志输出为单行 JSON 字符串。
    格式：{"timestamp_us": ..., "level": "...", "node_id": "...", "msg": "..."}
    """
    def __init__(self, node_id="carla-bridge"):
        super().__init__()
        self.node_id = node_id

    def format(self, record):
        # 1. 获取微秒级时间戳
        timestamp_us = int(record.created * 1_000_000)
        
        # 2. 获取日志级别字符串
        level_str = record.levelname
        
        # 3. 获取消息
        msg_str = record.getMessage()

        # 4. 构建 JSON 对象
        # 注意：Python 的 logging 默认不记录异常堆栈到 msg，
        # 如果需要详细信息，可以在这里处理 exc_info
        log_obj = {
            "timestamp_us": timestamp_us,
            "level": level_str.upper(),
            "node_id": self.node_id,
            "msg": msg_str
        }

        # 5. 序列化为 JSON 字符串
        try:
            return json.dumps(log_obj, ensure_ascii=False)
        except Exception:
            # Fallback 防止 JSON 序列化错误导致死循环
            return json.dumps({
                "timestamp_us": timestamp_us,
                "level": level_str.upper(),
                "node_id": self.node_id,
                "msg": f"Log format error: {msg_str}"
            })

# 初始化 Logger
logger = logging.getLogger("carla-bridge")
logger.setLevel(logging.DEBUG) # 设置为最低级别，由 Handler 决定输出

# 创建控制台输出流处理器
console_handler = logging.StreamHandler(sys.stdout)
console_handler.setFormatter(StructuredFormatter(node_id="carla-bridge"))
logger.addHandler(console_handler)


# ==========================
# 2. CARLA Bridge 主逻辑
# ==========================

class CarlaBridge:
    def __init__(self, host='localhost', port=2000, timeout=10.0):
        self.host = host
        self.port = port
        self.timeout = timeout
        self.client = None
        self.world = None
        self.blueprint_library = None
        self.vehicle = None
        self.camera_list = []

    def connect(self):
        """连接到 CARLA 服务器"""
        logger.info(f"Connecting to CARLA server at {self.host}:{self.port}...")
        try:
            self.client = carla.Client(self.host, self.port, worker_threads=4)
            self.client.set_timeout(self.timeout)
            self.world = self.client.get_world()
            logger.info(f"Connected successfully. World: {self.world.get_id()}")
            self.blueprint_library = self.world.get_blueprint_library()
            return True
        except Exception as e:
            logger.error(f"Failed to connect to CARLA: {e}")
            return False

    def spawn_vehicle(self, blueprint_name='vehicle.tesla.model3', spawn_point_name='carla/SpawnPoint_0'):
        """生成车辆"""
        logger.info(f"Spawning vehicle: {blueprint_name} at {spawn_point_name}")
        try:
            # 获取生成点和蓝图
            spawn_point_obj = self.world.get_map().get_spawn_point(spawn_point_name)
            blueprint = self.blueprint_library.find(blueprint_name)
            
            # 生成车辆
            vehicle = self.world.spawn_actor(
                blueprint,
                spawn_point_obj.transform
            )
            logger.info(f"Vehicle spawned: {vehicle.id}")
            return vehicle
        except Exception as e:
            logger.error(f"Failed to spawn vehicle: {e}")
            return None

    def spawn_camera(self, vehicle, camera_type='sensor.camera.rgb', transform_offset=None):
        """生成并挂载摄像机"""
        logger.info(f"Spawning camera {camera_type}...")
        try:
            # 查找摄像机蓝图
            camera_bp = self.blueprint_library.find(camera_type)
            
            # 设置摄像机属性（例如：图像尺寸）
            camera_bp.set_attribute('image_size_x', '800')
            camera_bp.set_attribute('image_size_y', '600')

            # 相对于车辆的变换
            # 如果有 offset，则应用
            if transform_offset is None:
                # 默认放置在车辆顶部后上方
                transform_offset = carla.Transform(
                    location=carla.Location(x=-0.5, z=1.8),
                    rotation=carla.Rotation(yaw=180.0, pitch=0.0, roll=0.0)
                )

            # 将相对变换转换为绝对变换
            vehicle_transform = vehicle.get_transform()
            transform = vehicle_transform.transform(transform_offset)
            
            # 生成摄像机
            camera = self.world.spawn_actor(camera_bp, transform, attach_to=vehicle)
            logger.info(f"Camera spawned: {camera.id}")
            self.camera_list.append(camera)
            return camera
        except Exception as e:
            logger.error(f"Failed to spawn camera: {e}")
            return None

    def cleanup(self):
        """销毁生成的 Actor"""
        logger.info("Cleaning up spawned actors...")
        for actor in self.camera_list:
            if actor.is_alive:
                actor.destroy()
        if self.vehicle and self.vehicle.is_alive:
            self.vehicle.destroy()
        logger.info("Cleanup finished.")

    def run(self):
        """运行桥接循环"""
        if not self.connect():
            return

        # 生成车辆 (可选)
        self.vehicle = self.spawn_vehicle()
        if not self.vehicle:
            logger.warning("No vehicle available. Camera generation may fail.")
        
        # 生成摄像机
        # 这里生成 4 个 RGB 摄像机用于远程驾驶 (前, 后, 左, 右)
        offsets = [
            (1.2, 1.8, 0),   # Front
            (-1.2, 1.8, 180), # Back
            (0.0, 1.8, -90),  # Left
            (0.0, 1.8, 90)    # Right
        ]
        
        for x, z, yaw in offsets:
            transform_offset = carla.Transform(
                location=carla.Location(x=x, z=z),
                rotation=carla.Rotation(yaw=yaw)
            )
            self.spawn_camera(self.vehicle, transform_offset=transform_offset)

        logger.info("CARLA Bridge initialization complete. Keeping alive...")
        
        try:
            # 保持脚本运行
            while True:
                # 检查 Actor 存活状态
                alive_count = sum(1 for c in self.camera_list if c.is_alive)
                if self.vehicle:
                    alive_count += 1 if self.vehicle.is_alive else 0
                
                # 如果没有 Actor 存活，尝试重新连接或生成
                if alive_count == 0:
                    logger.warning("All actors destroyed. Re-spawning...")
                    self.vehicle = self.spawn_vehicle()
                    if self.vehicle:
                        for x, z, yaw in offsets:
                            transform_offset = carla.Transform(
                                location=carla.Location(x=x, z=z),
                                rotation=carla.Rotation(yaw=yaw)
                            )
                            self.spawn_camera(self.vehicle, transform_offset=transform_offset)
                
                time.sleep(1)
                
        except KeyboardInterrupt:
            logger.info("KeyboardInterrupt received. Shutting down...")
        finally:
            self.cleanup()

# ==========================
# 3. 主程序入口
# ==========================
if __name__ == "__main__":
    logger.info("=" * 50)
    logger.info("CARLA Bridge - Python")
    logger.info("JSON Structured Logging Enabled")
    logger.info("=" * 50)

    # 从环境变量读取参数
    CARLA_HOST = os.environ.get('CARLA_HOST', 'localhost')
    CARLA_PORT = int(os.environ.get('CARLA_PORT', 2000))

    # 启动桥接
    bridge = CarlaBridge(host=CARLA_HOST, port=CARLA_PORT)
    bridge.run()
