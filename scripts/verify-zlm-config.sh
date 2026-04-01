#!/bin/bash
# ==============================================================================
# ZLM 配置一致性验证脚本（启动时 + 诊断时）
# 对比 deploy/zlm/config.ini（预期值）与运行中 ZLM 容器的实际配置，
# 检测配置漂移（Configuration Drift），避免因未挂载 config.ini 导致 secret 不一致、
# rtc.timeoutSec 过短等隐蔽问题。
#
# 用法：
#   bash scripts/verify-zlm-config.sh [zlm_container_name]
#   bash scripts/verify-zlm-config.sh teleop-zlmediakit
#   # 期望输出：所有检查项均为 PASS
# ==============================================================================

set -euo pipefail

ZLM_CONTAINER="${1:-teleop-zlmediakit}"
DEPLOY_CONFIG="$(dirname "$0")/../deploy/zlm/config.ini"
EXIT_CODE=0

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'
PASS() { echo -e "  ${GREEN}✓ PASS${NC}  $*"; }
FAIL() { echo -e "  ${RED}✗ FAIL${NC}  $*" >&2; EXIT_CODE=1; }
WARN() { echo -e "  ${YELLOW}⚠ WARN${NC}  $*"; }
INFO() { echo -e "  ${NC}    $*"; }

echo ""
echo "════════════════════════════════════════════════════════════════════════════"
echo " ZLM 配置一致性验证 — $(date '+%Y-%m-%d %H:%M:%S')"
echo "════════════════════════════════════════════════════════════════════════════"
echo " 容器: $ZLM_CONTAINER"
echo " 预期配置: $DEPLOY_CONFIG"
echo ""

# ── Step 0. 检查容器是否存在 ───────────────────────────────────────────────
echo "【0. 容器状态】"
if ! docker ps --format '{{.Names}}' 2>/dev/null | grep -q "^${ZLM_CONTAINER}$"; then
    FAIL "容器 '$ZLM_CONTAINER' 未运行（docker ps 中未找到）"
    echo "  提示：先启动服务 'docker compose up -d zlmediakit'"
    echo ""
    echo "════════════════════════════════════════════════════════════════════════════"
    echo " 验证结果: FAIL（容器未运行）"
    echo "════════════════════════════════════════════════════════════════════════════"
    exit 1
fi
PASS "容器 '$ZLM_CONTAINER' 正在运行"
CONTAINER_ID=$(docker ps --filter "name=${ZLM_CONTAINER}" -q | head -1)
INFO "容器 ID: $CONTAINER_ID"
echo ""

# ── Step 1. 检查 config.ini 是否被挂载 ─────────────────────────────────────
echo "【1. config.ini 挂载检查】"
if ! docker exec "$ZLM_CONTAINER" test -f /opt/media/conf/config.ini 2>/dev/null; then
    FAIL "/opt/media/conf/config.ini 不存在！ZLM 使用内置默认配置（secret=默认值，rtc.timeoutSec=10）"
    FAIL "  修复: 在 docker-compose.yml 的 zlmediakit.volumes 中添加:"
    FAIL "    - ./deploy/zlm/config.ini:/opt/media/conf/config.ini:ro"
    FAIL "  然后重启: docker compose restart zlmediakit"
else
    PASS "/opt/media/conf/config.ini 已挂载"
    # 检查是否为 deploy 目录中的文件
    MOUNT_INFO=$(docker exec "$ZLM_CONTAINER" ls -la /opt/media/conf/config.ini 2>/dev/null || echo "未知")
    INFO "挂载信息: $MOUNT_INFO"
fi
echo ""

# ── ZLM INI 配置读取（使用 Python configparser，正确处理 [rtc] 等 section）────────
# 返回指定 section.key 的值，找不到返回空字符串
get_zlm_conf() {
    local section="$1"
    local key="$2"
    docker exec "$ZLM_CONTAINER" cat /opt/media/conf/config.ini 2>/dev/null | python3 -c "
import sys, configparser
cfg = configparser.ConfigParser()
cfg.read_string(sys.stdin.read())
try:
    print(cfg.get('${section}', '${key}'))
except (configparser.NoSectionError, configparser.NoOptionError):
    print('')
" 2>/dev/null || echo ""
}

get_deploy_conf() {
    local section="$1"
    local key="$2"
    python3 -c "
import sys, configparser
cfg = configparser.ConfigParser()
cfg.read('${DEPLOY_CONFIG}')
try:
    print(cfg.get('${section}', '${key}'))
except (configparser.NoSectionError, configparser.NoOptionError):
    print('')
" 2>/dev/null || echo ""
}

