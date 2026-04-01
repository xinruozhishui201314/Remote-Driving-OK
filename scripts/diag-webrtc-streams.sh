#!/bin/bash
# ==============================================================================
# 诊断脚本：WebRTC 视频流断连问题定位（增强版）
#
# 用法：
#   sudo bash scripts/diag-webrtc-streams.sh [zlm_host] [duration_seconds]
#   sudo bash scripts/diag-webrtc-streams.sh zlmediakit 60
#
# 自动检测 ZLM secret（从运行中的容器 /opt/media/conf/config.ini 获取），
# 避免硬编码 secret 与实际配置不一致导致的 "Incorrect secret" 误判。
#
# 增强诊断点：
#   1. ZLM 配置完整性验证（secret / rtc.timeoutSec / api.apiDebug）
#   2. 推流源验证（RTMP/H264 注册状态 / aliveSecond）
#   3. WebRTC 会话验证（aliveSeconds / PeerCount）
#   4. RTCP/UDP 错误统计（从 ZLM 日志提取超时次数）
#   5. carla-bridge 推流健康检查
#   6. 帧间隔抓包分析（RTP 包到达间隔）
#   7. 问题定位建议（基于证据链自动推断）
# ==============================================================================

set -euo pipefail

ZLM_HOST="${1:-zlmediakit}"
DURATION="${2:-30}"
ZLM_CONTAINER="${ZLM_CONTAINER:-}"   # 可外部指定，如 teleop-zlmediakit

# ── 检测 ZLM secret（优先级：环境变量 > 运行容器 > deploy/config.ini）─────────────
detect_zlm_secret() {
    # 1. 环境变量
    if [ -n "${ZLM_API_SECRET:-}" ]; then
        echo "${ZLM_API_SECRET}"
        return 0
    fi
    # 2. 运行中的容器（实时获取当前配置）
    for c in "${ZLM_CONTAINER:-}" teleop-zlmediakit zlmediakit; do
        if [ -n "$c" ] && docker ps --format '{{.Names}}' 2>/dev/null | grep -q "^${c}$"; then
            local s
            s=$(docker exec "${c}" grep "^secret=" /opt/media/conf/config.ini 2>/dev/null | cut -d= -f2 | tr -d '\r' | head -1)
            if [ -n "$s" ]; then
                echo "$s"
                return 0
            fi
        fi
    done
    # 3. deploy/config.ini（fallback）
    local s
    s=$(grep "^secret=" "$(dirname "$0")/../deploy/zlm/config.ini" 2>/dev/null | cut -d= -f2 | tr -d '\r' | head -1)
    if [ -n "$s" ]; then
        echo "$s"
        return 0
    fi
    echo ""
}

ZLM_SECRET=$(detect_zlm_secret)

# ── ZLM API 调用（自动加 secret）───────────────────────────────────────────────
zlm_api() {
    local path="$1"
    if [ -z "$ZLM_SECRET" ]; then
        curl -sf "http://${ZLM_HOST}:80${path}" 2>/dev/null || echo ""
    else
        curl -sf "http://${ZLM_HOST}:80${path}?secret=${ZLM_SECRET}" 2>/dev/null || echo ""
    fi
}

# ── Python helper：解析 ZLM 流列表 ────────────────────────────────────────────
python_parse_media_list() {
    python3 -c "
import sys, json
try:
    raw = sys.stdin.read()
    if not raw.strip():
        print('(空响应)')
        sys.exit(0)
    d = json.loads(raw)
    data = d.get('data', [])
    if isinstance(data, list) and data:
        print('  app          stream                          schema  alive(s)')
        print('  ' + '-'*75)
        for m in data:
            app    = m.get('app', '')
            stream = m.get('stream', '')
            schema = m.get('schema', '')
            alive  = m.get('aliveSecond', 0)
            status = '✓' if int(alive or 0) > 0 else '✗'
            print(f'  {status} {app:<12} {stream:<30} {schema:<8} {alive}')
        print(f'  共 {len(data)} 个流')
    else:
        print('  (无活跃流)')
except Exception as e:
    print(f'  解析失败: {e}')
    print(raw[:500])
"
}

