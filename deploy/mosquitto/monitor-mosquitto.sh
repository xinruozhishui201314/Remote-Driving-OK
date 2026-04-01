#!/bin/bash
# ═══════════════════════════════════════════
# MQTT Broker (Mosquitto) 监控脚本
# 用于检测异常情况并自动处理
# ═══════════════════════════════════════════

set -e

MOSQUITTO_LOG="/mosquitto/log/mosquitto.log"
MOSQUITTO_PID_FILE="/var/run/mosquitto.pid"
CHECK_INTERVAL=30  # 检查间隔（秒）
MAX_RESTART_COUNT=5  # 最大重启次数
RESTART_WINDOW=300  # 重启窗口（秒）

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# 日志函数
log_info() {
    echo -e "${GREEN}[INFO]${NC} $(date '+%Y-%m-%d %H:%M:%S') - $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $(date '+%Y-%m-%d %H:%M:%S') - $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $(date '+%Y-%m-%d %H:%M:%S') - $1"
}

# ────────────────────────
# §1 检查 Broker 是否运行
# ────────────────────────

check_broker_running() {
    if [ -f "$MOSQUITTO_PID_FILE" ]; then
        local pid=$(cat "$MOSQUITTO_PID_FILE")
        if ps -p "$pid" > /dev/null 2>&1; then
            return 0
        fi
    fi
    
    # 尝试通过进程名查找
    if pgrep -f "mosquitto" > /dev/null; then
        return 0
    fi
    
    return 1
}

# ────────────────────────
# §2 检查端口是否监听
# ────────────────────────

check_port_listening() {
    local port=$1
    if netstat -tuln 2>/dev/null | grep -q ":$port " || \
       ss -tuln 2>/dev/null | grep -q ":$port "; then
        return 0
    fi
    return 1
}

# ────────────────────────
# §3 检查连接数
# ────────────────────────

check_connection_count() {
    # 通过 $SYS 主题获取连接数（需要订阅权限）
    # 这里简化处理，通过日志统计
    local conn_count=$(grep -c "New connection" "$MOSQUITTO_LOG" 2>/dev/null || echo "0")
    echo "$conn_count"
}

# ────────────────────────
# §4 检查错误日志
# ────────────────────────

check_error_logs() {
    local error_count=$(grep -c "Error\|error\|ERROR" "$MOSQUITTO_LOG" 2>/dev/null | tail -100 || echo "0")
    echo "$error_count"
}

# ────────────────────────
# §5 检查磁盘空间
# ────────────────────────

check_disk_space() {
    local usage=$(df -h /mosquitto/data 2>/dev/null | tail -1 | awk '{print $5}' | sed 's/%//')
    echo "$usage"
}

# ────────────────────────
# §6 重启 Broker
# ────────────────────────

restart_broker() {
    log_warn "尝试重启 MQTT Broker..."
    
    # 记录重启时间
    local restart_file="/tmp/mosquitto_restart_times"
    local now=$(date +%s)
    
    # 读取重启历史
    if [ -f "$restart_file" ]; then
        local restart_times=$(cat "$restart_file")
        local recent_restarts=0
        local window_start=$((now - RESTART_WINDOW))
        
        # 统计窗口内的重启次数
        while IFS= read -r restart_time; do
            if [ "$restart_time" -gt "$window_start" ]; then
                recent_restarts=$((recent_restarts + 1))
            fi
        done < "$restart_file"
        
        # 检查是否超过最大重启次数
        if [ "$recent_restarts" -ge "$MAX_RESTART_COUNT" ]; then
            log_error "重启次数过多（${recent_restarts}/${MAX_RESTART_COUNT}），停止自动重启"
            return 1
        fi
    fi
    
    # 记录重启时间
    echo "$now" >> "$restart_file"
    
    # 清理旧的重启记录（保留最近1小时）
    local one_hour_ago=$((now - 3600))
    if [ -f "$restart_file" ]; then
        awk -v cutoff="$one_hour_ago" '$1 > cutoff' "$restart_file" > "${restart_file}.tmp"
        mv "${restart_file}.tmp" "$restart_file"
    fi
    
    # 执行重启
    if command -v systemctl &> /dev/null; then
        systemctl restart mosquitto
    elif command -v supervisorctl &> /dev/null; then
        supervisorctl restart mosquitto
    else
        # 直接重启进程
        pkill -f mosquitto || true
        sleep 2
        mosquitto -c /mosquitto/config/mosquitto.conf -d
    fi
    
    log_info "MQTT Broker 已重启"
    return 0
}

# ────────────────────────
# §7 主监控循环
# ────────────────────────

main() {
    log_info "MQTT Broker 监控脚本启动"
    log_info "检查间隔: ${CHECK_INTERVAL}秒"
    
    while true; do
        # 检查 Broker 是否运行
        if ! check_broker_running; then
            log_error "MQTT Broker 未运行，尝试重启..."
            restart_broker || exit 1
            sleep 5
            continue
        fi
        
        # 检查端口是否监听
        if ! check_port_listening 1883; then
            log_warn "端口 1883 未监听，Broker 可能异常"
        fi
        
        # 检查磁盘空间
        local disk_usage=$(check_disk_space)
        if [ "$disk_usage" -gt 90 ]; then
            log_error "磁盘空间不足: ${disk_usage}%"
        elif [ "$disk_usage" -gt 80 ]; then
            log_warn "磁盘空间警告: ${disk_usage}%"
        fi
        
        # 检查错误日志（最近100行）
        local error_count=$(check_error_logs)
        if [ "$error_count" -gt 10 ]; then
            log_warn "检测到较多错误日志: ${error_count} 条"
        fi
        
        # 等待下次检查
        sleep "$CHECK_INTERVAL"
    done
}

# 信号处理
trap 'log_info "监控脚本停止"; exit 0' SIGTERM SIGINT

# 运行主循环
main