echo "【2. 关键配置项对比（deploy vs 容器实际值）】"
echo ""
echo "  ┌──────────────────────┬───────────────────────────────────────────────┬───────────────────────────────────────────────┬───────┐"
echo "  │ 配置项               │ 预期值（deploy/zlm/config.ini）              │ 实际值（运行中容器）                          │ 状态  │"
echo "  ├──────────────────────┼───────────────────────────────────────────────┼───────────────────────────────────────────────┼───────┤"

check_conf() {
    local section="$1"
    local key="$2"
    local desc="$3"
    local expected
    local actual
    expected=$(get_deploy_conf "$section" "$key")
    actual=$(get_zlm_conf "$section" "$key")
    if [ -z "$actual" ] && [ -z "$expected" ]; then
        printf "  │ %-20s │ %-47s │ %-47s │  PASS │\n" "$desc" "$expected" "$actual"
        return 0
    elif [ -z "$actual" ]; then
        printf "  │ %-20s │ %-47s │ %-47s │  FAIL │\n" "$desc" "$expected" "(未定义)"
        EXIT_CODE=1; return 1
    elif [ -z "$expected" ]; then
        printf "  │ %-20s │ %-47s │ %-47s │  PASS │\n" "$desc" "(未定义)" "$actual"
        return 0
    elif [ "$expected" = "$actual" ]; then
        printf "  │ %-20s │ %-47s │ %-47s │  PASS │\n" "$desc" "$expected" "$actual"
        return 0
    else
        printf "  │ %-20s │ %-47s │ %-47s │  FAIL │\n" "$desc" "$expected" "$actual"
        EXIT_CODE=1; return 1
    fi
}

check_conf "api"         "secret"       "API Secret"
check_conf "api"         "apiDebug"     "apiDebug"
check_conf "rtc"         "timeoutSec"   "rtc.timeoutSec"
check_conf "general"     "mediaServerId" "ServerID"

echo "  └──────────────────────┴───────────────────────────────────────────────┴───────────────────────────────────────────────┴───────┘"
echo ""

# ── Step 3. rtc.timeoutSec 安全阈值检查 ────────────────────────────────────
echo "【3. rtc.timeoutSec 安全阈值检查】"
RTC_TIMEOUT=$(get_zlm_conf "rtc" "timeoutSec")
if [ -z "$RTC_TIMEOUT" ] || ! [[ "$RTC_TIMEOUT" =~ ^[0-9]+$ ]]; then
    WARN "rtc.timeoutSec 未定义或非数字，极易导致 RTCP 超时断连"
    FAIL "  修复: 在 deploy/zlm/config.ini [rtc] 中添加: timeoutSec=120"
    FAIL "  然后重启: docker compose restart zlmediakit"
elif [ "$RTC_TIMEOUT" -lt 30 ]; then
    WARN "rtc.timeoutSec=${RTC_TIMEOUT}s < 30s，在 RTCP 超时后 ZLM 将关闭 WebRTC 会话"
    WARN "  症状：约 ${RTC_TIMEOUT}s 后 PeerConnection Closed，client 日志见 'ZLM 已停发帧'"
    FAIL "  修复: docker compose restart zlmediakit 使挂载的配置生效"
elif [ "$RTC_TIMEOUT" -lt 60 ]; then
    WARN "rtc.timeoutSec=${RTC_TIMEOUT}s < 60s，建议 >= 120s 以容纳突发延迟"
    INFO "  建议: 在 deploy/zlm/config.ini [rtc] 中修改 timeoutSec=120"
else
    PASS "rtc.timeoutSec=${RTC_TIMEOUT}s >= 60s，安全"
fi
echo ""

# ── Step 4. API Secret 正确性检查 ─────────────────────────────────────────
echo "【4. API Secret 正确性检查】"
EXPECTED_SECRET="035c73f7-bb6b-4889-a715-d9eb2d1925cc"
ACTUAL_SECRET=$(get_zlm_conf "api" "secret")
if [ -z "$ACTUAL_SECRET" ]; then
    FAIL "secret 未定义，diag-webrtc-streams.sh 将失败（ZLM 返回 'Incorrect secret'）"
elif [ "$ACTUAL_SECRET" = "$EXPECTED_SECRET" ]; then
    PASS "secret 与 deploy/zlm/config.ini 一致（diag 脚本可正常工作）"
else
    WARN "secret 与 deploy/zlm/config.ini 不一致"
    INFO "  deploy 预期: ${EXPECTED_SECRET}"
    INFO "  容器实际:   ${ACTUAL_SECRET}"
    FAIL "  影响: docker-compose.yml 中的 ZLM_API_SECRET 环境变量与容器实际 secret 不匹配"
    FAIL "  修复: 重启 ZLM 容器使挂载的 config.ini 生效"
fi
echo ""

