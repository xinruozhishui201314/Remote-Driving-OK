#!/bin/bash
# ==============================================================================
# 诊断脚本：CARLA-Bridge 推流质量分析
#
# 用法：
#   sudo bash scripts/diag-carla-bridge.sh [container_name] [duration_seconds]
#   sudo bash scripts/diag-carla-bridge.sh carla-server 60
#
# 功能：
#   1. 从 carla-server 容器日志解析推流质量报告（[推流质量] / [testsrc]）
#   2. 从 ZLM API 查询 RTMP 流注册状态（aliveSeconds / totalBytes）
#   3. 对比期望帧率（15fps）与实际推流帧率，给出根因定位建议
#   4. 检测是否已自动降级到 testsrc
# ==============================================================================

set -euo pipefail

CARLA_CONTAINER="${1:-carla-server}"
DURATION="${2:-60}"
ZLM_HOST="${ZLM_HOST:-zlmediakit}"
ZLM_SECRET="${ZLM_SECRET:-}"

# ── 加载 ZLM secret ────────────────────────────────────────────────────────────
if [ -z "$ZLM_SECRET" ]; then
    for c in teleop-zlmediakit zlmediakit; do
        if docker ps --format '{{.Names}}' 2>/dev/null | grep -q "^${c}$"; then
            ZLM_SECRET=$(docker exec "${c}" grep "^secret=" /opt/media/conf/config.ini 2>/dev/null | cut -d= -f2 | tr -d '\r' | head -1)
            [ -n "$ZLM_SECRET" ] && break
        fi
    done
fi

# ── ZLM API ────────────────────────────────────────────────────────────────────
zlm_api() {
    local path="$1"
    if [ -n "$ZLM_SECRET" ]; then
        curl -sf "http://${ZLM_HOST}:80${path}?secret=${ZLM_SECRET}" 2>/dev/null || echo ""
    else
        curl -sf "http://${ZLM_HOST}:80${path}" 2>/dev/null || echo ""
    fi
}

