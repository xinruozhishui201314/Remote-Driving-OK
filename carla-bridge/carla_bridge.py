#!/usr/bin/env python3
"""
CARLA Bridge：订阅 MQTT vehicle/control，控制 CARLA 车辆，发布 vehicle/status，
收到 start_stream 后四路相机推流到 ZLM，实现远程驾驶仿真闭环。

用法：
  export CARLA_HOST=127.0.0.1 MQTT_BROKER=127.0.0.1
  python carla_bridge.py
"""
# 立即输出启动标记，便于诊断 Bridge 是否被 exec（无缓冲）
import sys
sys.stdout.write("[carla-bridge] ========== Python Bridge 启动入口 ==========\n")
sys.stdout.flush()

import os
import sys
import json
import math
import time
import threading
import subprocess
import queue
import atexit

# 统一日志前缀：ISO8601 + [carla-bridge:TAG]，便于 grep 与问题分析（见 docs/LOGGING_BEST_PRACTICES.md）
def _ts_iso8601():
    from datetime import datetime, timezone
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%S.%f")[:-3] + "Z"

def _log(tag, msg, *args):
    ts = _ts_iso8601()
    text = (msg % args) if args else msg
    line = f"{ts} [carla-bridge:{tag}] {text}"
    try:
        print(line, flush=True)
    except UnicodeEncodeError:
        print(line.encode("ascii", "replace").decode("ascii"), flush=True)

def _err(tag, msg, *args):
    """错误输出，直接打印到 stderr 并记录，便于 grep 与问题分析。"""
    import sys, traceback as _tb
    ts = _ts_iso8601()
    text = (msg % args) if args else msg
    exc_line = ""
    try:
        exc_line = "".join(_tb.format_exception(*sys.exc_info()))
    except Exception:
        pass
    line = f"{ts} [carla-bridge:ERROR] [{tag}] {text}"
    try:
        print(line, flush=True, file=sys.stderr)
        if exc_line:
            print(exc_line, flush=True, file=sys.stderr)
    except Exception:
        pass

def _warn(tag, msg, *args):
    """告警输出，便于排查异常；与 _log 同格式，前缀 WARN。"""
    ts = _ts_iso8601()
    text = (msg % args) if args else msg
    line = f"{ts} [carla-bridge:WARN] [{tag}] {text}"
    try:
        print(line, flush=True)
    except UnicodeEncodeError:
        print(line.encode("ascii", "replace").decode("ascii"), flush=True)

def log_carla(msg, *args):
    _log("CARLA", msg, *args)

def log_mqtt(msg, *args):
    _log("MQTT", msg, *args)

def log_zlm(msg, *args):
    """车端仿真推流 → ZLM（RTMP），与客户端 [Client][WebRTC] 对齐的可 grep 前缀。"""
    ts = _ts_iso8601()
    text = (msg % args) if args else msg
    line = f"{ts} [CARLA-Bridge][ZLM][Push] {text}"
    try:
        print(line, flush=True)
    except UnicodeEncodeError:
        print(line.encode("ascii", "replace").decode("ascii"), flush=True)

def log_control(msg, *args):
    _log("Control", msg, *args)

# 启动阶段日志（便于精准定位卡点）
def _log_startup():
    import os as _os
    _h = _os.environ.get("CARLA_HOST", "127.0.0.1")
    _p = _os.environ.get("CARLA_PORT", "2000")
    _m = _os.environ.get("MQTT_BROKER", "127.0.0.1")
    _s = _os.environ.get("SPECTATOR_VIEW_MODE", "driver")
    print(f"[carla-bridge] 环境 CARLA_HOST={_h} CARLA_PORT={_p} MQTT_BROKER={_m} SPECTATOR_VIEW_MODE={_s}", flush=True)

CARLA_HOST = os.environ.get("CARLA_HOST", "127.0.0.1")
CARLA_PORT = int(os.environ.get("CARLA_PORT", "2000"))
CARLA_MAP = (os.environ.get("CARLA_MAP") or "").strip()
MQTT_BROKER = os.environ.get("MQTT_BROKER", "127.0.0.1")
MQTT_PORT = int(os.environ.get("MQTT_PORT", "1883"))
ZLM_HOST = os.environ.get("ZLM_HOST", "127.0.0.1")
ZLM_RTMP_PORT = os.environ.get("ZLM_RTMP_PORT", "1935")
ZLM_APP = os.environ.get("ZLM_APP", "teleop")
STATUS_HZ = float(os.environ.get("STATUS_HZ", "50"))
VIN = os.environ.get("VIN", "carla-sim-001")
# 车辆配置：CARLA_VEHICLE_BP 如 "vehicle.tesla.model3" 或 "vehicle.*"；CARLA_SPAWN_INDEX 出生点索引
CARLA_VEHICLE_BP = os.environ.get("CARLA_VEHICLE_BP", "vehicle.*")
CARLA_SPAWN_INDEX = int(os.environ.get("CARLA_SPAWN_INDEX", "0"))
# 仿真窗口视角：SPECTATOR_FOLLOW_VEHICLE=1 时镜头跟随车辆；SPECTATOR_VIEW_MODE=driver 为主视角（驾驶位）
SPECTATOR_FOLLOW_VEHICLE = (os.environ.get("SPECTATOR_FOLLOW_VEHICLE", "1").strip().lower() in ("1", "true", "yes"))
SPECTATOR_VIEW_MODE = (os.environ.get("SPECTATOR_VIEW_MODE", "driver").strip().lower())  # driver=主视角 third_person=第三人称
SPECTATOR_DEBUG = (os.environ.get("SPECTATOR_DEBUG", "").strip().lower() in ("1", "true", "yes"))  # 详细日志
# 车辆控制：CONTROL_DEBUG=1 时每帧输出 apply_control 参数；CONTROL_HZ 控制应用频率（默认 50）
CONTROL_DEBUG = (os.environ.get("CONTROL_DEBUG", "").strip().lower() in ("1", "true", "yes"))
CONTROL_HZ = float(os.environ.get("CONTROL_HZ", "50"))

_log_startup()

# 交互记录（开发调试用）：RECORD_INTERACTION=1 时写入 NDJSON 到 RECORD_INTERACTION_DIR
RECORD_INTERACTION = (os.environ.get("RECORD_INTERACTION", "").strip().lower() in ("1", "true"))
RECORD_INTERACTION_DIR = os.environ.get("RECORD_INTERACTION_DIR", "").strip() or "recordings"
RECORD_INTERACTION_FULL = (os.environ.get("RECORD_INTERACTION_FULL_PAYLOAD", "").strip().lower() in ("1", "true"))
_record_file = None
_record_lock = threading.Lock()