# ── Python helper：解析 ZLM WebRTC 会话列表 ───────────────────────────────────
python_parse_session_list() {
    python3 -c "
import sys, json
try:
    raw = sys.stdin.read()
    if not raw.strip():
        print('(空响应)')
        sys.exit(0)
    d = json.loads(raw)
    sessions = d.get('data', d) if isinstance(d, dict) else d
    if isinstance(sessions, list) and sessions:
        print('  PeerID           Stream                         Alive(s)  PeerType')
        print('  ' + '-'*70)
        for s in sessions:
            pid    = str(s.get('peer_id', s.get('id', '?')))[:20]
            stream = str(s.get('stream_id', s.get('stream', '')))[:30]
            alive  = s.get('alive_second', s.get('aliveSecond', s.get('alive_sec', 0)))
            ptype  = s.get('peer_type', s.get('type', '?'))
            print(f'  {pid:<20} {stream:<30} {alive:<10} {ptype}')
        print(f'  共 {len(sessions)} 个 WebRTC 会话')
    else:
        print('  (无活跃 WebRTC 会话)')
except Exception as e:
    print(f'  解析失败: {e}')
    print(raw[:500])
"
}

# ── Python helper：分析 PCAP RTP 间隔 ──────────────────────────────────────────
python_analyze_rtp_intervals() {
    python3 -c "
import sys, subprocess, struct, time

def parse_pcap(filename):
    try:
        with open(filename, 'rb') as f:
            # PCAP global header
            header = f.read(24)
            if len(header) < 24:
                return None
            # Read each packet
            intervals = []
            prev_ts = None
            while True:
                pkt_hdr = f.read(16)
                if len(pkt_hdr) < 16:
                    break
                ts_sec, ts_usec, incl_len = struct.unpack('III', pkt_hdr[0:12])
                data = f.read(incl_len)
                if not data or len(data) < 42:
                    continue
                # Ethernet + IP + UDP = 42 bytes
                ip_proto = data[23]
                if ip_proto != 17:  # UDP
                    continue
                ts = ts_sec + ts_usec / 1e6
                if prev_ts is not None:
                    intervals.append(ts - prev_ts)
                prev_ts = ts
            return intervals
    except Exception as e:
        return None

filename = '$1'
intervals = parse_pcap(filename)
if intervals and len(intervals) > 5:
    # Convert to ms
    intervals_ms = [i * 1000 for i in intervals]
    avg = sum(intervals_ms) / len(intervals_ms)
    max_v = max(intervals_ms)
    min_v = min(intervals_ms)
    # Count >100ms, >500ms
    over_100 = sum(1 for v in intervals_ms if v > 100)
    over_500 = sum(1 for v in intervals_ms if v > 500)
    print(f'RTP 包数={len(intervals_ms)} 间隔(ms): avg={avg:.1f} min={min_v:.1f} max={max_v:.1f}')
    print(f'  >100ms: {over_100} ({100*over_100/len(intervals_ms):.0f}%)  >500ms: {over_500}')
    if over_500 > 0:
        print('  !! 严重抖动: >500ms 间隔 > 0 表示严重丢包或 ZLM 发送不均匀')
    elif over_100 > len(intervals_ms) * 0.1:
        print('  !  中度抖动: >10% 的包间隔 >100ms')
    else:
        print('  OK 间隔正常')
else:
    print(f'RTP 包数不足: {len(intervals) if intervals else 0} (< 6，无统计意义)')
"
}