# ── 日志解析函数 ───────────────────────────────────────────────────────────────
python_parse_logs() {
    python3 -c "
import sys, re, json, os

container = '$CARLA_CONTAINER'
duration = int('$DURATION')

# 从容器日志提取
import subprocess
cmd = ['docker', 'logs', '--tail', '2000', container]
try:
    result = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
    raw = result.stdout + result.stderr
except Exception as e:
    print(f'获取容器日志失败: {e}')
    sys.exit(0)

lines = raw.splitlines()

# 分类：相机帧到达 / 推流质量 / testsrc 降级
camera_arrival = {}  # cam_id -> [(ts, fps), ...]
push_quality = {}   # cam_id -> [(ts, frames, fps), ...]
testsrc_active = set()
spawn_fail = set()
ffmpeg_crash = set()
warnings = []

for line in lines:
    # 相机帧到达率
    m = re.search(r'\[carla-bridge:Camera\]\s+stream_id=(\w+) 到达帧([0-9.]+)fps/([0-9]+)帧/([0-9.]+)s', line)
    if m:
        cid, fps, frames, elapsed = m.group(1), float(m.group(2)), int(m.group(3)), float(m.group(4))
        camera_arrival.setdefault(cid, []).append({'fps': fps, 'frames': frames, 'elapsed': elapsed})

    # testsrc 降级
    if '[testsrc]' in line and ('降级' in line or '启动' in line or 'testsrc' in line.lower()):
        m2 = re.search(r'(?:stream_id|cam_)(cam_\w+)', line)
        if m2:
            cid = m2.group(1)
            if '降级' in line:
                testsrc_active.add(cid)
                warnings.append(f'WARN: {cid} 已降级到 testsrc')
            elif '启动' in line and 'FORCE' not in line:
                testsrc_active.add(cid)

    # testsrc 强制模式
    if 'FORCE_TESTSRC' in line:
        warnings.append('WARN: FORCE_TESTSRC=1，全程使用 testsrc')

    # ffmpeg 崩溃
    if 'ffmpeg' in line and ('异常退出' in line or 'stdin 写入失败' in line):
        m3 = re.search(r'stream_id=(\w+)', line)
        if m3:
            ffmpeg_crash.add(m3.group(1))
            warnings.append(f'WARN: {m3.group(1)} ffmpeg 进程崩溃')

    # spawn 失败
    if 'spawn_actor camera failed' in line:
        m4 = re.search(r'cam_(\w+)', line)
        if m4:
            cid = 'cam_' + m4.group(1)
            spawn_fail.add(cid)
            warnings.append(f'WARN: {cid} 相机 spawn 失败')

    # 推流质量
    m5 = re.search(r'\[推流质量\]\s+stream_id=(\w+)[^0-9]*([0-9]+)s内推帧(\d+)\s+瞬时FPS=([0-9.]+)', line)
    if m5:
        cid, elapsed2, frames2, fps2 = m5.group(1), float(m5.group(2)), int(m5.group(3)), float(m5.group(4))
        push_quality.setdefault(cid, []).append({'elapsed': elapsed2, 'frames': frames2, 'fps': fps2})

print('=== CARLA-Bridge 推流诊断报告 ===')
print(f'容器: {container}  |  日志范围: 最近 2000 行')
print()

# 相机帧到达率分析
print('--- 1. CARLA 相机帧到达率 ---')
if camera_arrival:
    for cid, records in sorted(camera_arrival.items()):
        latest = records[-1] if records else {}
        fps_vals = [r['fps'] for r in records]
        avg_fps = sum(fps_vals) / len(fps_vals) if fps_vals else 0
        status = 'OK' if avg_fps >= 10 else ('LOW' if avg_fps >= 3 else 'CRITICAL')
        print(f'  {cid}: 平均 {avg_fps:.1f} fps (最新 {latest.get(\"fps\", 0):.1f} fps) [{status}]')
        if status != 'OK':
            print(f'    -> 原因: CARLA 相机层面帧率低，可能是 DISPLAY/GPU/传感器配置问题')
else:
    print('  (无帧到达率记录，需确认 carla-bridge 是否已启动推流)')

print()

#推流质量分析
print('--- 2. ffmpeg RTMP 推流质量 ---')
if push_quality:
    for cid, records in sorted(push_quality.items()):
        latest = records[-1] if records else {}
        fps_vals = [r['fps'] for r in records]
        avg_fps = sum(fps_vals) / len(fps_vals) if fps_vals else 0
        total_f = sum(r['frames'] for r in records)
        status = 'OK' if avg_fps >= 10 else ('LOW' if avg_fps >= 3 else 'CRITICAL')
        print(f'  {cid}: 平均 {avg_fps:.1f} fps / 共 {total_f} 帧 / 最新 {latest.get(\"fps\", 0):.1f} fps [{status}]')
        if status != 'OK':
            print(f'    -> 原因: 若相机到达率正常但推流率低 -> ffmpeg 编码慢或 ZLM RTMP 连接慢')
            print(f'    -> 原因: 若相机到达率也低 -> CARLA 渲染问题（见 §1）')
else:
    print('  (无推流质量记录)')

print()

# testsrc 降级检测
print('--- 3. testsrc 降级状态 ---')
if testsrc_active:
    for cid in sorted(testsrc_active):
        print(f'  {cid}: 已降级到 testsrc（正常模式下跳过此路）')
else:
    print('  未降级，全部使用 CARLA 相机')

print()

# 异常状态
print('--- 4. 异常事件 ---')
if warnings:
    for w in warnings[:10]:
        print(f'  {w}')
else:
    print('  无异常事件')

print()

# ZLM RTMP 流状态
print('--- 5. ZLM RTMP 流注册状态 ---')
import subprocess
cmd2 = ['curl', '-sf', f'http://{os.environ.get(\"ZLM_HOST\", \"zlmediakit\")}:80/api/stream/list?secret={os.environ.get(\"ZLM_SECRET\", \"\")}']
try:
    res2 = subprocess.run(cmd2, capture_output=True, text=True, timeout=10)
    d = json.loads(res2.stdout) if res2.stdout else {}
    streams = d.get('data', [])
    if streams:
        for s in streams:
            app = s.get('appName', '')
            stream = s.get('stream', '')
            schema = ','.join(s.get('schema', []))
            alive = s.get('aliveSecond', 0)
            total_bytes = s.get('totalBytes', 0)
            print(f'  {app}/{stream} schema={schema} alive={alive}s totalBytes={total_bytes}')
    else:
        print('  (无注册流，可能推流未到达 ZLM)')
except Exception as e:
    print(f'  ZLM API 查询失败: {e}')

print()

# 根因定位建议
print('=== 根因定位建议 ===')
has_critical = any(
    (avg_fps < 3) for fps_vals in [r['fps'] for records in camera_arrival.values() for r in records]
    for avg_fps in [sum(r['fps'] for r in records) / max(1, len(records)) for records in [list(camera_arrival.values())[0]]]
)
fps_vals_all = [r['fps'] for records in camera_arrival.values() for r in records]
avg_all = sum(fps_vals_all) / len(fps_vals_all) if fps_vals_all else 0
has_critical = avg_all < 3

if avg_all < 3:
    print('ROOT CAUSE: CARLA 相机帧率 < 3 fps（极低）')
    print('  可能性 1 [最可能]: DISPLAY 不匹配')
    print('    - CARLA 容器中 DISPLAY 与主机 X 服务器不一致')
    print('    - 主机用 :1 但容器用 :0，或反之')
    print('    - 排查: docker exec carla-server env | grep DISPLAY')
    print('    - 修复: 在 .env 中设置 CARLA_VDISP=:1 或 :0 对齐主机')
    print('  可能性 2: GPU 驱动/渲染问题')
    print('    - NVIDIA DRIVER CAPABILITIES 未设为 all')
    print('    - CARLA 版本与 GPU 驱动不兼容')
    print('  可能性 3: CARLA 仿真器渲染速度慢')
    print('    - 使用 Town01/Town04 轻量地图测试')
    print('    - 降低 CAMERA_FPS 到 10 或 5')
elif avg_all < 10:
    print('ROOT CAUSE: CARLA 相机帧率 3-10 fps（偏低）')
    print('  建议: 降低 CAMERA_FPS 到 10 或检查 GPU 利用率')
    print('  nvidia-smi -l 1 观察 GPU 使用率')
else:
    print('推流帧率正常 (>= 10 fps)，问题可能在 ZLM 或 WebRTC 侧')
    print('  建议: 检查 scripts/diag-webrtc-streams.sh')
"
}

echo ""
echo "=============================================================================="
echo "CARLA-Bridge 推流质量诊断 — $(date '+%Y-%m-%d %H:%M:%S')"
echo "=============================================================================="
echo ""

python_parse_logs

echo ""
echo "=============================================================================="
echo "诊断完成"
echo "=============================================================================="
echo "如需强制使用 testsrc（绕过 CARLA 相机验证 WebRTC 链路）："
echo "  1. 在 .env 中设置 FORCE_TESTSRC=1"
echo "  2. 重启 carla-server: docker-compose -f docker-compose.carla.yml up -d"
echo ""
echo "如需查看实时日志："
echo "  docker logs -f $CARLA_CONTAINER 2>&1 | grep -E '\[推流质量\]|\[Camera\]|\[testsrc\]|\[WARN\]'"
echo ""
