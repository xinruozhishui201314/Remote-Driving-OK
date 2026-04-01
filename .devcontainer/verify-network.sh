#!/bin/bash
# 网络环境验证脚本（在容器内运行，后台执行）

# 后台运行时不阻塞终端
# 输出可以重定向到日志文件

echo "=========================================="
echo "Verifying Network Configuration"
echo "=========================================="

# 检查是否在容器内
if [ -f /.dockerenv ]; then
    echo "✓ Running inside container"
else
    echo "⚠ Not running inside container (this script is for container use)"
fi

# 检查网络模式
echo ""
echo "Checking network mode..."
if [ -f /.dockerenv ]; then
    # 检查是否有独立的网络命名空间（host 模式下应该没有）
    if ip link show | grep -q "eth0"; then
        echo "⚠ Found eth0 interface - may not be using host network"
        echo "  Expected: Container should share host network (--network=host)"
    else
        echo "✓ No eth0 interface found (consistent with host network mode)"
    fi
    
    # 检查路由表
    echo ""
    echo "Checking routing table..."
    if [ -f /proc/net/route ]; then
        ROUTE_COUNT=$(wc -l < /proc/net/route)
        if [ "$ROUTE_COUNT" -gt 1 ]; then
            echo "✓ Routing table found ($((ROUTE_COUNT-1)) routes)"
            echo "  First few routes:"
            head -5 /proc/net/route | tail -4 | awk '{print "    Interface: " $1 ", Dest: " $2 ", Gateway: " $3}'
        else
            echo "⚠ No routes found in routing table"
        fi
    fi
fi

# 检查 DNS 配置
echo ""
echo "Checking DNS configuration..."
if [ -f /etc/resolv.conf ]; then
    DNS_COUNT=$(grep -c "^nameserver" /etc/resolv.conf 2>/dev/null || echo "0")
    if [ "$DNS_COUNT" -gt 0 ]; then
        echo "✓ DNS servers configured ($DNS_COUNT servers)"
        grep "^nameserver" /etc/resolv.conf | head -3 | sed 's/^/  /'
    else
        echo "⚠ No DNS servers found in /etc/resolv.conf"
    fi
else
    echo "⚠ /etc/resolv.conf not found"
fi

# 检查网络接口
echo ""
echo "Checking network interfaces..."
if command -v ip > /dev/null 2>&1; then
    INTERFACE_COUNT=$(ip link show | grep -c "^[0-9]")
    echo "✓ Found $INTERFACE_COUNT network interface(s)"
    ip link show | grep "^[0-9]" | head -5 | sed 's/^/  /'
elif [ -d /sys/class/net ]; then
    INTERFACE_COUNT=$(ls /sys/class/net | grep -v lo | wc -l)
    echo "✓ Found $INTERFACE_COUNT network interface(s)"
    ls /sys/class/net | grep -v lo | head -5 | sed 's/^/  /' | awk '{print "  " $0}'
else
    echo "⚠ Cannot enumerate network interfaces"
fi

# 测试本地网络连接
echo ""
echo "Testing local network connectivity..."
if ping -c 1 127.0.0.1 > /dev/null 2>&1; then
    echo "✓ Localhost (127.0.0.1) reachable"
else
    echo "✗ Localhost (127.0.0.1) not reachable"
fi

# 测试网关连接（如果可检测）
echo ""
echo "Testing gateway connectivity..."
if [ -f /proc/net/route ]; then
    GATEWAY=$(awk '/^[^I]/ && $2 == "00000000" {printf "%d.%d.%d.%d", "0x" substr($3,7,2), "0x" substr($3,5,2), "0x" substr($3,3,2), "0x" substr($3,1,2); exit}' /proc/net/route 2>/dev/null)
    if [ -n "$GATEWAY" ] && [ "$GATEWAY" != "0.0.0.0" ]; then
        if ping -c 1 -W 2 "$GATEWAY" > /dev/null 2>&1; then
            echo "✓ Gateway ($GATEWAY) reachable"
        else
            echo "⚠ Gateway ($GATEWAY) not reachable"
        fi
    else
        echo "⚠ Cannot determine gateway from routing table"
    fi
fi

# 测试外部网络连接
echo ""
echo "Testing external network connectivity..."
if ping -c 1 -W 3 8.8.8.8 > /dev/null 2>&1; then
    echo "✓ External network (8.8.8.8) reachable"
    
    # 测试 DNS
    if command -v nslookup > /dev/null 2>&1 || command -v host > /dev/null 2>&1; then
        if nslookup google.com > /dev/null 2>&1 || host google.com > /dev/null 2>&1; then
            echo "✓ DNS resolution working"
        else
            echo "⚠ DNS resolution may not be working"
        fi
    fi
else
    echo "✗ External network (8.8.8.8) not reachable"
    echo ""
    echo "  Possible causes:"
    echo "    1. Host machine has no internet connection"
    echo "    2. Container not using host network (--network=host not applied)"
    echo "    3. Firewall blocking ICMP"
    echo "    4. Network configuration issue"
fi

# 检查 host 网络模式特征
echo ""
echo "Checking host network mode indicators..."
if [ -f /.dockerenv ]; then
    # 在 host 网络模式下，容器应该能看到主机的所有网络接口
    HOST_INTERFACES=$(ls /sys/class/net 2>/dev/null | grep -v lo | wc -l)
    if [ "$HOST_INTERFACES" -gt 0 ]; then
        echo "✓ Found $HOST_INTERFACES host network interface(s)"
        echo "  This suggests host network mode is active"
    else
        echo "⚠ No host network interfaces found"
        echo "  Container may not be using --network=host"
    fi
    
    # 检查是否能看到主机的网络命名空间
    if [ -d /proc/sys/net ]; then
        echo "✓ Network sysfs accessible"
    fi
fi

echo ""
echo "=========================================="
echo "Network verification completed!"
echo "=========================================="
echo ""
echo "If network is not working:"
echo "  1. Check devcontainer.json has '--network=host' in runArgs"
echo "  2. Rebuild container: F1 → 'Dev Containers: Rebuild Container'"
echo "  3. Check host machine network connectivity"
echo "  4. Verify Docker daemon supports host network mode"
echo ""
