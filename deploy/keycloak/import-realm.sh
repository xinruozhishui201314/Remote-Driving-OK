#!/bin/bash
# Keycloak Realm 导入脚本
# 此脚本用于在 Keycloak 启动后导入 realm 配置

set -e

KEYCLOAK_URL="${KEYCLOAK_URL:-http://localhost:8080}"
KEYCLOAK_ADMIN="${KEYCLOAK_ADMIN:-admin}"
KEYCLOAK_ADMIN_PASSWORD="${KEYCLOAK_ADMIN_PASSWORD:-admin}"
REALM_FILE="${REALM_FILE:-./realm-export.json}"

echo "等待 Keycloak 服务就绪..."
max_attempts=30
attempt=0

while [ $attempt -lt $max_attempts ]; do
    if curl -f -s "${KEYCLOAK_URL}/health/ready" > /dev/null 2>&1; then
        echo "Keycloak 服务已就绪"
        break
    fi
    attempt=$((attempt + 1))
    echo "等待中... ($attempt/$max_attempts)"
    sleep 2
done

if [ $attempt -eq $max_attempts ]; then
    echo "错误: Keycloak 服务未能在预期时间内就绪"
    exit 1
fi

echo "获取 Keycloak Admin Token..."
TOKEN=$(curl -s -X POST "${KEYCLOAK_URL}/realms/master/protocol/openid-connect/token" \
    -H "Content-Type: application/x-www-form-urlencoded" \
    -d "username=${KEYCLOAK_ADMIN}" \
    -d "password=${KEYCLOAK_ADMIN_PASSWORD}" \
    -d "grant_type=password" \
    -d "client_id=admin-cli" | jq -r '.access_token')

if [ -z "$TOKEN" ] || [ "$TOKEN" = "null" ]; then
    echo "错误: 无法获取 Admin Token"
    exit 1
fi

echo "检查 Realm 是否已存在..."
REALM_EXISTS=$(curl -s -X GET "${KEYCLOAK_URL}/admin/realms/teleop" \
    -H "Authorization: Bearer ${TOKEN}" \
    -H "Content-Type: application/json" \
    -w "%{http_code}" -o /dev/null)

if [ "$REALM_EXISTS" = "200" ]; then
    echo "Realm 'teleop' 已存在，删除旧配置..."
    curl -s -X DELETE "${KEYCLOAK_URL}/admin/realms/teleop" \
        -H "Authorization: Bearer ${TOKEN}" \
        -H "Content-Type: application/json"
    echo "旧 Realm 已删除"
fi

echo "导入 Realm 配置..."
IMPORT_RESPONSE=$(curl -s -w "\n%{http_code}" -X POST "${KEYCLOAK_URL}/admin/realms" \
    -H "Authorization: Bearer ${TOKEN}" \
    -H "Content-Type: application/json" \
    -d @"${REALM_FILE}")

HTTP_CODE=$(echo "$IMPORT_RESPONSE" | tail -n1)
BODY=$(echo "$IMPORT_RESPONSE" | head -n-1)

if [ "$HTTP_CODE" = "201" ] || [ "$HTTP_CODE" = "204" ]; then
    echo "✓ Realm 'teleop' 导入成功"
else
    echo "错误: Realm 导入失败 (HTTP $HTTP_CODE)"
    echo "响应: $BODY"
    exit 1
fi

echo ""
echo "=========================================="
echo "Keycloak Realm 配置完成"
echo "=========================================="
echo "Realm: teleop"
echo "Admin Console: ${KEYCLOAK_URL}/admin"
echo "Admin Username: ${KEYCLOAK_ADMIN}"
echo ""
echo "已定义的角色:"
echo "  - admin: 管理用户、车辆、策略、审计、录制"
echo "  - owner: 账号拥有者，管理自己账号下 VIN 的绑定、授权"
echo "  - operator: 可申请控制指定 VIN（需被授权）"
echo "  - observer: 仅拉流查看指定 VIN（需被授权）"
echo "  - maintenance: 查看故障/诊断，可进行受限操作"
echo "=========================================="