def _record_interaction(direction, peer, topic_or_path, payload_summary, payload_size, vin="", session_id="", error="", payload=None):
    """写一条 NDJSON 到记录文件（仅当 RECORD_INTERACTION=1 时）。"""
    if not RECORD_INTERACTION:
        return
    try:
        from datetime import datetime, timezone
        ts = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%S.%f")[:-3] + "Z"
        rec = {
            "ts": ts,
            "module": "carla-bridge",
            "direction": direction,
            "peer": peer,
            "topic_or_path": topic_or_path,
            "payload_summary": (payload_summary or "")[:200],
            "payload_size": payload_size,
            "vin": vin,
            "session_id": session_id,
            "error": error,
        }
        if RECORD_INTERACTION_FULL and payload is not None:
            rec["payload"] = payload if isinstance(payload, str) else (payload[:500] if isinstance(payload, (bytes, bytearray)) else "")
        line = json.dumps(rec, ensure_ascii=False) + "\n"
        with _record_lock:
            global _record_file
            if _record_file is None:
                os.makedirs(RECORD_INTERACTION_DIR, exist_ok=True)
                fn = f"carla-bridge_{time.strftime('%Y-%m-%d_%H-%M-%S', time.gmtime())}.jsonl"
                path = os.path.join(RECORD_INTERACTION_DIR, fn)
                _record_file = open(path, "a", encoding="utf-8")
                log_mqtt("[RECORD] 交互记录已开启: %s", path)
            _record_file.write(line)
            _record_file.flush()
    except Exception as e:
        _warn("RECORD", "write interaction record failed: %s", e)

# 相机与推流
CAMERA_WIDTH = int(os.environ.get("CAMERA_WIDTH", "640"))
CAMERA_HEIGHT = int(os.environ.get("CAMERA_HEIGHT", "480"))
CAMERA_FPS = int(os.environ.get("CAMERA_FPS", "15"))
# 前摄像头：1.0,0,1.2 为驾驶位主视角（车内）；2.5 为车头视角
CAMERA_DRIVER_VIEW = (os.environ.get("CAMERA_DRIVER_VIEW", "1").strip().lower() in ("1", "true", "yes"))
_cam_front_x = 1.0 if CAMERA_DRIVER_VIEW else 2.5
CAMERA_CONFIGS = [
    ("cam_front", _cam_front_x, 0.0, 1.2, 0),
    ("cam_rear", -2.5, 0.0, 1.2, 180),
    ("cam_left", 0.0, -1.2, 1.2, 90),
    ("cam_right", 0.0, 1.2, 1.2, -90),
]
# 四路 worker 错峰启动间隔（秒），避免同时连 ZLM 导致部分推流失败
# 增大间隔可降低验证时因首帧未到导致 ffprobe 拉流失败的概率
STAGGER_START_SECONDS = float(os.environ.get("STAGGER_START_SECONDS", "1.5"))

def to_bgr(carla_image):
    """CARLA Image (BGRA) -> BGR numpy (H, W, 3)."""
    try:
        import numpy as np
        arr = np.frombuffer(carla_image.raw_data, dtype=np.uint8)
        arr = arr.reshape((carla_image.height, carla_image.width, 4))
        return arr[:, :, :3].copy()
    except Exception as e:
        _warn("CARLA", "to_bgr failed: %s", e)
        raise

def run_ffmpeg_pusher(stream_id, frame_queue, rtmp_url, width, height, fps, stop_event):
    """从 frame_queue 取 BGR 帧，经 ffmpeg 推 RTMP，直到 stop_event 或队列收到 None."""
    log_zlm("环节: worker 启动 stream_id=%s rtmp_url=%s size=%dx%d fps=%s", stream_id, rtmp_url, width, height, fps)
    _record_interaction("out", "zlm", stream_id, "push_start rtmp=%s" % rtmp_url, 0, vin=VIN)
    cmd = [
        "ffmpeg", "-y",
        "-f", "rawvideo", "-pix_fmt", "bgr24", "-s", f"{width}x{height}", "-r", str(fps),
        "-i", "pipe:0",
        "-c:v", "libx264", "-preset", "ultrafast", "-tune", "zerolatency",
        "-pix_fmt", "yuv420p", "-g", str(fps), "-keyint_min", str(fps),
        "-f", "flv", rtmp_url,
    ]
    # ── 诊断增强：捕获 ffmpeg stderr 以解析 FPS/码率/帧质量 ────────────────────
    # ffmpeg stderr 包含 frame= fps= bitrate= 等统计行，是推流质量的核心证据
    proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    import re
    ffmpeg_lines = []
    ffmpeg_lock = threading.Lock()
    ffmpeg_stop = threading.Event()

    def drain_stderr():
        try:
            while not ffmpeg_stop.is_set():
                line = proc.stderr.readline()
                if not line:
                    break
                line = line.decode("utf-8", errors="replace").rstrip()
                with ffmpeg_lock:
                    ffmpeg_lines.append(line)
                # 实时解析关键行（frame / fps / bitrate），平时静默
                m_frame = re.search(r"frame=\s*(\d+)", line)
                m_fps   = re.search(r"fps=\s*(\d+\.?\d*)", line)
                m_br    = re.search(r"bitrate=\s*(\d+\.?\d*\s*\D+)", line)
                m_drop  = re.search(r"dropped=\s*(\d+)", line)
                m_size  = re.search(r"size=\s*(\d+\s*\w+)", line)
                if m_frame or m_fps or m_br or m_drop or m_size:
                    parts = []
                    if m_frame: parts.append("frame=%s" % m_frame.group(1))
                    if m_fps:   parts.append("fps=%s" % m_fps.group(1))
                    if m_br:    parts.append("bitrate=%s" % m_br.group(1).strip())
                    if m_drop:  parts.append("drop=%s" % m_drop.group(1))
                    if m_size:  parts.append("size=%s" % m_size.group(1))
        except Exception:
            pass

    stderr_thread = threading.Thread(target=drain_stderr, daemon=True)
    stderr_thread.start()

    # ── 推流质量诊断：周期性打报告 ────────────────────────────────────────────
    import time
    last_quality_report = time.time()
    last_frame_count = 0
    frames_since_report = 0

    try:
        while True:
            # 两个退出条件：(1) stop_event 被设置，或 (2) 队列收到 None 哨兵
            if stop_event.is_set():
                log_zlm("环节: worker stop_event 已设置 stream_id=%s", stream_id)
                break
            try:
                frame = frame_queue.get(timeout=0.5)
            except queue.Empty:
                continue
            if frame is None:
                log_zlm("环节: worker 收到停止信号 stream_id=%s", stream_id)
                _record_interaction("out", "zlm", stream_id, "push_stop", 0, vin=VIN)
                break
            try:
                proc.stdin.write(frame.tobytes())
                proc.stdin.flush()
                frames_since_report += 1
            except (IOError, OSError) as e:
                # broken pipe (ffmpeg 崩溃) 或 pipe buffer 满；记录并退出推流循环
                _warn("ZLM", "worker stdin write failed stream_id=%s err=%s errno=%d", stream_id, e, e.errno)
                log_zlm("环节: worker stdin 写入失败 stream_id=%s err=%s errno=%d", stream_id, e, e.errno)
                break

            # ── 诊断：每 5s 打一次推流质量报告（对齐相机帧到达率打印）──────────────
            now = time.time()
            if now - last_quality_report >= 5.0:
                elapsed = now - last_quality_report
                inst_fps = frames_since_report / elapsed if elapsed > 0 else 0
                delta_frames = frames_since_report - last_frame_count
                # 同时检查 ffmpeg 进程是否存活（避免进程崩溃后还在空转）
                proc_alive = (proc.poll() is None)
                with ffmpeg_lock:
                    frame_snapshot = [l for l in ffmpeg_lines if "frame=" in l]
                    last_frame_line = frame_snapshot[-1] if frame_snapshot else "(无)"
                log_zlm("[推流质量] stream_id=%s %.0fs内推帧%d 瞬时FPS=%.1f ffmpeg=%s %s",
                         stream_id, elapsed, delta_frames, inst_fps,
                         "存活" if proc_alive else "已崩溃", last_frame_line)
                # 检测异常：瞬时 FPS 低于预期 50%
                if inst_fps < fps * 0.5 and frames_since_report > 10:
                    _warn("ZLM", "[推流质量] stream_id=%s FPS=%.1f < 预期%.0f 的 50%%！"
                               "检查 CARLA 相机帧率或 ffmpeg 性能", stream_id, inst_fps, fps)
                # ffmpeg 崩溃检测：若进程退出但推流仍在继续，说明推流链路断裂
                if not proc_alive:
                    log_zlm("[推流质量] ffmpeg 进程已退出 exit=%d，停止推流 stream_id=%s", proc.returncode, stream_id)
                    break
                last_quality_report = now
                last_frame_count = frames_since_report
    except Exception as e:
        import traceback
        _warn("ZLM", "worker unexpected error stream_id=%s err=%s", stream_id, e)
        log_zlm("环节: worker 异常 stream_id=%s 错误=%s", stream_id, e)
        log_zlm("Traceback: %s", traceback.format_exc())
    finally:
        ffmpeg_stop.set()
        try:
            proc.stdin.close()
        except (IOError, OSError) as e:
            log_zlm("环节: worker stdin.close 失败 stream_id=%s err=%s", stream_id, e)
        # 取 ffmpeg 最终统计行
        with ffmpeg_lock:
            final_lines = [l for l in ffmpeg_lines if any(k in l for k in ("frame=", "fps=", "bitrate=", "size="))]
            if final_lines:
                log_zlm("[推流质量] ffmpeg 最终统计 stream_id=%s: %s", stream_id, final_lines[-1])
        proc.wait()
        if proc.returncode is not None and proc.returncode != 0:
            log_zlm("环节: worker ffmpeg 异常退出 stream_id=%s exit_code=%d", stream_id, proc.returncode)
            # ── 诊断增强：ffmpeg 异常退出时打印最后 10 行 stderr ──────────────
            with ffmpeg_lock:
                recent = ffmpeg_lines[-10:] if ffmpeg_lines else []
            if recent:
                log_zlm("[推流质量] ffmpeg stderr 最近10行 stream_id=%s:", stream_id)
                for l in recent:
                    log_zlm("  ffmpeg: %s", l)
        log_zlm("环节: worker 已退出 stream_id=%s 累计推帧=%d", stream_id, frames_since_report)


