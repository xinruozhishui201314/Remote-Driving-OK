#!/usr/bin/env python3
"""
四路相机集成验证：连接 CARLA → spawn 车辆 → spawn 四路相机 → 每路收一帧 → 检查形状与数量 → 清理。
需 CARLA 已启动（如 docker run -p 2000-2002:2000-2002 carlasim/carla:latest）。

用法：
  export CARLA_HOST=127.0.0.1   # 可选
  python verify_cameras.py
退出码：0 表示四路均收到一帧且形状正确；非 0 表示失败。
"""

import os
import sys
import time

CARLA_HOST = os.environ.get("CARLA_HOST", "127.0.0.1")
CARLA_PORT = int(os.environ.get("CARLA_PORT", "2000"))
CAMERA_WIDTH = int(os.environ.get("CAMERA_WIDTH", "640"))
CAMERA_HEIGHT = int(os.environ.get("CAMERA_HEIGHT", "480"))

# 与 carla_bridge 一致（左右 yaw：外视 -90 / +90，见 carla_bridge.py 注释）
CAMERA_CONFIGS = [
    ("cam_front", 2.5, 0.0, 1.2, 0),
    ("cam_rear", -2.5, 0.0, 1.2, 180),
    ("cam_left", 0.0, -1.2, 1.2, -90),
    ("cam_right", 0.0, 1.2, 1.2, 90),
]


def to_bgr(carla_image):
    import numpy as np
    arr = np.frombuffer(carla_image.raw_data, dtype=np.uint8)
    arr = arr.reshape((carla_image.height, carla_image.width, 4))
    return arr[:, :, :3].copy()


def main():
    try:
        import carla
    except ImportError:
        print("请安装: pip install carla numpy", file=sys.stderr)
        return 1
    try:
        import numpy as np
    except ImportError:
        print("请安装: pip install numpy", file=sys.stderr)
        return 1

    import threading

    received = {}
    events = {sid: threading.Event() for sid, _, _, _, _ in CAMERA_CONFIGS}

    print(f"[CARLA] 连接 {CARLA_HOST}:{CARLA_PORT} ...")
    client = carla.Client(CARLA_HOST, CARLA_PORT)
    client.set_timeout(10.0)
    world = client.get_world()
    blueprint_library = world.get_blueprint_library()
    vehicle_bp = blueprint_library.filter("vehicle.tesla.model3")[0]
    spawn_points = world.get_map().get_spawn_points()
    if not spawn_points:
        print("[CARLA] 无 spawn 点", file=sys.stderr)
        return 1

    vehicle = world.spawn_actor(vehicle_bp, spawn_points[0])
    cameras = []
    try:
        cam_bp = blueprint_library.find("sensor.camera.rgb")
        cam_bp.set_attribute("image_size_x", str(CAMERA_WIDTH))
        cam_bp.set_attribute("image_size_y", str(CAMERA_HEIGHT))
        cam_bp.set_attribute("fov", "90")

        for stream_id, x, y, z, yaw in CAMERA_CONFIGS:
            transform = carla.Transform(
                carla.Location(x=x, y=y, z=z),
                carla.Rotation(pitch=0, yaw=yaw, roll=0),
            )
            cam = world.spawn_actor(cam_bp, transform, attach_to=vehicle)
            cameras.append(cam)
            ev = events[stream_id]

            def make_cb(sid, e):
                def cb(image):
                    try:
                        received[sid] = to_bgr(image)
                        e.set()
                    except Exception:
                        pass
                return cb

            cam.listen(make_cb(stream_id, ev))
            print(f"  已挂载: {stream_id}")

        # 等待每路至少一帧，超时 15 秒
        timeout = 15
        deadline = time.monotonic() + timeout
        all_ok = True
        for stream_id, _, _, _, _ in CAMERA_CONFIGS:
            ev = events[stream_id]
            remain = max(0, deadline - time.monotonic())
            if not ev.wait(timeout=remain):
                print(f"[FAIL] {stream_id} 未在 {timeout}s 内收到帧", file=sys.stderr)
                all_ok = False
            elif stream_id not in received:
                print(f"[FAIL] {stream_id} 未写入 received", file=sys.stderr)
                all_ok = False
            else:
                frame = received[stream_id]
                if frame.shape != (CAMERA_HEIGHT, CAMERA_WIDTH, 3) or frame.dtype != np.uint8:
                    print(
                        f"[FAIL] {stream_id} 形状/类型异常: shape={frame.shape} dtype={frame.dtype}",
                        file=sys.stderr,
                    )
                    all_ok = False
                else:
                    print(f"[OK] {stream_id} 收到一帧 {frame.shape} BGR")

        if all_ok:
            print("四路相机验证通过")
            return 0
        return 1
    finally:
        for cam in cameras:
            try:
                cam.destroy()
            except Exception:
                pass
        try:
            vehicle.destroy()
        except Exception:
            pass


if __name__ == "__main__":
    sys.exit(main())
