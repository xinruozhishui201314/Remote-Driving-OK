#!/bin/bash
# 清除客户端登录状态（用于测试）

set -e

echo "清除客户端登录状态..."

# 在容器内清除 QSettings 配置文件
docker compose exec client-dev bash -c "
    # QSettings 配置文件通常位于 ~/.config/<org>/<app>.conf
    # 对于 Qt 应用，可能是 ~/.config/RemoteDriving/RemoteDrivingClient.conf
    # 或者系统配置目录
    
    # 查找并删除所有可能的配置文件
    find ~/.config -name '*RemoteDriving*' -type f 2>/dev/null | xargs rm -f || true
    find ~/.config -name '*remote-driving*' -type f 2>/dev/null | xargs rm -f || true
    
    # 也清除系统级配置（如果存在）
    sudo find /etc/xdg -name '*RemoteDriving*' -type f 2>/dev/null | xargs sudo rm -f || true
    
    echo '登录状态已清除'
"

echo "完成！下次启动客户端时将显示登录界面。"