def run_ffmpeg_testsrc(stream_id, rtmp_url, width, height, fps, stop_event):
    """使用 ffmpeg testsrc 模式生成测试视频流，不依赖 CARLA 相机。
    用于无 GPU/DISPLAY 环境下验证完整推流链路（CARLA→ZLM→WebRTC→Client）。
    用法：FORCE_TESTSRC=1 或 USE_TESTSRC_FALLBACK=1（自动降级）"""
    log_zlm("[testsrc] 启动 stream_id=%s rtmp_url=%s size=%dx%d fps=%s", stream_id, rtmp_url, width, height, fps)
    cmd = [
        "ffmpeg", "-re",
        "-f", "lavfi", "-i", f"testsrc=size={width}x{height}:rate={fps}",
        "-f", "lavfi", "-i", "aevalsrc=0",
        "-c:v", "libx264", "-preset", "ultrafast", "-tune", "zerolatency",
        "-pix_fmt", "yuv420p", "-g", str(fps), "-keyint_min", str(fps),
        "-c:a", "aac", "-b:a", "0k",
        "-f", "flv", rtmp_url,
    ]
    proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    import re
    ffmpeg_lines = []
    ffmpeg_lock = threading.Lock()
    ffmpeg_stop = threading.Event()

    def drain_stderr():
        try:
            while not ffmpeg_stop.is_set():
                line = proc.stderr.readline()
                if not line:
                    break
                line = line.decode("utf-8", errors="replace").rstrip()
                with ffmpeg_lock:
                    ffmpeg_lines.append(line)
                m_frame = re.search(r"frame=\s*(\d+)", line)
                m_fps = re.search(r"fps=\s*(\d+\.?\d*)", line)
                if m_frame or m_fps:
                    parts = []
                    if m_frame: parts.append("frame=%s" % m_frame.group(1))
                    if m_fps: parts.append("fps=%s" % m_fps.group(1))
        except Exception:
            pass

    stderr_thread = threading.Thread(target=drain_stderr, daemon=True)
    stderr_thread.start()
    import time
    last_report = time.time()
    try:
        while not stop_event.is_set():
            time.sleep(0.5)
            if time.time() - last_report >= 10.0:
                with ffmpeg_lock:
                    snap = [l for l in ffmpeg_lines if "frame=" in l]
                    last_line = snap[-1] if snap else "(无)"
                log_zlm("[testsrc][推流质量] stream_id=%s %s", stream_id, last_line)
                last_report = time.time()
            if proc.poll() is not None:
                with ffmpeg_lock:
                    recent = ffmpeg_lines[-5:]
                log_zlm("[testsrc] ffmpeg 已退出 exit=%s 最近: %s", proc.returncode, recent)
                break
    except Exception as e:
        log_zlm("[testsrc] 异常 stream_id=%s: %s", stream_id, e)
    finally:
        ffmpeg_stop.set()
        proc.wait()
        log_zlm("[testsrc] worker 已退出 stream_id=%s", stream_id)

# ========================================
# 主流程开始
# ========================================
log_carla("启动配置: CARLA=%s:%s MAP=%s MQTT=%s:%s ZLM=%s:%s VIN=%s",
         CARLA_HOST, CARLA_PORT, CARLA_MAP or "(default)", MQTT_BROKER, MQTT_PORT, ZLM_HOST, ZLM_RTMP_PORT, VIN)
