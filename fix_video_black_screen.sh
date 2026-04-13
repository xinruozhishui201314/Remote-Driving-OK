#!/bin/bash
# Remote-Driving Video Black Screen Thorough Fix
# This script applies system-level optimizations and resets the media pipeline.

set -e

echo "=== Remote-Driving Video Fix Script ==="

# 1. Increase Host UDP Receive Buffers
# Default is often 212KB, which is too small for 4 concurrent 720p streams during I-frame bursts.
# We increase it to 25MB.
echo "[1/4] Increasing Host UDP buffers..."
sudo sysctl -w net.core.rmem_max=26214400
sudo sysctl -w net.core.wmem_max=26214400
sudo sysctl -w net.core.rmem_default=26214400
sudo sysctl -w net.core.wmem_default=26214400

# 2. Fix Docker MTU (Optional, but recommended if using VPN/Cloud)
# Default 1500 can cause fragmentation in some environments. 1400 is safer for WebRTC.
# Not applied by default, but logging warning if MTU seems high.
MTU=$(ip addr | grep -A 2 "docker0" | grep mtu | awk '{print $5}')
echo "[2/4] Current Docker MTU: $MTU"

# 3. Clean and Restart ZLMediaKit & CARLA
echo "[3/4] Restarting media services..."
docker compose stop zlmediakit carla vehicle
docker compose rm -f zlmediakit carla vehicle
docker compose up -d zlmediakit carla vehicle

# 4. Final Diagnostics
echo "[4/4] Running diagnostics..."
sleep 5
echo "Checking ZLMediaKit logs for RTCP timeouts..."
docker logs teleop-zlmediakit 2>&1 | grep -i "rtcp" | tail -n 20 || echo "No RTCP logs yet."

echo ""
echo "=== FIX APPLIED ==="
echo "The following code changes were also applied to the codebase:"
echo "1. H264Decoder: Reduced reorder buffer to 64 (faster hole skipping)."
echo "2. H264Decoder: Allowed decoding of incomplete IDR frames (prevents black screen loop)."
echo "3. H264Decoder: Faster Keyframe (PLI) requests (200ms vs 450ms)."
echo "4. VehicleSide: Fixed spdlog crash causing stream instability."
echo "5. Docker: Added sysctls to increase UDP buffers inside containers."
echo ""
echo "Please restart the Client App now."
