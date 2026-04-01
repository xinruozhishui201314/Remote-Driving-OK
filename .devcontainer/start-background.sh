#!/bin/bash
# 后台启动脚本（容器启动后自动运行）
# 所有初始化任务在后台执行，不阻塞终端，立即返回

LOG_DIR="/tmp/devcontainer-logs"
mkdir -p "$LOG_DIR"

# 后台运行 setup.sh（使用 nohup 确保进程独立）
nohup bash .devcontainer/setup.sh > "$LOG_DIR/setup.log" 2>&1 &
SETUP_PID=$!

# 后台运行网络诊断（等待 setup 完成后再运行，避免工具未安装）
(
    wait $SETUP_PID 2>/dev/null || sleep 2
    bash .devcontainer/verify-network.sh > "$LOG_DIR/network-verify.log" 2>&1
) &

# 立即返回，不等待后台任务
# 只显示简短提示，不阻塞终端
echo "✓ Container initialization running in background"
echo "  Logs: $LOG_DIR/setup.log, $LOG_DIR/network-verify.log"
exit 0