log_carla("视频源: CARLA 仿真相机（四路相机）；若日志中无此句则为 testsrc")

# 导入依赖（缺失时尝试 pip 安装后重试）
def _ensure_import(module_name, pip_package=None):
    pip_package = pip_package or module_name.replace(".", "-")
    try:
        if module_name == "paho.mqtt.client":
            import paho.mqtt.client as mqtt
            return mqtt
        return __import__(module_name)
    except ImportError:
        pass
    log_carla("尝试安装 %s ...", pip_package)
    rc = subprocess.run(
        [sys.executable, "-m", "pip", "install", "-q", pip_package],
        timeout=120,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if rc.returncode != 0:
        log_carla("pip install %s 失败", pip_package)
        return None
    try:
        if module_name == "paho.mqtt.client":
            import paho.mqtt.client as mqtt
            return mqtt
        return __import__(module_name)
    except ImportError:
        return None

carla = _ensure_import("carla", "carla")
if carla is None:
    log_carla("请安装 carla: pip install carla")
    sys.exit(1)
mqtt = _ensure_import("paho.mqtt.client", "paho-mqtt")
if mqtt is None:
    log_mqtt("请安装 paho-mqtt: pip install paho-mqtt")
    sys.exit(1)

import numpy as np
import sys
import os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "common"))
from retry import retry, is_transient_error

# 连接 CARLA（RPC 就绪较慢，增加重试与超时）
client = None
MAX_CARLA_ATTEMPTS = 7
CARLA_RETRY_SLEEP = 5

def _connect_carla():
    log_carla("连接 %s:%s ...", CARLA_HOST, CARLA_PORT)
    cl = carla.Client(CARLA_HOST, CARLA_PORT)
    cl.set_timeout(20)
    world = cl.get_world()
    current_map = (world.get_map().name if world else "") or ""
    log_carla("已连接，当前地图: %s", current_map or "(unknown)")
    if CARLA_MAP and CARLA_MAP not in current_map:
        log_carla("加载场景/地图: %s", CARLA_MAP)
        world = cl.load_world(CARLA_MAP)
        log_carla("已加载地图: %s", world.get_map().name)
    return cl, world

log_carla("[Bridge] 阶段: 连接 CARLA %s:%s (最多 %d 次)", CARLA_HOST, CARLA_PORT, MAX_CARLA_ATTEMPTS)
try:
    client, world = retry(_connect_carla, max_attempts=MAX_CARLA_ATTEMPTS, backoff_base=CARLA_RETRY_SLEEP)
    log_carla("[Bridge] 阶段: CARLA 连接成功")
except Exception as e:
    log_carla("CARLA connect failed after %d retries err=E_CARLA_CONN_FAILED cause=%s", MAX_CARLA_ATTEMPTS, e)
    import traceback
    log_carla("完整 Traceback:\n%s", traceback.format_exc())
    sys.exit(1)

if not client:
    log_carla("CARLA Client 初始化失败，请检查 CARLA 服务")
    exit(1)

blueprint_library = world.get_blueprint_library()
vehicle_bps = blueprint_library.filter(CARLA_VEHICLE_BP)
if not vehicle_bps:
    log_carla("无匹配车辆蓝图 CARLA_VEHICLE_BP=%s，尝试 vehicle.*", CARLA_VEHICLE_BP)
    vehicle_bps = blueprint_library.filter("vehicle.*")
if not vehicle_bps:
    log_carla("无车辆蓝图，退出")
    exit(1)
vehicle_bp = vehicle_bps[0]
log_carla("车辆蓝图: %s (CARLA_VEHICLE_BP=%s)", vehicle_bp.id, CARLA_VEHICLE_BP)

spawn_points = world.get_map().get_spawn_points()
if not spawn_points:
    log_carla("无 spawn 点，退出")
    exit(1)
spawn_idx = min(CARLA_SPAWN_INDEX, len(spawn_points) - 1)
spawn_point = spawn_points[spawn_idx]
log_carla("spawn 点: 索引 %d / 共 %d", spawn_idx, len(spawn_points))

log_carla("[Bridge] 阶段: spawn 车辆 %s 于 spawn_points[%d]", vehicle_bp.id, spawn_idx)
vehicle = world.spawn_actor(vehicle_bp, spawn_point)
log_carla("[Bridge] 阶段: 车辆已 spawn，进入主循环（含 Spectator 更新）")
log_carla("[Spectator] SPECTATOR_FOLLOW_VEHICLE=%s SPECTATOR_VIEW_MODE=%s CAMERA_DRIVER_VIEW=%s SPECTATOR_DEBUG=%s",
          SPECTATOR_FOLLOW_VEHICLE, SPECTATOR_VIEW_MODE, CAMERA_DRIVER_VIEW, SPECTATOR_DEBUG)
log_carla("[Spectator] 仿真窗口将使用: %s；前摄像头: %s",
          "驾驶位主视角" if SPECTATOR_VIEW_MODE == "driver" else "第三人称",
          "驾驶位主视角" if CAMERA_DRIVER_VIEW else "车头视角")

# ── testsrc 降级策略 ───────────────────────────────────────────────────────────
# FORCE_TESTSRC=1：完全跳过 CARLA 相机，始终使用 testsrc（用于无 GPU/CARLA 的环境）
# USE_TESTSRC_FALLBACK=1：若 CARLA 相机 10s 内到达帧率 < 3 fps，自动降级到 testsrc
# （保留 CARLA 相机，后续仍可切换回来）
FORCE_TESTSRC = (os.environ.get("FORCE_TESTSRC", "").strip().lower() in ("1", "true", "yes"))
USE_TESTSRC_FALLBACK = (os.environ.get("USE_TESTSRC_FALLBACK", "1").strip().lower() in ("1", "true", "yes"))
_test_src_active = {}  # cam_id -> True if currently using testsrc for this stream
_test_src_lock = threading.Lock()

# 控制量（由 MQTT 更新）；档位：-1=R 0=N 1=D 2=P（与客户端 DrivingInterface 一致）
control_state = {
    "steering": 0.0,
    "throttle": 0.0,
    "brake": 0.0,
    "gear": 1,  # 默认 D 档
    "remote_enabled": False,
    "streaming": False,
    "emergency_stop": False,  # 急停激活时 throttle=0 brake=1 hand_brake=True
}
control_lock = threading.Lock()
_control_apply_count = 0
_control_last_log_time = 0.0

# 推流相关：cameras + queues + workers；四路相互独立，每路一个线程
camera_actors = []
frame_queues = {}
stop_events = {}
worker_threads = []
streaming_lock = threading.Lock()
spawn_lock = threading.Lock()  # 仅用于串行化 CARLA spawn_actor，避免多线程同时 spawn 相互影响

