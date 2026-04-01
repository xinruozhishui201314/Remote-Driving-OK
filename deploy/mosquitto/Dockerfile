# MQTT Broker (Mosquitto) 自定义镜像
# 用于包含初始化脚本和工具

FROM docker.1ms.run/eclipse-mosquitto:2.0

# 安装必要工具（用于初始化脚本）
RUN apk add --no-cache \
    openssl \
    bash

# 复制初始化脚本
COPY init-mosquitto.sh /mosquitto/config/init-mosquitto.sh
RUN chmod +x /mosquitto/config/init-mosquitto.sh

# 设置工作目录
WORKDIR /mosquitto

# 默认命令（会被 docker-compose 覆盖）
CMD ["mosquitto", "-c", "/mosquitto/config/mosquitto.conf"]
