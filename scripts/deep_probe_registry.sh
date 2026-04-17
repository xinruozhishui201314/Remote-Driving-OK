#!/bin/bash
# 深度探测 Harbor 内部配置与 Docker 兼容性

REGISTRY_IP="192.168.2.10"
USER="wangqs"
PASS="Wangqs123"

echo "--- [1] 探测 Harbor 系统配置信息 ---"
# 尝试获取 Harbor 的系统信息，看它自认为的 URL 是什么
SYSTEM_INFO=$(curl -s -u "$USER:$PASS" "http://$REGISTRY_IP/api/v2.0/systeminfo" || echo "API 访问受限")
echo "系统信息摘要: $(echo $SYSTEM_INFO | jq -r '.harbor_storage.type // "Unknown"') storage, Auth mode: $(echo $SYSTEM_INFO | jq -r '.auth_mode // "Unknown"')"

echo -e "\n--- [2] 模拟 Docker 的三步认证链路 ---"
# 第一步：获取 Token (指定 push 范围)
TOKEN=$(curl -s -u "$USER:$PASS" "http://$REGISTRY_IP/service/token?service=harbor-registry&scope=repository:remote_driving/zlmediakit:push" | jq -r .token)

# 第二步：使用 Token 探测镜像层 (HEAD 请求)
# 我们分别测试带端口和不带端口的情况
echo "测试带端口 (HTTP 80) 的 HEAD 请求:"
curl -v -I -H "Authorization: Bearer $TOKEN" "http://$REGISTRY_IP:80/v2/remote_driving/zlmediakit/blobs/sha256:5a59284bad1455d5687d978af93a7988c7c6d2b4d389a93957f7b8fc00e82769" 2>&1 | grep -iE "HTTP/|Location|Docker" | head -n 5

echo -e "\n测试不带端口 (HTTP) 的 HEAD 请求:"
curl -v -I -H "Authorization: Bearer $TOKEN" "http://$REGISTRY_IP/v2/remote_driving/zlmediakit/blobs/sha256:5a59284bad1455d5687d978af93a7988c7c6d2b4d389a93957f7b8fc00e82769" 2>&1 | grep -iE "HTTP/|Location|Docker" | head -n 5

echo -e "\n--- [3] 检查 Docker 守护进程对该地址的识别 ---"
# 查看 Docker 如何解析这些地址
docker info 2>/dev/null | grep -A 5 "Insecure Registries"
