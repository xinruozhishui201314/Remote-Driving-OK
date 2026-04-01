#!/usr/bin/env bash
# 为 Keycloak realm teleop 下的用户 e2e-test 设置密码（Realm 导入时可能未导入密码）
# 用法：./scripts/keycloak-set-e2e-password.sh
# 依赖：Keycloak 已启动，admin 可登录；curl、jq

set -e
KEYCLOAK_URL="${KEYCLOAK_URL:-http://localhost:8080}"
ADMIN_USER="${KEYCLOAK_ADMIN:-admin}"
ADMIN_PASS="${KEYCLOAK_ADMIN_PASSWORD:-admin}"
REALM="teleop"
USERNAME="e2e-test"
NEW_PASSWORD="e2e-test-password"

echo "[Keycloak] 获取 admin token..."
ADMIN_TOKEN=$(curl -s -X POST "${KEYCLOAK_URL}/realms/master/protocol/openid-connect/token" \
  -d "client_id=admin-cli" \
  -d "username=${ADMIN_USER}" \
  -d "password=${ADMIN_PASS}" \
  -d "grant_type=password" | jq -r '.access_token')

if [ -z "$ADMIN_TOKEN" ] || [ "$ADMIN_TOKEN" = "null" ]; then
  echo "[Keycloak] 获取 admin token 失败，请检查 $KEYCLOAK_URL 及 admin 账号" >&2
  exit 1
fi

echo "[Keycloak] 查找用户 $USERNAME..."
USER_ID=$(curl -s -H "Authorization: Bearer $ADMIN_TOKEN" \
  "${KEYCLOAK_URL}/admin/realms/${REALM}/users?username=${USERNAME}" | jq -r '.[0].id')

if [ -z "$USER_ID" ] || [ "$USER_ID" = "null" ]; then
  echo "[Keycloak] 未找到用户 $USERNAME，请确认 realm $REALM 已导入" >&2
  exit 1
fi

echo "[Keycloak] 设置密码..."
curl -s -X PUT -H "Authorization: Bearer $ADMIN_TOKEN" \
  -H "Content-Type: application/json" \
  "${KEYCLOAK_URL}/admin/realms/${REALM}/users/${USER_ID}/reset-password" \
  -d "{\"type\":\"password\",\"value\":\"${NEW_PASSWORD}\",\"temporary\":false}"

# PUT 成功通常返回 204 无内容
echo "[Keycloak] 完成。可用 e2e-test / e2e-test-password 登录 realm teleop。"