# ── 颜色定义 ─────────────────────────────────────────────────────────────────
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; CYAN='\033[0;36m'; NC='\033[0m'
log()  { echo -e "${GREEN}[$(date '+%H:%M:%S')]${NC} $*"; }
warn() { echo -e "${YELLOW}[$(date '+%H:%M:%S')] WARN:${NC} $*" | tee -a "$LOG_FILE"; }
err()  { echo -e "${RED}[$(date '+%H:%M:%S')] ERROR:${NC} $*" | tee -a "$LOG_FILE" >&2; }
info() { echo -e "${CYAN}[$(date '+%H:%M:%S')] INFO:${NC} $*"; }

PCAP_FILE="/tmp/zlm_webrtc_$(date +%Y%m%d_%H%M%S).pcap"
LOG_FILE="/tmp/zlm_diag_$(date +%Y%m%d_%H%M%S).log"

exec > >(tee -a "$LOG_FILE") 2>&1

# ── 打印报告头 ───────────────────────────────────────────────────────────────
echo ""
echo "================================================================================"
echo " WebRTC 视频流诊断（增强版）— $(date '+%Y-%m-%d %H:%M:%S')"
echo "================================================================================"
echo " ZLM_HOST=$ZLM_HOST  DURATION=${DURATION}s  PCAP=$PCAP_FILE"
SECRET_SOURCE="(空)"
if [ -n "${ZLM_API_SECRET:-}" ]; then
    SECRET_SOURCE="环境变量 ZLM_API_SECRET"
elif docker ps --format '{{.Names}}' 2>/dev/null | grep -q "^${ZLM_CONTAINER:-teleop-zlmediakit}$"; then
    SECRET_SOURCE="运行中容器 ${ZLM_CONTAINER:-teleop-zlmediakit}"
else
    SECRET_SOURCE="deploy/zlm/config.ini"
fi
echo " ZLM_SECRET 检测来源: $SECRET_SOURCE"
echo " ZLM_SECRET: ${ZLM_SECRET:+已检测到（前16字符: $(echo "$ZLM_SECRET" | head -c 16)...）} 长度=${#ZLM_SECRET}"
echo ""

# ── Step 0. ZLM 配置完整性验证 ──────────────────────────────────────────────────
log "0. ZLM 配置完整性验证..."
echo ""

ZLM_RTC_TIMEOUT=""
ZLM_API_DEBUG=""
ZLM_ACTUAL_SECRET=""
for c in teleop-zlmediakit zlmediakit "${ZLM_CONTAINER:-}"; do
    if docker ps --format '{{.Names}}' 2>/dev/null | grep -q "^${c}$"; then
        info "从容器 ${c} 获取实际配置..."
        ZLM_ACTUAL_SECRET=$(docker exec "${c}" grep "^secret=" /opt/media/conf/config.ini 2>/dev/null | cut -d= -f2 | tr -d '\r' | head -1)
        ZLM_RTC_TIMEOUT=$(docker exec "${c}" grep "^timeoutSec=" /opt/media/conf/config.ini 2>/dev/null | grep "rtc\." | cut -d= -f2 | head -1)
        ZLM_API_DEBUG=$(docker exec "${c}" grep "^apiDebug=" /opt/media/conf/config.ini 2>/dev/null | head -1)
        break
    fi
done

EXPECTED_SECRET="035c73f7-bb6b-4889-a715-d9eb2d1925cc"
EXPECTED_RTC_TIMEOUT="120"
EXPECTED_API_DEBUG="0"

check_ok() {
    [ "$1" = "$2" ] && echo -e "  ${GREEN}✓ OK${NC}" || echo -e "  ${RED}✗ $3${NC}"
}

