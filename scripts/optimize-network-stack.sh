#!/bin/bash
# ═══════════════════════════════════════════════════════════════════════════════════════
# REMOTE DRIVING NETWORK STACK OPTIMIZATION (GOOGLE SRE / LOW-LATENCY STANDARDS)
# Target: Ubuntu/Linux 5.x+
# ═══════════════════════════════════════════════════════════════════════════════════════

set -e

# Check for root
if [[ $EUID -ne 0 ]]; then
   echo "This script must be run as root" 
   exit 1
fi

echo "[Network] Starting OS-level optimization for Remote Driving..."

# 1. Congestion Control: Switch to BBR (Bottleneck Bandwidth and RTT)
# BBR is superior for lossy/congested mobile links (4G/5G) compared to CUBIC.
echo "[Network] Setting congestion control to BBR..."
modprobe tcp_bbr || echo "BBR module not found, skipping..."
sysctl -w net.core.default_qdisc=fq
sysctl -w net.ipv4.tcp_congestion_control=bbr

# 2. Reducing TCP Minimum Retransmission Timeout (MinRTO)
# Default is 200ms, which is too slow for 150ms E2E latency budget.
# We reduce it to 50ms for the default route.
echo "[Network] Reducing TCP MinRTO to 50ms..."
DEFAULT_ROUTE=$(ip route show default | head -n 1)
if [[ $DEFAULT_ROUTE == *"rto_min"* ]]; then
    echo "[Network] MinRTO already set, skipping route update..."
else
    # shellcheck disable=SC2086
    ip route change $DEFAULT_ROUTE rto_min 50ms
fi

# 3. Optimize Socket Buffers for Low Latency (Avoid Bufferbloat)
echo "[Network] Tuning socket buffers..."
sysctl -w net.core.rmem_max=16777216
sysctl -w net.core.wmem_max=16777216
sysctl -w net.ipv4.tcp_rmem="4096 87380 16777216"
sysctl -w net.ipv4.tcp_wmem="4096 65536 16777216"

# 4. Low Latency Flags
echo "[Network] Enabling low-latency flags..."
sysctl -w net.ipv4.tcp_low_latency=1
sysctl -w net.ipv4.tcp_fastopen=3
sysctl -w net.ipv4.tcp_slow_start_after_idle=0 # Maintain speed after idle periods

# 5. UDP Optimizations (Critical for WebRTC)
echo "[Network] Optimizing UDP stack..."
sysctl -w net.core.netdev_max_backlog=5000
sysctl -w net.core.optmem_max=25165824

# 6. Persistence (Write to /etc/sysctl.d/99-remote-driving.conf)
cat <<EOF > /etc/sysctl.d/99-remote-driving.conf
# Remote Driving Optimizations
net.core.default_qdisc=fq
net.ipv4.tcp_congestion_control=bbr
net.ipv4.tcp_low_latency=1
net.ipv4.tcp_fastopen=3
net.ipv4.tcp_slow_start_after_idle=0
net.core.rmem_max=16777216
net.core.wmem_max=16777216
net.ipv4.tcp_rmem=4096 87380 16777216
net.ipv4.tcp_wmem=4096 65536 16777216
net.core.netdev_max_backlog=5000
EOF

echo "[Network] Optimization complete. Persistent config written to /etc/sysctl.d/99-remote-driving.conf"
echo "[Network] Current Congestion Control: $(sysctl net.ipv4.tcp_congestion_control)"
echo "[Network] Default Route MinRTO: $(ip route show default | grep -o 'rto_min [0-9ms]*' || echo 'Default (200ms)')"
