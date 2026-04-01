#!/bin/bash
# ═══════════════════════════════════════════
# MQTT Broker (Mosquitto) 初始化脚本
# 用于创建默认用户和配置
# ═══════════════════════════════════════════

set -e

# 注意：此脚本在容器内执行，路径为容器内路径

MOSQUITTO_CONFIG_DIR="/mosquitto/config"
MOSQUITTO_DATA_DIR="/mosquitto/data"
MOSQUITTO_LOG_DIR="/mosquitto/log"

echo "=========================================="
echo "MQTT Broker (Mosquitto) 初始化"
echo "=========================================="

# ────────────────────────
# §1 创建目录
# ────────────────────────

mkdir -p "$MOSQUITTO_CONFIG_DIR/certs"
mkdir -p "$MOSQUITTO_DATA_DIR"
mkdir -p "$MOSQUITTO_LOG_DIR"

# ────────────────────────
# §2 创建密码文件（如果不存在）
# ────────────────────────

if [ ! -f "$MOSQUITTO_CONFIG_DIR/passwd" ]; then
    echo "创建默认密码文件..."
    
    # 检查 mosquitto_passwd 是否可用
    if command -v mosquitto_passwd &> /dev/null; then
        # 创建密码文件（使用环境变量或默认值）
        VEHICLE_PASSWORD="${MQTT_VEHICLE_PASSWORD:-vehicle_password_change_in_prod}"
        CLIENT_PASSWORD="${MQTT_CLIENT_PASSWORD:-client_password_change_in_prod}"
        ADMIN_PASSWORD="${MQTT_ADMIN_PASSWORD:-admin_password_change_in_prod}"
        
        # 创建密码文件
        mosquitto_passwd -c -b "$MOSQUITTO_CONFIG_DIR/passwd" vehicle_side "$VEHICLE_PASSWORD"
        mosquitto_passwd -b "$MOSQUITTO_CONFIG_DIR/passwd" client_user "$CLIENT_PASSWORD"
        mosquitto_passwd -b "$MOSQUITTO_CONFIG_DIR/passwd" admin "$ADMIN_PASSWORD"
        
        echo "✓ 密码文件已创建"
        echo "  默认用户："
        echo "    - vehicle_side / $VEHICLE_PASSWORD"
        echo "    - client_user / $CLIENT_PASSWORD"
        echo "    - admin / $ADMIN_PASSWORD"
        echo ""
        echo "⚠️  警告：生产环境必须修改默认密码！"
    else
        echo "⚠️  mosquitto_passwd 不可用，跳过密码文件创建"
        echo "   请手动创建密码文件："
        echo "   mosquitto_passwd -c $MOSQUITTO_CONFIG_DIR/passwd username"
    fi
else
    echo "✓ 密码文件已存在，跳过创建"
fi

# ────────────────────────
# §3 创建 ACL 文件（如果不存在）
# ────────────────────────

if [ ! -f "$MOSQUITTO_CONFIG_DIR/acl" ]; then
    echo "创建默认 ACL 文件..."
    cat > "$MOSQUITTO_CONFIG_DIR/acl" << 'EOF'
# 车端权限
user vehicle_side
topic read vehicle/control
topic read vehicle/+/control
topic write vehicle/status
topic write vehicle/+/status

# 客户端权限
user client_user
topic write vehicle/control
topic write vehicle/+/control
topic read vehicle/status
topic read vehicle/+/status

# 管理员权限
user admin
topic readwrite #
topic read $SYS/#
EOF
    echo "✓ ACL 文件已创建"
else
    echo "✓ ACL 文件已存在，跳过创建"
fi

# ────────────────────────
# §4 生成自签名证书（如果不存在，仅用于开发环境）
# ────────────────────────

if [ ! -f "$MOSQUITTO_CONFIG_DIR/certs/server.crt" ]; then
    echo "生成自签名证书（仅用于开发环境）..."
    
    if command -v openssl &> /dev/null; then
        # 生成 CA 私钥
        openssl genrsa -out "$MOSQUITTO_CONFIG_DIR/certs/ca.key" 2048
        
        # 生成 CA 证书
        openssl req -new -x509 -days 3650 -key "$MOSQUITTO_CONFIG_DIR/certs/ca.key" \
            -out "$MOSQUITTO_CONFIG_DIR/certs/ca.crt" \
            -subj "/C=CN/ST=Beijing/L=Beijing/O=Remote-Driving/CN=MQTT-CA"
        
        # 生成服务器私钥
        openssl genrsa -out "$MOSQUITTO_CONFIG_DIR/certs/server.key" 2048
        
        # 生成服务器证书请求
        openssl req -new -key "$MOSQUITTO_CONFIG_DIR/certs/server.key" \
            -out "$MOSQUITTO_CONFIG_DIR/certs/server.csr" \
            -subj "/C=CN/ST=Beijing/L=Beijing/O=Remote-Driving/CN=mqtt-broker"
        
        # 使用 CA 签名服务器证书
        openssl x509 -req -in "$MOSQUITTO_CONFIG_DIR/certs/server.csr" \
            -CA "$MOSQUITTO_CONFIG_DIR/certs/ca.crt" \
            -CAkey "$MOSQUITTO_CONFIG_DIR/certs/ca.key" \
            -CAcreateserial \
            -out "$MOSQUITTO_CONFIG_DIR/certs/server.crt" \
            -days 3650
        
        # 设置权限
        chmod 600 "$MOSQUITTO_CONFIG_DIR/certs/server.key"
        chmod 644 "$MOSQUITTO_CONFIG_DIR/certs/server.crt"
        chmod 644 "$MOSQUITTO_CONFIG_DIR/certs/ca.crt"
        
        echo "✓ 自签名证书已生成"
        echo "⚠️  警告：生产环境必须使用有效的 CA 签名证书！"
    else
        echo "⚠️  openssl 不可用，跳过证书生成"
        echo "   生产环境必须提供有效的 TLS 证书"
    fi
else
    echo "✓ TLS 证书已存在，跳过生成"
fi

# ────────────────────────
# §5 设置权限（仅对可写目录）
# ────────────────────────

# 注意：配置文件目录可能是只读挂载，跳过 chown
# 只对数据目录和日志目录设置权限
if [ -w "$MOSQUITTO_DATA_DIR" ]; then
    chown -R mosquitto:mosquitto "$MOSQUITTO_DATA_DIR" 2>/dev/null || true
fi
if [ -w "$MOSQUITTO_LOG_DIR" ]; then
    chown -R mosquitto:mosquitto "$MOSQUITTO_LOG_DIR" 2>/dev/null || true
fi
# 配置文件目录如果是可写的，才设置权限
if [ -w "$MOSQUITTO_CONFIG_DIR" ]; then
    chown -R mosquitto:mosquitto "$MOSQUITTO_CONFIG_DIR" 2>/dev/null || true
fi

echo ""
echo "=========================================="
echo "初始化完成"
echo "=========================================="
