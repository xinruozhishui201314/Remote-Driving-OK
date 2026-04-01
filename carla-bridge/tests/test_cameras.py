#!/usr/bin/env python3
"""四路相机相关逻辑的单元测试：to_bgr、CAMERA_CONFIGS、run_ffmpeg_pusher 行为。"""

import unittest
import threading
import queue
import sys

# 从上层目录导入
sys.path.insert(0, __import__("pathlib").Path(__file__).resolve().parents[1].as_posix())

try:
    import numpy as np
    HAS_NUMPY = True
except ImportError:
    HAS_NUMPY = False

from carla_bridge import to_bgr, CAMERA_CONFIGS, run_ffmpeg_pusher


@unittest.skipIf(not HAS_NUMPY, "numpy 未安装，跳过 to_bgr 测试")
class TestToBgr(unittest.TestCase):
    """测试 CARLA BGRA 图像转 BGR numpy。"""

    def test_shape_and_dtype(self):
        w, h = 640, 480
        # 模拟 CARLA Image：BGRA，每像素 4 字节
        raw = np.zeros((h * w * 4,), dtype=np.uint8)
        raw[0::4] = 100  # B
        raw[1::4] = 101  # G
        raw[2::4] = 102  # R
        raw[3::4] = 255  # A
        mock = type("Image", (), {"raw_data": raw.tobytes(), "width": w, "height": h})()

        bgr = to_bgr(mock)

        self.assertEqual(bgr.shape, (h, w, 3))
        self.assertEqual(bgr.dtype, np.uint8)
        self.assertEqual(bgr[0, 0, 0], 100)
        self.assertEqual(bgr[0, 0, 1], 101)
        self.assertEqual(bgr[0, 0, 2], 102)

    def test_contiguous_copy(self):
        w, h = 2, 2
        raw = np.arange(w * h * 4, dtype=np.uint8)
        mock = type("Image", (), {"raw_data": raw.tobytes(), "width": w, "height": h})()

        bgr = to_bgr(mock)

        self.assertTrue(bgr.flags["C_CONTIGUOUS"])
        self.assertEqual(bgr.shape, (2, 2, 3))


class TestCameraConfigs(unittest.TestCase):
    """测试四路相机配置。"""

    def test_four_cameras(self):
        self.assertEqual(len(CAMERA_CONFIGS), 4)

    def test_stream_ids(self):
        ids = [c[0] for c in CAMERA_CONFIGS]
        self.assertEqual(sorted(ids), ["cam_front", "cam_left", "cam_rear", "cam_right"])

    def test_each_has_xyzyaw(self):
        for cfg in CAMERA_CONFIGS:
            self.assertEqual(len(cfg), 5, msg=str(cfg))
            stream_id, x, y, z, yaw = cfg
            self.assertIsInstance(stream_id, str)
            self.assertIsInstance(x, (int, float))
            self.assertIsInstance(y, (int, float))
            self.assertIsInstance(z, (int, float))
            self.assertIsInstance(yaw, (int, float))


class TestRunFfmpegPusher(unittest.TestCase):
    """测试推流 worker：收到 None 后正常退出；无 ffmpeg 时跳过。"""

    def test_exits_on_none(self):
        """队列收到 None 时线程退出（mock Popen 避免调用真实 ffmpeg）。"""
        from unittest.mock import patch, MagicMock

        q = queue.Queue()
        stop = threading.Event()
        q.put(None)

        mock_proc = MagicMock()
        mock_proc.stdin.write = MagicMock()
        mock_proc.stdin.close = MagicMock()
        mock_proc.wait = MagicMock(return_value=0)

        with patch("carla_bridge.subprocess.Popen", return_value=mock_proc):
            run_ffmpeg_pusher(
                "test_cam",
                q,
                "rtmp://127.0.0.1:1935/teleop/test",
                64,
                48,
                10,
                stop,
            )
        self.assertTrue(True)

    def test_exits_on_stop_event(self):
        """stop_event 置位后线程能退出（配合超时取帧）。"""
        import subprocess
        from unittest.mock import patch, MagicMock

        q = queue.Queue()
        stop = threading.Event()

        mock_proc = MagicMock()
        mock_proc.stdin = MagicMock()
        mock_proc.wait = MagicMock(return_value=0)

        def fake_popen(*args, **kwargs):
            return mock_proc

        with patch("carla_bridge.subprocess.Popen", side_effect=fake_popen):
            t = threading.Thread(
                target=run_ffmpeg_pusher,
                args=("test", q, "rtmp://x/y", 64, 48, 10, stop),
            )
            t.start()
            stop.set()
            t.join(timeout=3)
        self.assertFalse(t.is_alive())


if __name__ == "__main__":
    unittest.main()