# ── 相机帧率统计（用于诊断推流低 FPS 根因）──────────────────────────────────────
# key = cam_id, value = {"count": 0, "first_ts": None, "last_ts": None, "last_report": 0}
_cam_stats = {}
_cam_stats_lock = threading.Lock()


def _report_cam_stats():
    """每 5s 打印一次各相机帧到达率，与 ffmpeg 推流质量报告对齐，便于定位根因：
    - 如果相机回调到达率正常(~15fps) 但 ffmpeg 推流率低 → ffmpeg 或 ZLM 问题
    - 如果相机回调到达率本身就低 → CARLA 渲染问题(DISPLAY/GPU/传感器配置)
    """
    import time
    now = time.time()
    lines = []
    with _cam_stats_lock:
        for cam_id, st in _cam_stats.items():
            if st["first_ts"] is None or st["last_report"] == 0:
                continue
            elapsed = now - st["last_report"]
            if elapsed < 4.9:
                continue
            arrived = st["count"]
            fps = arrived / elapsed if elapsed > 0 else 0
            lines.append(
                f"stream_id={cam_id} 到达帧{fps:.1f}fps/{arrived}帧/{elapsed:.0f}s "
                f"(总{int(st['first_ts']) if st['first_ts'] else 0}s内{st.get('total', arrived)}帧)"
            )
            st["count"] = 0
            st["last_report"] = now
            st["total"] = st.get("total", arrived) + arrived
    for l in lines:
        _log("Camera", l)


def _put_cam_frame(cam_id, image):
    """相机回调：将 CARLA 图像转 BGR 放入队列；异常时告警不抛。
    ★ 关键：使用 put(timeout=0) 非阻塞写入；队列满时丢弃旧帧，避免阻塞相机回调导致 CARLA 丢帧。"""
    import time as _time
    try:
        # ── 帧到达统计（先于 to_bgr 计数，哪怕转换失败也要记录）─────────────────
        with _cam_stats_lock:
            if cam_id not in _cam_stats:
                _cam_stats[cam_id] = {"count": 0, "first_ts": _time.time(), "last_ts": None, "last_report": _time.time(), "total": 0}
            st = _cam_stats[cam_id]
            st["count"] += 1
            st["last_ts"] = _time.time()
        frame = to_bgr(image)
        # put_nowait 队列满时抛 Full；捕获后删除最旧帧再重试（最多重试一次）
        try:
            frame_queues[cam_id].put_nowait(frame)
        except queue.Full:
            try:
                frame_queues[cam_id].get_nowait()  # 丢弃队首（旧帧）
            except queue.Empty:
                pass
            try:
                frame_queues[cam_id].put_nowait(frame)
            except queue.Full:
                # 两次队列满，说明 ffmpeg 写入阻塞；记录并丢弃该帧
                _warn("ZLM", "camera callback: queue full after drain, dropping frame cam_id=%s", cam_id)
    except Exception as e:
        _warn("ZLM", "camera callback unexpected error cam_id=%s: %s", cam_id, e)

def run_one_stream(cam_id, x, y, z, yaw, q, stop, rtmp_url, cam_bp):
    """单路推流线程：spawn 相机、注册回调、运行 ffmpeg 推流；与其余三路互不影响。
    若 USE_TESTSRC_FALLBACK=1 且 CARLA 相机 10s 内到达帧率 < 3 fps，自动降级到 testsrc。"""
    try:
        if FORCE_TESTSRC:
            log_zlm("[testsrc] FORCE_TESTSRC=1，跳过 CARLA 相机直接启动 testsrc stream_id=%s", cam_id)
            run_ffmpeg_testsrc(cam_id, rtmp_url, CAMERA_WIDTH, CAMERA_HEIGHT, CAMERA_FPS, stop)
            return

        import time as _time
        # ── 尝试 spawn CARLA 相机 ───────────────────────────────────────────
        try:
            with spawn_lock:
                transform = carla.Transform(carla.Location(x=x, y=y, z=z), rotation=carla.Rotation(yaw=yaw))
                cam_actor = world.spawn_actor(cam_bp, transform, attach_to=vehicle)
            camera_actors.append(cam_actor)
            log_zlm("环节: spawn 相机 %s 位置(%.2f,%.2f,%.2f) yaw=%d", cam_id, x, y, z, yaw)
        except Exception as e:
            _warn("CARLA", "spawn_actor camera failed %s: %s", cam_id, e)
            if USE_TESTSRC_FALLBACK:
                log_zlm("[testsrc] 相机 spawn 失败，降级到 testsrc stream_id=%s", cam_id)
                run_ffmpeg_testsrc(cam_id, rtmp_url, CAMERA_WIDTH, CAMERA_HEIGHT, CAMERA_FPS, stop)
            return

        # 注册相机回调（在启动推流前先注册，确保帧计数从一开始就进行）
        camera.listen(lambda img, cid=cam_id: _put_cam_frame(cid, img))

        # ── 启动推流 worker（独立线程消费队列）───────────────────────────────
        # 使用 threading.Event 作为推流停止信号，不直接阻塞 spawn 流程
        pusher_stop = threading.Event()
        pusher_thread = threading.Thread(
            target=run_ffmpeg_pusher,
            args=(cam_id, q, rtmp_url, CAMERA_WIDTH, CAMERA_HEIGHT, CAMERA_FPS, pusher_stop),
            daemon=True, name=f"pusher-{cam_id}",
        )
        pusher_thread.start()

        # ── 10s 窗口：观测 CARLA 相机帧到达率 ──────────────────────────────
        if USE_TESTSRC_FALLBACK:
            t_start = _time.time()
            init_count = 0
            with _cam_stats_lock:
                st = _cam_stats.get(cam_id, {"count": 0})
                init_count = st.get("count", 0)

            arrived_at_start = init_count
            fps_low_logged = False
            while _time.time() - t_start < 10.0:
                _time.sleep(1.0)
                if stop.is_set():
                    pusher_stop.set()
                    return

                with _cam_stats_lock:
                    st = _cam_stats.get(cam_id, {"count": 0})
                    count_now = st.get("count", 0)

                elapsed = _time.time() - t_start
                arrival_fps = (count_now - arrived_at_start) / elapsed if elapsed > 0.1 else 0.0

                if arrival_fps >= 3.0:
                    log_zlm("[CARLA相机] stream_id=%s 帧到达率 %.1f fps >= 3，继续使用 CARLA 相机", cam_id, arrival_fps)
                    break  # 正常，使用 CARLA 相机
                if not fps_low_logged and elapsed >= 5.0:
                    _warn("ZLM", "[testsrc] 相机 %s 帧率 %.1f fps < 3（%.0fs 内 %d 帧），"
                               "若持续 < 3 将降级到 testsrc", cam_id, arrival_fps, elapsed, count_now - arrived_at_start)
                    fps_low_logged = True
            else:
                # 10s 窗口内始终 < 3 fps，降级到 testsrc
                final_count = 0
                with _cam_stats_lock:
                    st = _cam_stats.get(cam_id, {"count": 0})
                    final_count = st.get("count", 0)
                arrival_fps = (final_count - arrived_at_start) / 10.0
                log_zlm("[testsrc] 降级 CARLA→testsrc stream_id=%s（%.0fs 内 %d 帧，%.1f fps < 3 fps）",
                        cam_id, 10.0, final_count - arrived_at_start, arrival_fps)
                # 停止 CARLA 推流 worker，切换到 testsrc
                pusher_stop.set()
                pusher_thread.join(timeout=3.0)
                try:
                    camera.stop()
                except Exception:
                    pass
                with _cam_stats_lock:
                    _test_src_active[cam_id] = True
                run_ffmpeg_testsrc(cam_id, rtmp_url, CAMERA_WIDTH, CAMERA_HEIGHT, CAMERA_FPS, stop)
                return

        # 正常路径：等待 CARLA 推流 worker 结束
        pusher_thread.join()
    except Exception as e:
        _warn("ZLM", "run_one_stream failed %s: %s", cam_id, e)
        import traceback
        log_zlm("Traceback: %s", traceback.format_exc())

