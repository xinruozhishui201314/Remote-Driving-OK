#!/usr/bin/env bash
# ZLM / UDP 侧快速证据：当前 teleop app 下已注册媒体（推流是否在册）。
# 与客户端日志 [Client][WebRTC][Diag] 配合：若此处无 {VIN}_cam_* 或 cam_*，优先查车端/Bridge 推流。
#
# 用法示例：
#   ZLM_URL=http://127.0.0.1:80 ZLM_SECRET=<secret> VIN=carla-sim-001 bash scripts/diag-zlm-streams.sh
#   bash scripts/diag-zlm-streams.sh          # 自动检测 secret（从容器或 deploy/config.ini）
set -euo pipefail

ZLM_URL="${ZLM_URL:-http://127.0.0.1:80}"
APP="${ZLM_APP:-teleop}"
# VIN：用于检测 VIN-prefixed 流名（{VIN}_cam_front 等）；为空时回退检测无前缀流名
VIN="${VIN:-${VEHICLE_VIN:-}}"
VIN_PREFIX="${VIN:+${VIN}_}"

# ── 自动检测 ZLM secret（优先级：环境变量 > 运行容器 > deploy/config.ini）─────────
detect_secret() {
    # 1. 环境变量
    if [ -n "${ZLM_SECRET:-}" ]; then
        echo "${ZLM_SECRET}"
        return 0
    fi
    # 2. 运行中的容器
    for c in teleop-zlmediakit zlmediakit; do
        if docker ps --format '{{.Names}}' 2>/dev/null | grep -q "^${c}$"; then
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

SECRET=$(detect_secret)

# ── 打印 header ──────────────────────────────────────────────────────────────
echo "[diag-zlm-streams] ZLM_URL=${ZLM_URL} app=${APP} VIN=${VIN:-(无前缀)}"
SECRET_SOURCE="(空)"
if [ -n "${ZLM_SECRET:-}" ]; then
    SECRET_SOURCE="环境变量 ZLM_SECRET"
elif docker ps --format '{{.Names}}' 2>/dev/null | grep -q "^teleop-zlmediakit$"; then
    SECRET_SOURCE="运行中容器 teleop-zlmediakit"
else
    SECRET_SOURCE="deploy/zlm/config.ini"
fi
echo "[diag-zlm-streams] ZLM_SECRET 来源: $SECRET_SOURCE"
echo "[diag-zlm-streams] ZLM_SECRET: ${SECRET:+已检测到前8字符: $(echo "$SECRET" | head -c 8)...} 长度=${#SECRET}"
echo "[diag-zlm-streams] GET /index/api/getMediaList"
echo ""

# ── 调用 ZLM API ────────────────────────────────────────────────────────────
if [ -z "$SECRET" ]; then
    echo "[diag-zlm-streams] INFO: ZLM_SECRET 为空，尝试不带 secret 访问（部分 ZLM 版本从 127.0.0.1 可免 secret）"
    out="$(curl -sfS "${ZLM_URL}/index/api/getMediaList?app=${APP}" 2>&1)" || out=""
    if [ -z "$out" ]; then
        echo "[diag-zlm-streams] ERROR: curl 失败（ZLM 不可达或 secret 必需但为空）" >&2
        echo "[diag-zlm-streams]   1. 确认 ZLM 容器运行中：docker ps | grep zlm"
        echo "[diag-zlm-streams]   2. 若 secret 必需：ZLM_SECRET=<实际secret> bash scripts/diag-zlm-streams.sh"
        echo "[diag-zlm-streams]   3. 查看 ZLM secret：docker exec teleop-zlmediakit grep ^secret= /opt/media/conf/config.ini"
        exit 1
    fi
else
    out="$(curl -sfS "${ZLM_URL}/index/api/getMediaList?secret=${SECRET}&app=${APP}" 2>&1)" || out=""
    if [ -z "$out" ]; then
        echo "[diag-zlm-streams] ERROR: curl 失败（ZLM 不可达或 secret 错误）" >&2
        echo "[diag-zlm-streams]   当前 secret: '${SECRET}'（可能不正确）"
        echo "[diag-zlm-streams]   验证 secret：docker exec teleop-zlmediakit grep ^secret= /opt/media/conf/config.ini"
        exit 1
    fi
fi

# ── 输出结果 ────────────────────────────────────────────────────────────────
echo "$out" | head -c 12000
echo ""

# ── 流名检测 ────────────────────────────────────────────────────────────────
PATTERN="${VIN_PREFIX}cam_front|${VIN_PREFIX}cam_rear|${VIN_PREFIX}cam_left|${VIN_PREFIX}cam_right"
if echo "$out" | python3 -m json.tool >/dev/null 2>&1; then
    # JSON 有效，检测流
    if echo "$out" | grep -qE "$PATTERN"; then
        echo "[diag-zlm-streams] OK: 响应中含 ${VIN_PREFIX}cam_* 流名（推流/注册侧通常正常）"
    elif [ -z "$VIN_PREFIX" ] && echo "$out" | grep -qE 'cam_front|cam_rear|cam_left|cam_right'; then
        echo "[diag-zlm-streams] OK: 响应中含 cam_* 流名（无 VIN 前缀模式）"
    else
        # 检查是否因为 VIN 前缀不匹配（可能推流用了不同 VIN）
        KNOWN_STREAMS=$(echo "$out" | python3 -c "
import sys,json
d=json.load(sys.stdin)
for m in d.get('data',[]):
    s=m.get('stream','')
    if 'cam_' in s:
        print('  - '+s)
" 2>/dev/null)
        echo "[diag-zlm-streams] WARN: 响应中未见 ${VIN_PREFIX}cam_* 流名，客户端 WHEP 将 stream not found" >&2
        if [ -n "$KNOWN_STREAMS" ]; then
            echo "[diag-zlm-streams] 实际存在的相机流（VIN 前缀可能不一致）："
            echo "$KNOWN_STREAMS" | sed 's/^/  /'
            echo "[diag-zlm-streams] 提示: 用 VIN=<实际vin> bash scripts/diag-zlm-streams.sh 重新检测"
        else
            echo "[diag-zlm-streams] 提示: 确认 carla-bridge/carla-sim 已收到 MQTT start_stream 并正在推流"
        fi
    fi
else
    # 非 JSON，可能是 secret 错误
    if echo "$out" | grep -qi "incorrect secret\|unauthorized\|forbidden"; then
        echo "[diag-zlm-streams] ERROR: ZLM 返回 'Incorrect secret'！" >&2
        echo "[diag-zlm-streams]   当前 secret: '${SECRET}' 不正确"
        echo "[diag-zlm-streams]   修复：'docker compose restart zlmediakit' 使挂载的 config.ini 生效"
        echo "[diag-zlm-streams]   验证：docker exec teleop-zlmediakit grep ^secret= /opt/media/conf/config.ini"
        exit 1
    else
        echo "[diag-zlm-streams] ERROR: ZLM API 返回非 JSON：${out:0:200}" >&2
        exit 1
    fi
fi

echo ""
echo "[diag-zlm-streams] 可选：UDP 抓包（需 root，容器名按实际替换）"
echo "  sudo tcpdump -i docker0 -n -c 200 'udp and net 172.18.0.0/16'"
