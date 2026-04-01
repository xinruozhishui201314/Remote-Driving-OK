# 基于宿主机已有的 carlasim/carla:latest 构建，满足 C++ Bridge 运行。
# Paho MQTT C/C++ 使用宿主机已拉取的源码（deploy/carla/deps/）COPY 进镜像内安装，不在容器内拉取 GitHub。
#
# 构建前请在宿主机执行：bash deploy/carla/clone-deps.sh

FROM carlasim/carla:latest

USER root

RUN mv /etc/apt/sources.list.d/cuda.list /etc/apt/sources.list.d/cuda.list.bak 2>/dev/null || true; \
    mv /etc/apt/sources.list.d/nvidia-ml.list /etc/apt/sources.list.d/nvidia-ml.list.bak 2>/dev/null || true

# 拆成多步：apt → 构建链 → CMake 下载/解压 → 清理，便于看进度与利用层缓存
RUN echo "[build] [1/6] apt update" && apt-get update

RUN echo "[build] [2/6] apt install ca-certificates wget" && \
    apt-get install -y --no-install-recommends ca-certificates wget

RUN echo "[build] [3/6] apt install ffmpeg, python3-pip, ninja-build, build-essential, libssl-dev" && \
    apt-get update && apt-get install -y --no-install-recommends \
    ffmpeg python3-pip python3-setuptools ninja-build build-essential libssl-dev

# 使用宿主机 deps/cmake.tar.gz（构建前请下载到 deploy/carla/deps/cmake.tar.gz），无需构建时从 GitHub 拉取
RUN echo "[build] [4/6] Using local CMake tarball (deps/cmake.tar.gz)"
COPY deps/cmake.tar.gz /tmp/cmake.tar.gz
RUN echo "[build] [5/6] Extracting CMake to /usr/local" && \
    tar xzf /tmp/cmake.tar.gz -C /usr/local --strip-components=1 && rm /tmp/cmake.tar.gz

RUN echo "[build] [6/6] apt cleanup" && \
    apt-get clean && rm -rf /var/lib/apt/lists/*

# 宿主机 clone 的 Paho MQTT C/C++：同一 RUN 内先装 C 再装 C++，保证链接时 /usr/local 可见
COPY deps/paho.mqtt.c /tmp/paho.mqtt.c
COPY deps/FindPahoMqttC.cmake /tmp/FindPahoMqttC.cmake
COPY deps/paho.mqtt.cpp /tmp/paho.mqtt.cpp
RUN set -e && \
    echo "[build] [2/4] Configuring & building Paho MQTT C..." && \
    cd /tmp/paho.mqtt.c && mkdir -p build && cd build && \
    cmake .. -DPAHO_WITH_SSL=OFF -DPAHO_BUILD_SHARED=ON -DCMAKE_INSTALL_PREFIX=/usr/local -DCMAKE_INSTALL_LIBDIR=lib && \
    cmake --build . && cmake --install . && ldconfig && \
    cp src/libpaho-mqtt3a.so src/libpaho-mqtt3c.so /usr/local/lib/ && ldconfig && \
    mkdir -p /opt/paho-c-include && cp ../src/MQTTAsync.h ../src/MQTTClient.h ../src/MQTTClientPersistence.h ../src/MQTTProperties.h ../src/MQTTReasonCodes.h ../src/MQTTSubscribeOpts.h ../src/MQTTExportDeclarations.h /opt/paho-c-include/ && \
    rm -rf /tmp/paho.mqtt.c && \
    cp /opt/paho-c-include/*.h /tmp/paho.mqtt.cpp/src/mqtt/ && \
    echo "[build] [3/4] Configuring & building Paho MQTT C++..." && \
    cd /tmp/paho.mqtt.cpp && mkdir -p build && cd build && \
    cmake -G Ninja .. -DPAHO_WITH_SSL=OFF -DPAHO_BUILD_SHARED=ON -DPAHO_BUILD_STATIC=OFF -DCMAKE_MODULE_PATH=/tmp && \
    cmake --build . && cmake --install . && ldconfig && \
    rm -rf /tmp/paho.mqtt.cpp /tmp/FindPahoMqttC.cmake /opt/paho-c-include && \
    echo "[build] [3/4] done."

# Python Bridge 固化：安装 Python 3.7 + carla 0.9.13（与服务器同版），避免运行时 rpc::rpc_error (version)
RUN set -e; \
    (apt-get update && apt-get install -y --no-install-recommends software-properties-common 2>/dev/null) || true; \
    (add-apt-repository -y ppa:deadsnakes/ppa 2>/dev/null && apt-get update) || true; \
    (apt-get install -y --no-install-recommends python3.7 python3.7-venv python3.7-distutils 2>/dev/null) || true; \
    if [ -x /usr/bin/python3.7 ] && [ -f /home/carla/PythonAPI/carla/dist/carla-0.9.13-cp37-cp37m-manylinux_2_27_x86_64.whl ]; then \
      wget -q -O /tmp/get-pip.py https://bootstrap.pypa.io/pip/3.7/get-pip.py && python3.7 /tmp/get-pip.py -q; \
      /usr/bin/python3.7 -m pip install -q typing_extensions /home/carla/PythonAPI/carla/dist/carla-0.9.13-cp37-cp37m-manylinux_2_27_x86_64.whl paho-mqtt numpy; \
    fi; \
    apt-get clean 2>/dev/null; rm -rf /var/lib/apt/lists/* 2>/dev/null; true

# 当前仅使用 C++ Bridge：MQTT 已由上方 Paho C/C++ 提供，无需再装 Python 的 paho-mqtt/opencv。
# Python Bridge 使用 python3.7 + 上一步预装的 carla 0.9.13（entrypoint 会优先调用 python3.7）。

COPY entrypoint.sh /entrypoint.sh
RUN echo "[build] [4/4] Installing entrypoint." && chmod +x /entrypoint.sh

WORKDIR /workspace
ENTRYPOINT ["/entrypoint.sh"]
