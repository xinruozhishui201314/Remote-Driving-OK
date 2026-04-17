#!/bin/bash
# 终极诊断脚本：检测网络、MTU、认证、重定向

REGISTRY_IP="192.168.2.10"
REPO="remote_driving/zlmediakit"
USER="wangqs"
PASS="Wangqs123"

echo "--- [1] 网络层探测：MTU 检测 ---"
echo "检查本地 MTU:"
ip addr show | grep mtu
echo "探测到服务器的路径 MTU (如果不通，说明中间有静默丢包):"
ping -M do -s 1472 -c 1 $REGISTRY_IP || echo "1500 字节包不通，可能存在 MTU 限制"
ping -M do -s 1400 -c 1 $REGISTRY_IP && echo "1400 字节包通了，建议调小 MTU"

echo -e "\n--- [2] 协议层探测：强制 HTTP 行为测试 ---"
# 获取 Token
TOKEN=$(curl -s -u "$USER:$PASS" "http://$REGISTRY_IP/service/token?service=harbor-registry&scope=repository:$REPO:push" | jq -r .token)

echo "测试 Harbor 内部跳转地址 (检查是否强制跳到 443):"
curl -v -I -H "Authorization: Bearer $TOKEN" -X POST "http://$REGISTRY_IP:80/v2/$REPO/blobs/uploads/" 2>&1 | grep -iE "Location|HTTP/"

echo -e "\n--- [3] 模拟推送最后一步 (PUT) 的服务器响应 ---"
# 模拟那个报错的 PUT 请求
curl -v -X PUT -H "Authorization: Bearer $TOKEN" "http://$REGISTRY_IP/v2/$REPO/blobs/uploads/test-diagnostic-session?digest=sha256:5a59284bad1455d5687d978af93a7988c7c6d2b4d389a93957f7b8fc00e82769" 2>&1 | grep -iE "HTTP/|Server|X-Request-Id"

echo -e "\n--- [4] 检查 Docker 内部状态 ---"
# 检查是否有僵死的上传任务
docker ps -a | grep zlmediakit || true
