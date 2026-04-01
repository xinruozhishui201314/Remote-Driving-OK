#!/bin/bash
# 自动化测试与诊断脚本：启动系统、收集日志、分析问题、提供解决方案
# 用法: bash scripts/test-and-diagnose.sh [选项]
#
# 选项：
#   --no-start     不启动系统，仅分析现有日志
#   --no-fix       不提供修复建议，仅诊断
#   --verbose      详细输出
#   --focus <模块> 聚焦特定模块（client/vehicle/mqtt/stream/all）

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

CLIENT_CONTAINER="teleop-client-dev"
VEHICLE_CONTAINER="remote-driving-vehicle-1"
ZLM_CONTAINER="teleop-zlmediakit"
MQTT_CONTAINER="teleop-mosquitto"
BACKEND_CONTAINER="teleop-backend"

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
CYAN='\033[0;36m'
BLUE='\033[0;34m'
NC='\033[0m'

# 解析参数
NO_START=false
NO_FIX=false
VERBOSE=false
FOCUS_MODULE="all"

while [[ $# -gt 0 ]]; do
    case $1 in
        --no-start)
            NO_START=true
            shift
            ;;
        --no-fix)
            NO_FIX=true
            shift
            ;;
        --verbose)
            VERBOSE=true
            shift
            ;;
        --focus)
            FOCUS_MODULE="$2"
            shift 2
            ;;
        *)
            echo -e "${RED}未知参数: $1${NC}"
            exit 1
            ;;
    esac
done

# 诊断结果存储
DIAGNOSIS_FILE="/tmp/remote-driving-diagnosis-$$.json"
ISSUES_FOUND=0
FIXES_PROVIDED=0

# 初始化诊断文件
cat > "$DIAGNOSIS_FILE" <<EOF
{
  "timestamp": "$(date -Iseconds)",
  "issues": [],
  "fixes": [],
  "logs": {
    "client": "",
    "vehicle": "",
    "mqtt": "",
    "zlm": ""
  }
}
EOF