echo "  ┌─────────────────────────────────────────────────────────────────────────────┐"
echo "  │ 配置项                   │ 预期值               │ 实际值                   │ 状态   │"
echo "  ├─────────────────────────────────────────────────────────────────────────────┤"
printf "  │ [api] secret           │ %.40s │ %.40s │\n" "$EXPECTED_SECRET" "${ZLM_ACTUAL_SECRET:-未检测}"
printf "  │                         │                             │                             │  %-6s │\n" "$(if [ "$ZLM_ACTUAL_SECRET" = "$EXPECTED_SECRET" ]; then echo -e "${GREEN}✓ OK${NC}"; else echo -e "${RED}✗ 不匹配${NC}"; fi)"
printf "  │ [api] apiDebug         │ $EXPECTED_API_DEBUG                  │ ${ZLM_API_DEBUG:-未检测}                       │  %-6s │\n" "$(if [ "${ZLM_API_DEBUG#*=}" = "$EXPECTED_API_DEBUG" ]; then echo -e "${GREEN}✓ OK${NC}"; else echo -e "${RED}✗ != $EXPECTED_API_DEBUG${NC}"; fi)"
printf "  │ [rtc] timeoutSec      │ >= $EXPECTED_RTC_TIMEOUT (>=30s)  │ ${ZLM_RTC_TIMEOUT:-未检测}                 │  %-6s │\n" "$(if [ "${ZLM_RTC_TIMEOUT:-0}" -ge 30 ] 2>/dev/null; then echo -e "${GREEN}✓ OK${NC}"; else echo -e "${RED}✗ <30s${NC}"; fi)"
echo "  └─────────────────────────────────────────────────────────────────────────────┘"
echo ""

if [ "$ZLM_ACTUAL_SECRET" != "$EXPECTED_SECRET" ] || [ -z "$ZLM_ACTUAL_SECRET" ]; then
    warn "ZLM secret 不匹配或未检测到！"
    warn "  当前实际 secret: '${ZLM_ACTUAL_SECRET:-未检测}'"
    warn "  预期 secret:     '${EXPECTED_SECRET}'"
    warn "  可能原因: deploy/zlm/config.ini 未挂载到 ZLM 容器"
    warn "  修复方法: 'docker compose restart zlmediakit' 或 'docker compose down && docker compose up -d'"
fi
if [ "${ZLM_RTC_TIMEOUT:-0}" -lt 30 ] 2>/dev/null; then
    warn "rtc.timeoutSec=${ZLM_RTC_TIMEOUT:-未检测} < 30s"
    warn "  WebRTC 会话将在 RTCP 超时后被 ZLM 关闭（症状：约15-30s后 PeerConnection Closed）"
fi
echo ""

# ── Step 1. ZLM 媒体流列表 ───────────────────────────────────────────────────
log "1. 查询 ZLM 媒体流列表..."
echo ""
ZLM_LIST=$(zlm_api "/index/api/getMediaList")
if [ -z "$ZLM_LIST" ]; then
    warn "ZLM API 请求失败（curl 返回空）：检查 ZLM 是否可达"
    warn "  curl -v http://${ZLM_HOST}:80/index/api/getMediaList"
    warn "  docker logs \$(docker ps --filter name=zlm -q) 2>&1 | tail -5"
    echo "  curl 原始响应: $(curl -s "http://${ZLM_HOST}:80/index/api/getMediaList" 2>&1 | head -c 300)"
elif echo "$ZLM_LIST" | python3 -m json.tool >/dev/null 2>&1; then
    echo "$ZLM_LIST" | python3 -m json.tool 2>/dev/null | head -80
    STREAM_COUNT=$(echo "$ZLM_LIST" | python3 -c "import sys,json; d=json.load(sys.stdin); print(len(d.get('data',[])))" 2>/dev/null || echo "?")
    log "活跃流数量: $STREAM_COUNT"
    echo ""
    info "推流源健康检查（RTMP → ZLM）："
    echo "$ZLM_LIST" | python_parse_media_list
    echo ""
    # 关键路径：检查 VIN-prefixed 流是否存在（多车隔离）
    CAM_STREAMS=$(echo "$ZLM_LIST" | python3 -c "
import sys,json
d=json.load(sys.stdin)
for m in d.get('data',[]):
    s=m.get('stream','')
    if 'cam_front' in s or 'cam_rear' in s or 'cam_left' in s or 'cam_right' in s:
        print(s)
" 2>/dev/null)
    if [ -n "$CAM_STREAMS" ]; then
        log "检测到相机流: $(echo "$CAM_STREAMS" | tr '\n' ' ')"
    else
        warn "未检测到 cam_* 流！检查 carla-bridge MQTT start_stream 是否被正确处理。"
    fi
else
    warn "ZLM API 返回非 JSON（可能是 secret 错误）：${ZLM_LIST:0:300}"
    if echo "$ZLM_LIST" | grep -qi "incorrect secret"; then
        err "ZLM 返回 'Incorrect secret'！"
        err "  当前检测到的 secret='${ZLM_SECRET:-空}' 不正确。"
        err "  修复方法: 'docker compose restart zlmediakit'"
        err "  手动验证: docker exec teleop-zlmediakit grep '^secret=' /opt/media/conf/config.ini"
    fi
fi
echo ""

# ── Step 2. ZLM WebRTC 会话列表 ───────────────────────────────────────────────
log "2. 查询 ZLM WebRTC 会话列表..."
echo ""
ZLM_SESSIONS=$(zlm_api "/index/api/getAllSession")
if [ -n "$ZLM_SESSIONS" ] && echo "$ZLM_SESSIONS" | python3 -m json.tool >/dev/null 2>&1; then
    echo "$ZLM_SESSIONS" | python_parse_session_list
    # 提取每个会话的 aliveSecond
    echo ""
    info "WebRTC 会话 aliveSecond（数值越小说明越接近超时）："
    echo "$ZLM_SESSIONS" | python3 -c "
import sys,json
d=json.load(sys.stdin)
sessions = d.get('data', d) if isinstance(d, dict) else d
for s in (sessions if isinstance(sessions, list) else []):
    sid   = str(s.get('peer_id', s.get('id', '?')))[:20]
    alive = s.get('alive_second', s.get('aliveSecond', s.get('alive_sec', 0)))
    print(f'  {sid:<20} alive={alive}s')
" 2>/dev/null
else
    echo "  (无法获取 WebRTC 会话：ZLM API secret 不正确或 secret 为空)"
fi
echo ""

# ── Step 3. ZLM 日志错误统计 ─────────────────────────────────────────────────
log "3. ZLM RTCP/UDP 错误统计（从 ZLM 容器日志）..."
echo ""
RTCP_ERR_COUNT=0
WEBRTC_CLOSE_COUNT=0
RTMP_ERR_COUNT=0

for c in teleop-zlmediakit zlmediakit; do
    if docker ps --format '{{.Names}}' 2>/dev/null | grep -q "^${c}$"; then
        RTCP_ERR_COUNT=$(docker logs "${c}" 2>&1 | grep -c "接受rtp/rtcp/datachannel超时\|rtcp.*timeout" || echo 0)
        WEBRTC_CLOSE_COUNT=$(docker logs "${c}" 2>&1 | grep -c "WebRtcPlayer.*onDestory\|WebRtcSession.*onError\|播放.*结束" || echo 0)
        RTMP_ERR_COUNT=$(docker logs "${c}" 2>&1 | grep -c "publish.*fail\|rtmp.*error\|RTMP.*ERROR" || echo 0)
        info "ZLM 容器: ${c}"
        info "最近 30 条 WebRTC/RTMP 相关日志："
        docker logs "${c}" 2>&1 | grep -E "WebRtc|接受rtp|RTCP|publish|rtmp|onDestory|onError|播放.*结束" | tail -30 | sed 's/^/  /'
        break
    fi
done

echo ""
echo "  ┌──────────────────────────────────────────────┬───────┬──────────────────────────────────────────────┐"
echo "  │ 指标                                       │ 数量  │ 说明                                         │"
echo "  ├──────────────────────────────────────────────┼───────┼──────────────────────────────────────────────┤"
printf  "  │ RTCP/UDP 超时关闭                         │ %5s │ (>0 = libdatachannel 未发 RTCP RR)       │\n" "$RTCP_ERR_COUNT"
printf "  │ WebRTC 会话被关闭                         │ %5s │ (>0 = 超时 / ICE 断开 / RTCP 超时)       │\n" "$WEBRTC_CLOSE_COUNT"
printf "  │ RTMP publish 失败                         │ %5s │ (>0 = carla-bridge → ZLM 推流异常)      │\n" "$RTMP_ERR_COUNT"
echo "  └──────────────────────────────────────────────┴───────┴──────────────────────────────────────────────┘"
echo ""

if [ "$RTCP_ERR_COUNT" -gt 0 ]; then
    warn "检测到 RTCP 超时 ${RTCP_ERR_COUNT} 次！"
    warn "  原因分析："
    warn "    1. ZLM rtc.timeoutSec 过短（当前=${ZLM_RTC_TIMEOUT:-未检测}，应 >=120）"
    warn "    2. libdatachannel recvonly 模式未发送 RTCP Receiver Report"
    warn "    3. 防火墙阻断 UDP 8000-65535 端口（ZLM ← Client RTCP 路径）"
    warn "  修复步骤："
    warn "    1. 确认 deploy/zlm/config.ini 已挂载：docker exec teleop-zlmediakit grep timeoutSec /opt/media/conf/config.ini"
    warn "    2. 重启 ZLM：docker compose restart zlmediakit"
    warn "    3. 重新拉流并观察"
fi
echo ""

# ── Step 4. carla-bridge 推流状态 ─────────────────────────────────────────────
log "4. carla-bridge 推流状态..."
echo ""
CARLA_CONTAINER=""
for c in carla-server remote-driving-carla-bridge carla-bridge; do
    if docker ps --format '{{.Names}}' 2>/dev/null | grep -q "^${c}$"; then
        CARLA_CONTAINER="$c"
        break
    fi
done

if [ -n "$CARLA_CONTAINER" ]; then
    info "检测到容器: $CARLA_CONTAINER"
    docker logs "$CARLA_CONTAINER" 2>&1 | grep -E "worker|stdin|ffmpeg|ERROR|推流|spawn|stream|cam_front|cam_rear|cam_left|cam_right|start_stream" | tail -30 | sed 's/^/  /'
    WORKER_RUNNING=$(docker logs "$CARLA_CONTAINER" 2>&1 | grep -c "worker.*启动\|ffmpeg.*flv" || echo 0)
    STREAM_STARTED=$(docker logs "$CARLA_CONTAINER" 2>&1 | grep -c "start_stream\|spawn_cameras" || echo 0)
    echo ""
    echo "  worker 启动次数: $WORKER_RUNNING  | start_stream 接收次数: $STREAM_STARTED"
    if [ "$WORKER_RUNNING" -eq 0 ]; then
        warn "carla-bridge 未检测到推流 worker 启动！"
        warn "  可能原因：MQTT start_stream 消息未被处理"
        warn "  检查：docker logs $CARLA_CONTAINER | grep start_stream"
    fi
    if [ "$STREAM_STARTED" -eq 0 ]; then
        warn "carla-bridge 未收到 start_stream 消息！"
        warn "  检查 MQTT 链路：backend → carla-bridge 是否畅通"
    fi
else
    warn "未找到 carla-bridge 容器（carla-server / remote-driving-carla-bridge）"
fi
echo ""

# ── Step 5. 抓包 ──────────────────────────────────────────────────────────────
log "5. 开始抓包（${DURATION}s）..."
echo ""
log "  保存到: $PCAP_FILE"
log "  说明：tcpdump 需 root 权限；UDP 包包含 ZLM→Client RTP 视频流"
echo ""

DOCKER_IF=""
for IF in docker0; do
    if ip link show "$IF" &>/dev/null; then
        DOCKER_IF="$IF"
        break
    fi
done
# fallback: 找到 teleop-network 的 bridge 接口
if [ -z "$DOCKER_IF" ]; then
    for br in $(docker network ls --format '{{.Name}}' 2>/dev/null | grep -v "^bridge$"); do
        candidate=$(ip -br addr show 2>/dev/null | awk '{print $1}' | while read name; do
            ip link show "$name" 2>/dev/null | grep -q "state UP" && echo "$name" && break
        done | head -1)
        [ -n "$candidate" ] && DOCKER_IF="$candidate" && break
    done
fi
DOCKER_IF="${DOCKER_IF:-docker0}"

if ip link show "$DOCKER_IF" &>/dev/null; then
    log "  使用接口: $DOCKER_IF"
    timeout "${DURATION}" tcpdump -i "$DOCKER_IF" -n udp -w "$PCAP_FILE" 2>/dev/null &
    TCPD_PID=$!
    log "  tcpdump PID=$TCPD_PID 运行中（Ctrl+C 提前停止）..."
    wait $TCPD_PID
    RC=$?
    if [ $RC -eq 0 ] || [ $RC -eq 124 ]; then
        log "抓包完成 (exit=$RC)"
    else
        warn "tcpdump 异常退出 (exit=$RC)：需要 sudo 或确认接口存在"
    fi
else
    warn "接口 $DOCKER_IF 不存在，跳过抓包（需要 sudo bash 重新运行）"
fi
echo ""

# ── Step 6. PCAP 分析 ────────────────────────────────────────────────────────
if [ -f "$PCAP_FILE" ] && [ -s "$PCAP_FILE" ]; then
    log "6. PCAP 分析..."
    echo ""
    PKT_COUNT=$(tcpdump -r "$PCAP_FILE" 2>/dev/null | wc -l)
    RTP_COUNT=$(tcpdump -r "$PCAP_FILE" -n 2>/dev/null | grep -cE "RTP|H264" || echo 0)
    UDP_COUNT=$(tcpdump -r "$PCAP_FILE" -n 2>/dev/null | grep -c "UDP" || echo 0)
    log "  总包数=$PKT_COUNT  RTP/H264包=$RTP_COUNT  UDP包=$UDP_COUNT"
    echo ""
    echo "  唯一源 IP（抓包期间发包的主机）："
    tcpdump -r "$PCAP_FILE" -n 2>/dev/null | awk '{print $3}' | cut -d. -f1-4 | sort -u | head -10 | sed 's/^/    /'
    echo ""
    echo "  最近 20 个包："
    tcpdump -r "$PCAP_FILE" -n 2>/dev/null | tail -20 | sed 's/^/    /'
    echo ""
    info "RTP 包间隔分析（判断 UDP 网络抖动）："
    if python_analyze_rtp_intervals "$PCAP_FILE" 2>&1 | tee -a "$LOG_FILE"; then
        :  # 已通过 python_analyze_rtp_intervals 输出
    fi
    echo ""
    info "快速 RTP 分析命令："
    echo "    tcpdump -r $PCAP_FILE -n | grep RTP | awk '{print \$1}' | sort -n | awk '{print NR, \$0}' | awk '{if(NR>1)print \$1, (\$2-prev)*1000\"ms\"; prev=\$2}' | sort -rn | head -10"
    echo ""
else
    warn "未捕获到 PCAP 文件（需 sudo bash 运行此脚本）"
fi
echo ""

# ── Step 7. 综合诊断建议 ─────────────────────────────────────────────────────
log "7. 综合诊断结论与下一步操作..."
echo ""
echo "  ┌──────────────────────────────────────────────────────────────────────────────────────────┐"
echo "  │ 优先级 │ 诊断结论                                                    │ 下一步                          │"
echo "  ├──────────────────────────────────────────────────────────────────────────────────────────┤"

# 综合判断
HAS_STREAMS=false
if [ -n "$ZLM_LIST" ] && echo "$ZLM_LIST" | python3 -m json.tool >/dev/null 2>&1; then
    CNT=$(echo "$ZLM_LIST" | python3 -c "import sys,json; d=json.load(sys.stdin); print(len(d.get('data',[])))" 2>/dev/null || echo 0)
    [ "$CNT" -gt 0 ] 2>/dev/null && HAS_STREAMS=true
fi

if [ "$RTCP_ERR_COUNT" -gt 0 ]; then
    echo "  │  P0    │ RTCP 超时 ${RTCP_ERR_COUNT} 次 → libdatachannel 未发 RTCP RR      │ 确认 ZLM rtc.timeoutSec=120，重启 ZLM 容器  │"
fi
if [ "$ZLM_ACTUAL_SECRET" != "$EXPECTED_SECRET" ]; then
    echo "  │  P0    │ ZLM secret 不匹配 → API 请求失败，诊断链路断裂  │ docker compose restart zlmediakit         │"
fi
if [ "$HAS_STREAMS" = false ]; then
    echo "  │  P0    │ ZLM 无活跃流 → carla-bridge 未推流或未收到 start_stream  │ docker logs carla-server | grep start_stream │"
fi
if [ "${ZLM_RTC_TIMEOUT:-0}" -lt 30 ] 2>/dev/null; then
    echo "  │  P0    │ rtc.timeoutSec=${ZLM_RTC_TIMEOUT:-?} < 30s                     │ 挂载 deploy/zlm/config.ini，重启 ZLM      │"
fi
if [ "$HAS_STREAMS" = true ] && [ "$RTCP_ERR_COUNT" -eq 0 ] && [ "$RTMP_ERR_COUNT" -eq 0 ]; then
    echo "  │  P2    │ ZLM 有流、无 RTCP 超时 → 问题可能在客户端渲染层      │ docker logs teleop-client-dev | grep Video   │"
fi
if [ "$HAS_STREAMS" = true ] && [ "$RTMP_ERR_COUNT" -gt 0 ]; then
    echo "  │  P1    │ ZLM 有流但 RTMP publish 失败 ${RTMP_ERR_COUNT} 次            │ docker logs carla-server | grep ffmpeg     │"
fi

echo "  │  —     │ 所有检查通过但仍有间歇性断连                      │ 手动抓包 sudo tcpdump -i docker0 -n udp    │"
echo "  └──────────────────────────────────────────────────────────────────────────────────────────┘"
echo ""

# ── Step 8. 快速命令汇总 ────────────────────────────────────────────────────
log "8. 快速诊断命令汇总..."
echo ""
echo "  # 1. 确认 ZLM 配置已正确挂载"
echo "  docker exec teleop-zlmediakit grep -E 'secret=|timeoutSec=' /opt/media/conf/config.ini"
echo ""
echo "  # 2. 查看 WebRTC RTCP 超时详情"
echo "  docker logs teleop-zlmediakit 2>&1 | grep -E '接受rtp/rtcp/datachannel超时|WebRtcPlayer.*onDestory' | tail -20"
echo ""
echo "  # 3. 实时监控推流状态"
echo "  watch -n 2 'curl -s http://localhost:80/index/api/getMediaList?secret=035c73f7-bb6b-4889-a715-d9eb2d1925cc | python3 -c \"import sys,json;d=json.load(sys.stdin);print(len(d.get(chr(101)+chr(100)+chr(97)+chr(116)+chr(61)+chr(32)+chr(91)+chr(93)))\" )'"
echo ""
echo "  # 4. 实时抓包分析 RTP 抖动（需 sudo）"
echo "  sudo tcpdump -i docker0 -n 'udp port 8000' -c 1000 -w /tmp/webrtc.pcap &"
echo "  sleep 30; kill %1; tcpdump -r /tmp/webrtc.pcap -n | grep RTP | awk '{print \$1}' | sort -n | awk '{if(NR>1)printf \"%dms\n\",(\$2-prev)*1000;prev=\$2}' | sort -rn | head -5"
echo ""

log "诊断完成。日志文件: $LOG_FILE"
[ -f "$PCAP_FILE" ] && [ -s "$PCAP_FILE" ] && log "PCAP 文件: $PCAP_FILE"
echo ""