def spawn_cameras_and_start_pushers():
    # camera_actors, frame_queues, stop_events, worker_threads 已为全局变量，无需声明 nonlocal
    try:
        _spawn_cameras_and_start_pushers_impl()
    except Exception as e:
        _warn("ZLM", "spawn_cameras_and_start_pushers failed: %s", e)
        import traceback
        log_zlm("Traceback: %s", traceback.format_exc())

def _spawn_cameras_and_start_pushers_impl():
    with streaming_lock:
        if camera_actors:
            log_zlm("已在推流，跳过重复 spawn")
            return
        log_zlm("四路推流相互独立，每路一线程；spawn 与推流互不影响")
        rtmp_base = f"rtmp://{ZLM_HOST}:{ZLM_RTMP_PORT}/{ZLM_APP}"
        log_zlm("环节: spawn_cameras_and_start_pushers 进入 ZLM=%s:%s app=%s vin=%s", ZLM_HOST, ZLM_RTMP_PORT, ZLM_APP, VIN)
        # 多车隔离：流名加 VIN 前缀，格式 {vin}_cam_front，避免多台车流名冲突
        vin_prefix = VIN + "_" if VIN else ""
        log_zlm("视频源: CARLA 仿真相机；推流目标 %s/{%scam_front,%scam_rear,%scam_left,%scam_right}",
                rtmp_base, vin_prefix, vin_prefix, vin_prefix, vin_prefix)

        cam_bp = blueprint_library.find("sensor.camera.rgb")
        cam_bp.set_attribute("image_size_x", str(CAMERA_WIDTH))
        cam_bp.set_attribute("image_size_y", str(CAMERA_HEIGHT))
        cam_bp.set_attribute("sensor_tick", str(CAMERA_FPS))

        for cam_id, x, y, z, yaw in CAMERA_CONFIGS:
            q = queue.Queue(maxsize=5)
            frame_queues[cam_id] = q
            stop = threading.Event()
            stop_events[cam_id] = stop

        for i, (cam_id, x, y, z, yaw) in enumerate(CAMERA_CONFIGS):
            q = frame_queues[cam_id]
            stop = stop_events[cam_id]
            stream_name = f"{vin_prefix}{cam_id}"
            rtmp_url = f"{rtmp_base}/{stream_name}"
            t = threading.Thread(
                target=run_one_stream,
                args=(cam_id, x, y, z, yaw, q, stop, rtmp_url, cam_bp),
                daemon=True,
                name=f"stream-{stream_name}",
            )
            worker_threads.append(t)
            t.start()
            if i < len(CAMERA_CONFIGS) - 1:
                time.sleep(STAGGER_START_SECONDS)

def stop_streaming():
    if camera_actors:
        log_zlm("环节: stop_streaming：停止 %d 路 worker", len(camera_actors))
        for cam_id, stop in stop_events.items():
            stop.set()
        for q in frame_queues.values():
            try:
                q.put(None)
            except Exception as e:
                _warn("ZLM", "stop_streaming put None failed: %s", e)
        log_zlm("环节: stop_streaming：已向所有队列发送停止信号")


def _gear_to_carla(gear):
    """档位映射：teleop -1=R 0=N 1=D 2=P -> CARLA reverse/hand_brake。"""
    g = int(gear) if isinstance(gear, (int, float)) else 1
    if g == -1:
        return True, False   # reverse
    if g == 2:
        return False, True   # P 档手刹
    return False, False      # N/D


def _apply_vehicle_control():
    """将 control_state 应用到 CARLA 车辆。远驾未启用时安全制动；急停时强制刹车。"""
    global _control_apply_count, _control_last_log_time
    try:
        with control_lock:
            steer = max(-1.0, min(1.0, control_state.get("steering", 0.0)))
            throttle = max(0.0, min(1.0, control_state.get("throttle", 0.0)))
            brake = max(0.0, min(1.0, control_state.get("brake", 0.0)))
            gear = control_state.get("gear", 1)
            remote_ok = control_state.get("remote_enabled", False)
            emergency = control_state.get("emergency_stop", False)

        if emergency:
            throttle, brake = 0.0, 1.0
            reverse, hand_brake = False, True
            log_control("[Control] 急停激活：throttle=0 brake=1 hand_brake=True")
        elif not remote_ok:
            throttle, brake = 0.0, 0.5  # 远驾未启用：安全制动
            reverse, hand_brake = False, False
        else:
            reverse, hand_brake = _gear_to_carla(gear)

        ctrl = carla.VehicleControl(
            throttle=throttle,
            steer=steer,
            brake=brake,
            hand_brake=hand_brake,
            reverse=reverse,
            manual_gear_shift=False,
            gear=0,
        )
        vehicle.apply_control(ctrl)
        _control_apply_count += 1

        now = time.time()
        if CONTROL_DEBUG or (
            _control_apply_count <= 5 or (now - _control_last_log_time >= 10)
        ):
            log_control(
                "[Control] 应用 #%d steer=%.3f throttle=%.3f brake=%.3f gear=%d reverse=%s hand_brake=%s remote=%s",
                _control_apply_count, steer, throttle, brake, gear, reverse, hand_brake, remote_ok,
            )
            _control_last_log_time = now
    except Exception as e:
        _warn("Control", "apply_control failed: %s", e)
        import traceback
        if CONTROL_DEBUG:
            log_control("[Control] Traceback: %s", traceback.format_exc())


def _read_vehicle_speed():
    """从 CARLA 车辆读取速度（km/h）。"""
    try:
        v = vehicle.get_velocity()
        return math.sqrt(v.x**2 + v.y**2 + v.z**2) * 3.6  # m/s -> km/h
    except Exception:
        return 0.0