# 日志函数
log_info() {
    echo -e "${CYAN}[INFO]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
    ISSUES_FOUND=$((ISSUES_FOUND + 1))
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_fix() {
    echo -e "${BLUE}[FIX]${NC} $1"
    FIXES_PROVIDED=$((FIXES_PROVIDED + 1))
}

# 收集日志
collect_logs() {
    log_info "收集日志..."
    
    CLIENT_LOGS=""
    VEHICLE_LOGS=""
    MQTT_LOGS=""
    ZLM_LOGS=""
    
    if docker ps --format '{{.Names}}' | grep -q "^${CLIENT_CONTAINER}$"; then
        CLIENT_LOGS=$(docker logs ${CLIENT_CONTAINER} --tail 1000 2>&1 || echo "")
    fi
    
    if docker ps --format '{{.Names}}' | grep -q "^${VEHICLE_CONTAINER}$"; then
        VEHICLE_LOGS=$(docker logs ${VEHICLE_CONTAINER} --tail 1000 2>&1 || echo "")
    fi
    
    if docker ps --format '{{.Names}}' | grep -q "^${MQTT_CONTAINER}$"; then
        MQTT_LOGS=$(docker logs ${MQTT_CONTAINER} --tail 500 2>&1 || echo "")
    fi
    
    if docker ps --format '{{.Names}}' | grep -q "^${ZLM_CONTAINER}$"; then
        ZLM_LOGS=$(docker logs ${ZLM_CONTAINER} --tail 500 2>&1 || echo "")
    fi
    
    log_success "日志收集完成"
}

# 诊断编译错误
diagnose_compilation_errors() {
    log_info "诊断编译错误..."
    
    local has_error=false
    
    # 检查客户端编译错误
    if echo "$CLIENT_LOGS" | grep -qE "error:|Error:|编译失败|make.*Error"; then
        local errors=$(echo "$CLIENT_LOGS" | grep -E "error:|Error:" | tail -10)
        
        # 检查常见编译错误
        if echo "$errors" | grep -q "QJsonDocument.*not declared\|incomplete type.*QJsonDocument"; then
            log_error "客户端编译错误：缺少 QJsonDocument 头文件"
            if [ "$NO_FIX" = false ]; then
                log_fix "修复方案：在 client/src/vehiclestatus.cpp 中添加 #include <QJsonDocument>"
                log_fix "执行：sed -i '2a #include <QJsonDocument>' client/src/vehiclestatus.cpp"
            fi
            has_error=true
        fi
        
        if echo "$errors" | grep -q "QElapsedTimer.*not declared\|does not name a type.*QElapsedTimer"; then
            log_error "客户端编译错误：缺少 QElapsedTimer 头文件"
            if [ "$NO_FIX" = false ]; then
                log_fix "修复方案：在 client/src/webrtcclient.cpp 中添加 #include <QElapsedTimer>"
                log_fix "执行：sed -i '/#include <QTimer>/a #include <QElapsedTimer>' client/src/webrtcclient.cpp"
            fi
            has_error=true
        fi
        
        if echo "$errors" | grep -q "undefined reference\|未定义的引用"; then
            log_error "客户端链接错误：缺少库链接"
            if [ "$NO_FIX" = false ]; then
                log_fix "修复方案：检查 CMakeLists.txt 中的 target_link_libraries"
            fi
            has_error=true
        fi
        
        if [ "$has_error" = false ]; then
            log_error "客户端编译错误（未分类）："
            echo "$errors" | head -5 | sed 's/^/  /'
        fi
    fi
    
    # 检查车端编译错误
    if echo "$VEHICLE_LOGS" | grep -qE "error:|Error:|编译失败|make.*Error"; then
        local errors=$(echo "$VEHICLE_LOGS" | grep -E "error:|Error:" | tail -10)
        log_error "车端编译错误："
        echo "$errors" | head -5 | sed 's/^/  /'
        has_error=true
    fi
    
    if [ "$has_error" = false ]; then
        log_success "未发现编译错误"
    fi
}

# 诊断运行时错误
diagnose_runtime_errors() {
    log_info "诊断运行时错误..."
    
    local has_error=false
    
    # 检查客户端崩溃
    if echo "$CLIENT_LOGS" | grep -qE "Segmentation fault|segfault|崩溃|crash|SIGSEGV"; then
        log_error "客户端崩溃：段错误"
        if [ "$NO_FIX" = false ]; then
            log_fix "修复方案：检查空指针访问、数组越界、线程安全问题"
            log_fix "执行：docker logs ${CLIENT_CONTAINER} --tail 200 | grep -A 10 -B 10 'Segmentation fault'"
        fi
        has_error=true
    fi
    
    # 检查车端崩溃
    if echo "$VEHICLE_LOGS" | grep -qE "Segmentation fault|segfault|崩溃|crash|SIGSEGV"; then
        log_error "车端崩溃：段错误"
        if [ "$NO_FIX" = false ]; then
            log_fix "修复方案：检查空指针访问、数组越界、线程安全问题"
            log_fix "执行：docker logs ${VEHICLE_CONTAINER} --tail 200 | grep -A 10 -B 10 'Segmentation fault'"
        fi
        has_error=true
    fi
    
    # 检查异常
    if echo "$CLIENT_LOGS" | grep -qE "exception|Exception|throw"; then
        log_error "客户端异常："
        echo "$CLIENT_LOGS" | grep -E "exception|Exception|throw" | tail -5 | sed 's/^/  /'
        has_error=true
    fi
    
    if echo "$VEHICLE_LOGS" | grep -qE "exception|Exception|throw"; then
        log_error "车端异常："
        echo "$VEHICLE_LOGS" | grep -E "exception|Exception|throw" | tail -5 | sed 's/^/  /'
        has_error=true
    fi
    
    if [ "$has_error" = false ]; then
        log_success "未发现运行时错误"
    fi
}

# 诊断连接问题
diagnose_connection_issues() {
    log_info "诊断连接问题..."
    
    local has_error=false
    
    # 检查 MQTT 连接
    if echo "$CLIENT_LOGS" | grep -qE "MQTT.*连接失败|无法连接.*MQTT|Connection refused"; then
        log_error "客户端 MQTT 连接失败"
        if [ "$NO_FIX" = false ]; then
            log_fix "修复方案：检查 MQTT Broker 是否运行"
            log_fix "执行：docker ps | grep mosquitto"
            log_fix "执行：docker logs ${MQTT_CONTAINER} --tail 50"
        fi
        has_error=true
    fi
    
    if echo "$VEHICLE_LOGS" | grep -qE "MQTT.*连接失败|无法连接.*MQTT|Connection refused"; then
        log_error "车端 MQTT 连接失败"
        if [ "$NO_FIX" = false ]; then
            log_fix "修复方案：检查 MQTT Broker 是否运行"
            log_fix "执行：docker ps | grep mosquitto"
        fi
        has_error=true
    fi
    
    # 检查 WebRTC 连接
    if echo "$CLIENT_LOGS" | grep -qE "WebRTC.*连接失败|无法连接.*WebRTC|offer.*失败"; then
        log_error "客户端 WebRTC 连接失败"
        if [ "$NO_FIX" = false ]; then
            log_fix "修复方案：检查 ZLMediaKit 是否运行"
            log_fix "执行：docker ps | grep zlmediakit"
            log_fix "执行：curl http://localhost:80/index/api/getServerConfig"
        fi
        has_error=true
    fi
    
    # 检查视频流
    if echo "$CLIENT_LOGS" | grep -qE "视频流.*失败|stream.*failed|无法接收视频"; then
        log_error "客户端视频流接收失败"
        if [ "$NO_FIX" = false ]; then
            log_fix "修复方案：检查推流是否启动"
            log_fix "执行：docker exec ${VEHICLE_CONTAINER} ps aux | grep ffmpeg"
            log_fix "执行：curl http://localhost:80/index/api/getMediaList?app=teleop"
        fi
        has_error=true
    fi
    
    if [ "$has_error" = false ]; then
        log_success "未发现连接问题"
    fi
}

# 诊断远驾接管功能
diagnose_remote_control() {
    log_info "诊断远驾接管功能..."
    
    local has_error=false
    
    # 检查车端是否检测到指令
    if ! echo "$VEHICLE_LOGS" | grep -qE "REMOTE_CONTROL.*确认是|确认是 remote_control"; then
        log_warn "车端未检测到 remote_control 指令（如果未操作过，这是正常的）"
    else
        log_success "车端已检测到 remote_control 指令"
    fi
    
    # 检查车端是否发送确认
    if echo "$VEHICLE_LOGS" | grep -qE "REMOTE_CONTROL.*已成功发送|已成功发送远驾接管确认"; then
        log_success "车端已发送确认消息"
    else
        if echo "$VEHICLE_LOGS" | grep -qE "remote_control|REMOTE_CONTROL"; then
            log_error "车端检测到指令但未发送确认"
            if [ "$NO_FIX" = false ]; then
                log_fix "修复方案：检查 mqtt_handler.cpp 中的 publishRemoteControlAck() 方法"
                log_fix "执行：grep -n 'publishRemoteControlAck' Vehicle-side/src/mqtt_handler.cpp"
            fi
            has_error=true
        fi
    fi
    
    # 检查客户端是否收到确认
    if echo "$CLIENT_LOGS" | grep -qE "REMOTE_CONTROL.*收到|收到远驾接管确认"; then
        log_success "客户端已收到确认消息"
    else
        if echo "$CLIENT_LOGS" | grep -qE "requestRemoteControl|远驾接管"; then
            log_error "客户端发送了指令但未收到确认"
            if [ "$NO_FIX" = false ]; then
                log_fix "修复方案：检查 MQTT 订阅主题是否正确"
                log_fix "执行：grep -n 'vehicle/status' client/src/mqttcontroller.cpp"
            fi
            has_error=true
        fi
    fi
    
    # 检查状态更新
    if echo "$CLIENT_LOGS" | grep -qE "REMOTE_CONTROL.*状态变化|远驾接管状态变化"; then
        log_success "客户端状态已更新"
    else
        if echo "$CLIENT_LOGS" | grep -qE "remote_control_ack"; then
            log_error "客户端收到确认但状态未更新"
            if [ "$NO_FIX" = false ]; then
                log_fix "修复方案：检查 vehiclestatus.cpp 中的 updateStatus() 方法"
                log_fix "执行：grep -n 'remote_control_enabled' client/src/vehiclestatus.cpp"
            fi
            has_error=true
        fi
    fi
    
    if [ "$has_error" = false ]; then
        log_success "远驾接管功能正常（或未测试）"
    fi
}

# 诊断容器状态
diagnose_containers() {
    log_info "诊断容器状态..."
    
    local has_error=false
    
    local containers=("${CLIENT_CONTAINER}" "${VEHICLE_CONTAINER}" "${MQTT_CONTAINER}" "${ZLM_CONTAINER}" "${BACKEND_CONTAINER}")
    
    for container in "${containers[@]}"; do
        if ! docker ps --format '{{.Names}}' | grep -q "^${container}$"; then
            log_error "容器未运行：${container}"
            if [ "$NO_FIX" = false ]; then
                log_fix "修复方案：启动容器"
                log_fix "执行：bash scripts/start-full-chain.sh manual"
            fi
            has_error=true
        else
            local status=$(docker ps --format '{{.Status}}' --filter "name=^${container}$")
            if echo "$status" | grep -qE "unhealthy|Restarting|Exited"; then
                log_error "容器状态异常：${container} (${status})"
                if [ "$NO_FIX" = false ]; then
                    log_fix "修复方案：检查容器日志"
                    log_fix "执行：docker logs ${container} --tail 100"
                fi
                has_error=true
            else
                log_success "容器正常：${container}"
            fi
        fi
    done
    
    if [ "$has_error" = false ]; then
        log_success "所有容器状态正常"
    fi
}

# 诊断代码更新
diagnose_code_updates() {
    log_info "诊断代码更新..."
    
    local has_error=false
    
    # 检查车端代码是否包含新标记
    if docker ps --format '{{.Names}}' | grep -q "^${VEHICLE_CONTAINER}$"; then
        if docker exec ${VEHICLE_CONTAINER} bash -c "strings /tmp/vehicle-build/VehicleSide 2>/dev/null | grep -q 'REMOTE_CONTROL'" 2>/dev/null; then
            log_success "车端代码已更新（包含 REMOTE_CONTROL 标记）"
        else
            log_warn "车端代码可能未更新（未找到 REMOTE_CONTROL 标记）"
            if [ "$NO_FIX" = false ]; then
                log_fix "修复方案：重启车端容器以重新编译"
                log_fix "执行：docker restart ${VEHICLE_CONTAINER}"
            fi
        fi
    fi
    
    # 检查客户端代码编译时间
    if docker ps --format '{{.Names}}' | grep -q "^${CLIENT_CONTAINER}$"; then
        if docker exec ${CLIENT_CONTAINER} bash -c "test -x /tmp/client-build/RemoteDrivingClient || test -x /workspace/client/build/RemoteDrivingClient" 2>/dev/null; then
            log_success "客户端已编译"
        else
            log_warn "客户端未编译（将在启动时自动编译）"
        fi
    fi
}

# 生成诊断报告
generate_report() {
    log_info "生成诊断报告..."
    
    echo ""
    echo -e "${CYAN}========================================${NC}"
    echo -e "${CYAN}诊断报告${NC}"
    echo -e "${CYAN}========================================${NC}"
    echo ""
    echo -e "发现的问题: ${RED}${ISSUES_FOUND}${NC}"
    echo -e "提供的修复: ${BLUE}${FIXES_PROVIDED}${NC}"
    echo ""
    
    if [ "$ISSUES_FOUND" -eq 0 ]; then
        log_success "✓ 未发现严重问题，系统运行正常"
    else
        log_error "✗ 发现 ${ISSUES_FOUND} 个问题，请查看上述修复建议"
    fi
    
    echo ""
    echo -e "${CYAN}详细日志位置：${NC}"
    echo "  客户端: docker logs ${CLIENT_CONTAINER} --tail 200"
    echo "  车端: docker logs ${VEHICLE_CONTAINER} --tail 200"
    echo "  MQTT: docker logs ${MQTT_CONTAINER} --tail 100"
    echo ""
    
    if [ "$VERBOSE" = true ]; then
        echo -e "${CYAN}关键日志片段：${NC}"
        echo ""
        echo "【客户端关键错误】"
        echo "$CLIENT_LOGS" | grep -E "error|Error|ERROR|exception|Exception|failed|Failed" | tail -10 || echo "无"
        echo ""
        echo "【车端关键错误】"
        echo "$VEHICLE_LOGS" | grep -E "error|Error|ERROR|exception|Exception|failed|Failed" | tail -10 || echo "无"
        echo ""
    fi
}

# 主流程
main() {
    echo -e "${CYAN}========================================${NC}"
    echo -e "${CYAN}自动化测试与诊断${NC}"
    echo -e "${CYAN}========================================${NC}"
    echo ""
    
    # 启动系统
    if [ "$NO_START" = false ]; then
        log_info "启动系统..."
        bash "$SCRIPT_DIR/start-full-chain.sh" manual no-client 2>&1 | tee /tmp/start-full-chain-$$.log || {
            log_error "系统启动失败"
            exit 1
        }
        
        log_info "等待系统稳定（30秒）..."
        sleep 30
    else
        log_info "跳过系统启动（--no-start）"
    fi
    
    # 收集日志
    collect_logs
    
    # 根据聚焦模块执行诊断
    case "$FOCUS_MODULE" in
        client)
            diagnose_compilation_errors
            diagnose_runtime_errors
            ;;
        vehicle)
            diagnose_compilation_errors
            diagnose_runtime_errors
            ;;
        mqtt)
            diagnose_connection_issues
            ;;
        stream)
            diagnose_connection_issues
            ;;
        remote-control|remote_control)
            diagnose_remote_control
            ;;
        all|*)
            diagnose_containers
            diagnose_code_updates
            diagnose_compilation_errors
            diagnose_runtime_errors
            diagnose_connection_issues
            diagnose_remote_control
            ;;
    esac
    
    # 生成报告
    generate_report
    
    # 清理临时文件
    rm -f "$DIAGNOSIS_FILE"
    
    # 返回状态码
    if [ "$ISSUES_FOUND" -eq 0 ]; then
        exit 0
    else
        exit 1
    fi
}

# 执行主流程
main "$@"