# ── Step 5. ZLM API 连通性快速检查 ────────────────────────────────────────
echo "【5. ZLM API 连通性 + Secret 验证】"
API_SECRET="${ACTUAL_SECRET:-$EXPECTED_SECRET}"
# 优先通过 Docker 网络内部域名访问（docker compose 网络内），fallback 到 localhost
API_HOST="$ZLM_CONTAINER"
if ! docker exec "$ZLM_CONTAINER" sh -c "curl -sf http://localhost:80/index/api/getMediaList?secret=${API_SECRET}" >/dev/null 2>&1; then
    # fallback：通过宿主机映射端口访问
    if curl -sf "http://localhost:80/index/api/getMediaList?secret=${API_SECRET}" >/dev/null 2>&1; then
        API_HOST="localhost"
    else
        API_HOST="不可达"
    fi
fi
API_RESP=$(curl -sf "http://${API_HOST}:80/index/api/getMediaList?secret=${API_SECRET}" 2>&1) || API_RESP=""
if [ -z "$API_RESP" ]; then
    WARN "ZLM API 请求失败（host=${API_HOST}）"
    INFO "  尝试 curl 详情: $(curl -v "http://${API_HOST}:80/index/api/getMediaList?secret=${API_SECRET}" 2>&1 | head -c 300)"
elif echo "$API_RESP" | python3 -m json.tool >/dev/null 2>&1; then
    PASS "ZLM API 连通正常，secret 验证通过（host=${API_HOST}）"
    STREAM_COUNT=$(echo "$API_RESP" | python3 -c "import sys,json; d=json.load(sys.stdin); print(len(d.get('data',[])))" 2>/dev/null || echo "?")
    INFO "当前注册流数量: $STREAM_COUNT"
    # 显示活跃流详情
    echo "$API_RESP" | python3 -c "
import sys,json
d=json.load(sys.stdin)
for m in d.get('data',[]):
    s=m.get('stream','')
    schema=m.get('schema','')
    alive=m.get('aliveSecond',0)
    print(f'    {s} [{schema}] alive={alive}s')
" 2>/dev/null
else
    if echo "$API_RESP" | grep -qi "incorrect secret"; then
        FAIL "ZLM 返回 'Incorrect secret'！容器 secret 与 API 请求使用的 secret 不一致"
        FAIL "  请重启 ZLM 容器使挂载的 config.ini 生效"
    else
        FAIL "ZLM API 返回非 JSON：${API_RESP:0:200}"
    fi
fi
echo ""

# ── Step 6. WebRTC RTCP 超时历史检查 ──────────────────────────────────────
echo "【6. WebRTC RTCP 超时历史（从容器日志）】"
RTCP_ERR=$(docker logs "$ZLM_CONTAINER" 2>/dev/null | grep -c "接受rtp/rtcp/datachannel超时" || true)
RTCP_ERR="${RTCP_ERR:-0}"
if [ -n "$RTCP_ERR" ] && [ "$RTCP_ERR" -gt 0 ] 2>/dev/null; then
    FAIL "ZLM 容器历史日志中存在 RTCP 超时 ${RTCP_ERR} 次！"
    INFO "  最近 5 次超时日志："
    docker logs "$ZLM_CONTAINER" 2>&1 | grep "接受rtp/rtcp/datachannel超时" | tail -5 | sed 's/^/    /'
    WARN "  可能原因：libdatachannel recvonly 模式未发送 RTCP RR / rtc.timeoutSec 过短"
else
    PASS "ZLM 日志中无 RTCP 超时记录"
fi
echo ""

# ── 输出最终结果 ────────────────────────────────────────────────────────────
echo "════════════════════════════════════════════════════════════════════════════"
if [ "$EXIT_CODE" -eq 0 ]; then
    echo -e "  ${GREEN}验证结果: ALL PASS ✓${NC}"
    echo "  ZLM 配置与 deploy/zlm/config.ini 一致，配置漂移检测通过。"
else
    echo -e "  ${RED}验证结果: FAIL ✗${NC}"
    echo "  检测到配置不一致！请按上述 FAIL 项修复后重新运行本脚本验证。"
fi
echo "════════════════════════════════════════════════════════════════════════════"
echo ""

# ── 快速修复命令 ──────────────────────────────────────────────────────────
if [ "$EXIT_CODE" -ne 0 ]; then
    echo "快速修复命令："
    echo ""
    echo "  # 方案 1：重启 ZLM（使挂载的 config.ini 生效，推荐）"
    echo "  docker compose restart zlmediakit"
    echo "  sleep 5"
    echo "  bash scripts/verify-zlm-config.sh"
    echo ""
    echo "  # 方案 2：确认 docker-compose.yml 中 zlmediakit 的 volumes 是否包含 config.ini"
    echo "  grep -A 5 'zlmediakit:' docker-compose.yml | grep config"
    echo ""
fi

exit $EXIT_CODE
