#!/bin/bash
# 修正后的诊断脚本

REGISTRY="192.168.2.10:80"
REPO="remote_driving/zlmediakit"
USER="wangqs"
PASS="Wangqs123"

function decode_jwt(){
   decode_base64_url() {
     local len=$((${#1} % 4))
     local result="$1"
     if [ $len -eq 2 ]; then result="$1"=="; fi
     if [ $len -eq 3 ]; then result="$1"="; fi
     echo "$result" | tr '_-' '/+' | base64 -d
   }
   decode_base64_url $(echo "$1" | cut -d"." -f2)
}

echo "--- [1] 检查本地 Docker 配置 ---"
grep "\"$REGISTRY\"" /etc/docker/daemon.json || echo "[FAIL] $REGISTRY 不在 insecure-registries 中"

echo -e "\n--- [2] 探测 Harbor 认证挑战 ---"
CHALLENGE=$(curl -sI http://$REGISTRY/v2/ | grep -i "Www-Authenticate" || true)
echo "认证挑战: $CHALLENGE"

echo -e "\n--- [3] 获取并解码 Token ---"
REALM="http://192.168.2.10/service/token"
TOKEN_RESPONSE=$(curl -s -u "$USER:$PASS" "$REALM?service=harbor-registry&scope=repository:$REPO:push")
TOKEN=$(echo $TOKEN_RESPONSE | jq -r .token)

if [ "$TOKEN" == "null" ]; then
    echo "[ERROR] 获取 Token 失败: $TOKEN_RESPONSE"
    exit 1
fi

echo "Token Payload:"
decode_jwt "$TOKEN" | jq .

echo -e "\n--- [4] 验证重定向行为 (Location Header) ---"
UPLOAD_RES=$(curl -sI -X POST -H "Authorization: Bearer $TOKEN" "http://$REGISTRY/v2/$REPO/blobs/uploads/")
echo "$UPLOAD_RES"

LOCATION=$(echo "$UPLOAD_RES" | grep -i "Location" || true)
if [[ $LOCATION == *":80"* ]]; then
    echo "[OK] Location 包含端口号: $LOCATION"
else
    echo -e "\033[31m[CRITICAL] 根本原因确诊：Harbor 返回的跳转地址丢失了 :80 端口！\033[0m"
    echo "Harbor 返回: $LOCATION"
    echo "Docker 客户端预期目标应保持一致性。地址变更为不带端口的 IP 会导致 Insecure Registry 校验失败或 Token 丢失。"
fi