def send_status():
    try:
        speed_kmh = _read_vehicle_speed()
        with control_lock:
            status = {
                "vin": VIN,
                "timestamp": int(time.time() * 1000),
                "speed": round(speed_kmh, 2),
                "battery": 99.9,
                "brake": control_state.get("brake", 0.0),
                "throttle": control_state.get("throttle", 0.0),
                "steering": control_state.get("steering", 0.0),
                "gear": control_state.get("gear", 1),
                "odometer": 0.0,
                "temperature": 25,
                "voltage": 48,
                "type": "vehicle_status",
                "remote_control_enabled": control_state.get("remote_enabled", False),
                "driving_mode": "远驾" if control_state.get("remote_enabled", False) else "自驾",
                "streaming": control_state.get("streaming", False),
            }
        status_json = json.dumps(status)
        _now = time.time()
        if CONTROL_DEBUG or not hasattr(send_status, "_last_status_log") or (_now - send_status._last_status_log >= 5.0):
            log_control("[Control] 发布 vehicle_status speed=%.1f gear=%d steer=%.2f throttle=%.2f brake=%.2f",
                        status.get("speed", 0), status.get("gear", 1),
                        status.get("steering", 0), status.get("throttle", 0), status.get("brake", 0))
            send_status._last_status_log = _now
        # 交互记录：status 采样（每 1s 一条，避免 50Hz 刷盘）
        if RECORD_INTERACTION:
            now = time.time()
            if not hasattr(send_status, "_last_record_time"):
                send_status._last_record_time = 0
            if now - send_status._last_record_time >= 1.0:
                summary = "speed=%.1f,gear=%d,streaming=%s" % (
                    status.get("speed", 0), status.get("gear", 1), status.get("streaming", False))
                _record_interaction("out", "mqtt", "vehicle/status", summary, len(status_json), vin=VIN)
                send_status._last_record_time = now
        try:
            mqtt_client.publish(f"vehicle/status", payload=status_json)
        except Exception as e:
            _warn("MQTT", "publish vehicle/status failed err=E_MQTT_CONN_FAILED cause=%s", e)
    except Exception as e:
        _err("Control", "send_status 总异常: %s", e)

def on_connect(client, userdata, flags, rc, *args):
    """兼容 paho-mqtt v1 (rc) 与 v2 (reason_code, properties)。"""
    try:
        log_mqtt("MQTT 已连接 broker=%s:%s", MQTT_BROKER, MQTT_PORT)
        log_mqtt("启动 MQTT 客户端 ID: %s", getattr(client, "_client_id", "unknown"))
        result = client.subscribe("vehicle/control", qos=1)
        log_mqtt("订阅 vehicle/control 成功（QoS=1），result=%s", result)
    except Exception as e:
        import traceback
        _err("MQTT", "on_connect/subscribe failed err=E_MQTT_CONN_FAILED cause=%s\n%s", e, traceback.format_exc())

def on_message(client, userdata, msg):
    import traceback as _tb
    try:
        raw = (msg.payload or b"")
        if isinstance(raw, bytes):
            raw = raw.decode("utf-8", errors="replace")
        log_control("收到 vehicle/control 消息 topic=%s payload=%s", msg.topic, raw[:200])
        try:
            payload = json.loads(msg.payload if isinstance(msg.payload, str) else (msg.payload or b"").decode("utf-8"))
        except json.JSONDecodeError as e:
            _warn("Control", "JSON 解析失败: %s payload=%s", e, raw[:200])
            return
        if not isinstance(payload, dict):
            _warn("Control", "payload 不是 dict 类型: %s type=%s", type(payload).__name__, raw[:100])
            return
        msg_type = payload.get("type", "")
        vin = payload.get("vin", "")
        # 交互记录：MQTT 入
        summary = "type=%s,vin=%s" % (msg_type, vin)
        _record_interaction("in", "mqtt", msg.topic or "vehicle/control", summary, len(raw), vin=vin, payload=raw if RECORD_INTERACTION_FULL else None)
        if vin != VIN:
            return

        if msg_type == "remote_control":
            # Plan 4.1 & 9.3: Handle remote control enabling/disabling
            enable = payload.get("enable", False)
            with control_lock:
                control_state["remote_enabled"] = enable
                log_control("收到远驾接管指令: enable=%s", enable)
            # Send Ack
            status = {
                "vin": VIN,
                "type": "remote_control_ack",
                "remote_control_enabled": enable,
                "driving_mode": "远驾" if enable else "自驾",
                "timestamp": int(time.time() * 1000)
            }
            try:
                mqtt_client.publish(f"vehicle/status", payload=json.dumps(status))
            except Exception as e:
                _warn("MQTT", "publish remote_control_ack failed: %s", e)
            log_control("已发送远驾接管确认: %s", status)
        elif msg_type == "drive":
            with control_lock:
                try:
                    control_state["steering"] = float(payload.get("steering", 0.0))
                    control_state["throttle"] = float(payload.get("throttle", 0.0))
                    control_state["brake"] = float(payload.get("brake", 0.0))
                except (ValueError, TypeError) as e:
                    _warn("Control", "drive 指令字段解析异常: %s payload=%s", e, raw[:200])
                gear_val = payload.get("gear")
                if isinstance(gear_val, (int, float)):
                    control_state["gear"] = int(gear_val)
                log_control(
                    "[Control] 收到 drive: steering=%.3f throttle=%.3f brake=%.3f gear=%d",
                    control_state["steering"], control_state["throttle"],
                    control_state["brake"], control_state["gear"],
                )
        elif msg_type == "emergency_stop":
            enable = payload.get("enable", True)
            with control_lock:
                control_state["emergency_stop"] = bool(enable)
            log_control("[Control] 收到 emergency_stop: enable=%s", enable)
        elif msg_type == "gear":
            gear_val = payload.get("value", payload.get("gear"))
            if isinstance(gear_val, (int, float)):
                with control_lock:
                    control_state["gear"] = int(gear_val)
                log_control("[Control] 收到 gear: value=%d (-1=R 0=N 1=D 2=P)", int(gear_val))
        elif msg_type == "start_stream":
            log_control("收到 start_stream，准备 spawn 相机并开始推流")
            log_control("已置 streaming=True（VIN 匹配）")
            try:
                spawn_cameras_and_start_pushers()
            except Exception as e:
                _err("ZLM", "spawn_cameras_and_start_pushers 异常: %s", e)
            with control_lock:
                control_state["streaming"] = True
            log_control("已置 streaming=True，将发布 vehicle/status")
        elif msg_type == "stop_stream":
            log_control("收到 stop_stream，准备停止推流")
            try:
                stop_streaming()
            except Exception as e:
                _err("ZLM", "stop_streaming 异常: %s", e)
            with control_lock:
                control_state["streaming"] = False
            log_control("已置 streaming=False，将发布 vehicle/status")
        elif msg_type == "remote_control_ack":
            log_control("收到远驾接管确认消息")
            pass
    except Exception as e:
        _err("Control", "on_message 总异常: %s\n%s", e, _tb.format_exc())

mqtt_client = mqtt.Client(client_id=f"carla-bridge-{VIN}", clean_session=True)
mqtt_client.on_connect = on_connect
mqtt_client.on_message = on_message
mqtt_client.will_set(f"vehicle/status", payload=json.dumps({"vin": VIN, "type": "offline"}), qos=1, retain=False)

def mqtt_thread_func():
    log_mqtt("[MQTT] MQTT 线程启动")
    while True:
        try:
            mqtt_client.connect(MQTT_BROKER, MQTT_PORT, keepalive=60)
            log_mqtt("[MQTT] 连接成功 broker=%s:%s，进入 loop_forever 接收 vehicle/control", MQTT_BROKER, MQTT_PORT)
            mqtt_client.loop_forever()
        except Exception as e:
            _warn("MQTT", "connect/loop_forever failed err=E_MQTT_CONN_FAILED cause=%s", e)
            log_mqtt("[MQTT] 连接或循环异常: %s，2s 后重连", e)
            time.sleep(2)

mqtt_thread = threading.Thread(target=mqtt_thread_func, daemon=True)
mqtt_thread.start()

last_status_time = 0
last_spectator_time = 0
last_control_time = 0
last_cam_stats_time = 0
_cam_stats_interval = 5.0  # 每 5s 打印一次相机帧到达率
_spectator_update_count = 0
_spectator_last_log_time = 0
SPECTATOR_UPDATE_HZ = 20  # 仿真窗口镜头更新频率
SPECTATOR_OFFSET_BEHIND = 8.0   # 第三人称：镜头在车辆后方距离（米）
SPECTATOR_OFFSET_UP = 4.0       # 第三人称：镜头在车辆上方距离（米）
SPECTATOR_OFFSET_DRIVER_X = 1.0  # 主视角：驾驶位眼高，车辆前方偏移（米）
SPECTATOR_OFFSET_DRIVER_Z = 1.2  # 主视角：眼高（米）

def _update_spectator_follow_vehicle():
    """将 CARLA 仿真窗口的 spectator 镜头设置为跟随车辆。driver=主视角（驾驶位），third_person=第三人称。"""
    global _spectator_update_count, _spectator_last_log_time
    if not SPECTATOR_FOLLOW_VEHICLE:
        return
    try:
        spectator = world.get_spectator()
        if spectator is None:
            if SPECTATOR_DEBUG or _spectator_update_count == 0:
                _warn("Spectator", "world.get_spectator() 返回 None（无头模式时可能无 spectator）")
            return
        v_transform = vehicle.get_transform()
        yaw_rad = math.radians(v_transform.rotation.yaw)
        if SPECTATOR_VIEW_MODE == "driver":
            dx = SPECTATOR_OFFSET_DRIVER_X * math.cos(yaw_rad)
            dy = SPECTATOR_OFFSET_DRIVER_X * math.sin(yaw_rad)
            dz = SPECTATOR_OFFSET_DRIVER_Z
        else:
            dx = -SPECTATOR_OFFSET_BEHIND * math.cos(yaw_rad)
            dy = -SPECTATOR_OFFSET_BEHIND * math.sin(yaw_rad)
            dz = SPECTATOR_OFFSET_UP
        spectator_loc = carla.Location(
            x=v_transform.location.x + dx,
            y=v_transform.location.y + dy,
            z=v_transform.location.z + dz,
        )
        spectator_transform = carla.Transform(spectator_loc, v_transform.rotation)
        spectator.set_transform(spectator_transform)
        _spectator_update_count += 1
        now = time.time()
        if SPECTATOR_DEBUG or (_spectator_update_count <= 3) or (now - _spectator_last_log_time >= 10):
            log_carla("[Spectator] 更新 #%d mode=%s 车辆(%.1f,%.1f,%.1f) yaw=%.1f 镜头(%.1f,%.1f,%.1f)",
                      _spectator_update_count, SPECTATOR_VIEW_MODE,
                      v_transform.location.x, v_transform.location.y, v_transform.location.z,
                      v_transform.rotation.yaw,
                      spectator_loc.x, spectator_loc.y, spectator_loc.z)
            _spectator_last_log_time = now
    except Exception as e:
        _warn("Spectator", "spectator follow failed: %s", e)
        import traceback
        if SPECTATOR_DEBUG:
            log_carla("[Spectator] Traceback: %s", traceback.format_exc())

log_carla("[Control] 车辆控制已启用 CONTROL_HZ=%.0f 档位映射: -1=R 0=N 1=D 2=P", CONTROL_HZ)
_loop_error_count = 0
while True:
    try:
        current_time = time.time()
        if current_time - last_control_time >= (1.0 / CONTROL_HZ):
            _apply_vehicle_control()
            last_control_time = current_time
        if current_time - last_status_time >= (1.0 / STATUS_HZ):
            send_status()
            last_status_time = current_time
        if SPECTATOR_FOLLOW_VEHICLE and current_time - last_spectator_time >= (1.0 / SPECTATOR_UPDATE_HZ):
            _update_spectator_follow_vehicle()
            last_spectator_time = current_time
        if current_time - last_cam_stats_time >= _cam_stats_interval:
            _report_cam_stats()
            last_cam_stats_time = current_time
        _loop_error_count = 0  # 成功后重置连续错误计数
    except KeyboardInterrupt:
        log_carla("收到 KeyboardInterrupt，优雅退出")
        break
    except Exception as e:
        _loop_error_count += 1
        import traceback as _tb
        _err("Main", "主循环迭代异常 #%d: %s", _loop_error_count, e)
        try:
            _log("Main", "主循环 traceback: %s", "".join(_tb.format_exc()))
        except Exception:
            pass
        # 连续失败超过 10 次，强制退出（防止卡死）
        if _loop_error_count >= 10:
            _err("Main", "主循环连续失败 %d 次，强制退出防止死循环", _loop_error_count)
            break
        # 短暂休眠避免 CPU 忙转，再继续下一轮
        time.sleep(0.1)
    time.sleep(0.02)

def cleanup():
    log_carla("退出：停止 MQTT、销毁 actors")
    try:
        mqtt_client.disconnect()
        mqtt_client.loop_stop()
    except Exception as e:
        _warn("MQTT", "cleanup disconnect/loop_stop: %s", e)
    if client:
        try:
            for actor in world.get_actors().filter("*vehicle*"):
                if actor.type_id == "sensor.camera.rgb":
                    world.destroy_actor(actor)
        except Exception as e:
            _warn("CARLA", "cleanup destroy_actor: %s", e)

atexit.register(cleanup)
