#!/bin/bash
# 在 carlasim/carla 容器内：按需安装 Bridge 依赖，再启动 CARLA 服务，最后运行 CARLA Bridge。
# 直接使用 carlasim/carla:latest，无需构建自定义镜像；挂载 carla-src 后优先使用其 PythonAPI。
echo "[entrypoint] ========== 入口脚本启动 $(date -Iseconds 2>/dev/null || date) =========="

# 提升文件描述符限制，防止 CARLA 仿真相机较多时报 Bad file descriptor
ulimit -n 65535 || true

set -e

CARLA_MAP="${CARLA_MAP:-}"
CARLA_HOST="${CARLA_HOST:-127.0.0.1}"
CARLA_PORT="${CARLA_PORT:-2000}"
BRIDGE_DIR="${BRIDGE_DIR:-/workspace/carla-bridge}"
CARLA_PYTHON_SRC="${CARLA_PYTHON_SRC:-}"
CARLA_DATASET="${CARLA_DATASET:-/data/carla}"

# 0) Bridge 依赖：remote-driving/carla-with-bridge 镜像已预装 ffmpeg/pip/libjpeg，不执行 apt-get
mkdir -p /workspace/logs
chmod 777 /workspace/logs || true

# 1) 配置 Python CARLA（优先 python3.7 避免 python3 用 py2.7 egg 导致 segfault，再尝试 wheel/egg/PyPI）
CARLA_PY3_OK=0
# 1d-first) 优先使用 python3.7 + carla（镜像固化时已装 0.9.13），避免 python3(3.6) 加载 egg 时 Segmentation fault
if [ -x /usr/bin/python3.7 ] && env -u PYTHONPATH /usr/bin/python3.7 -c "import carla; getattr(carla, 'Client')" 2>/dev/null; then
  echo "[entrypoint] 使用 python3.7 的 carla（已固化），跳过 python3 检测"
  CARLA_PY3_OK=1
fi
# 1a) 若尚未可用，尝试镜像内置 wheel（carlasim/carla 常见路径）
if [ "$CARLA_PY3_OK" -ne 1 ] && [ -f "/home/carla/PythonAPI/carla/dist/carla-0.9.13-py3-none-any.whl" ]; then
  if pip3 install -q "/home/carla/PythonAPI/carla/dist/carla-0.9.13-py3-none-any.whl" 2>/dev/null; then
    if python3 -c "import carla; getattr(carla, 'Client')" 2>/dev/null; then
      echo "[entrypoint] 使用镜像内置 wheel 成功"
      CARLA_PY3_OK=1
    fi
  fi
fi
# 1b) 已有 carla 模块（如刚安装的 wheel）或挂载/PyPI/镜像 egg
if [ "$CARLA_PY3_OK" -ne 1 ] && python3 -c "import carla; getattr(carla, 'Client')" 2>/dev/null; then
  echo "[entrypoint] 使用已有 carla 模块 (python3)"
  CARLA_PY3_OK=1
fi
# 1c) 尝试挂载 carla-src 的预构建 egg（若有）
if [ "$CARLA_PY3_OK" -ne 1 ] && [ -n "$CARLA_PYTHON_SRC" ] && [ -d "$CARLA_PYTHON_SRC/PythonAPI/carla/dist" ]; then
    for egg in "$CARLA_PYTHON_SRC/PythonAPI/carla/dist"/carla*.egg; do
      if [ -f "$egg" ]; then
        export PYTHONPATH="${egg}${PYTHONPATH:+:$PYTHONPATH}"
        echo "[entrypoint] 使用挂载 CARLA 预构建 egg: $egg"
        if python3 -c "import carla; getattr(carla, 'Client')" 2>/dev/null; then
          CARLA_PY3_OK=1
          break
        fi
      fi
    done
  fi
  # 1b) 再尝试 PyPI carla 包（快、稳定、避免镜像 egg segfault）
  if [ "$CARLA_PY3_OK" -ne 1 ]; then
    echo "[entrypoint] 尝试 PyPI carla 包..."
    if pip3 install -q 'carla>=0.9.5' 2>/dev/null; then
      if python3 -c "import carla; getattr(carla, 'Client')" 2>/dev/null; then
        echo "[entrypoint] 使用 PyPI carla 包"
        CARLA_PY3_OK=1
      fi
    fi
  fi
  # 1c) 若仍不可用，再尝试镜像内 egg（常见为 py2.7，python3 可能 segfault）
  if [ "$CARLA_PY3_OK" -ne 1 ]; then
    for egg in /home/carla/PythonAPI/carla/dist/carla*.egg; do
      if [ -f "$egg" ]; then
        export PYTHONPATH="${egg}${PYTHONPATH:+:$PYTHONPATH}"
        echo "[entrypoint] 尝试镜像内 PythonAPI: $egg"
        break
      fi
    done
    if python3 -c "import carla; getattr(carla, 'Client')" 2>/dev/null; then
      CARLA_PY3_OK=1
      echo "[entrypoint] 使用镜像内 egg 成功"
    fi
  fi
if [ "$CARLA_PY3_OK" -ne 1 ]; then
  echo "[entrypoint] python3 无法加载 carla；将回退到 C++ Bridge（testsrc）"
fi
export CARLA_PY3_OK

# 2) Bridge 依赖（若挂载了 bridge 目录且有 requirements.txt）
if [ -f "$BRIDGE_DIR/requirements.txt" ]; then
  python3 -m pip install -q -r "$BRIDGE_DIR/requirements.txt" 2>/dev/null || true
fi

# 3) 启动 CARLA 服务（后台）。carlasim/carla 要求非 root 运行，CarlaUE4.sh 在 /home/carla
#
# 渲染模式（三档）：
#   offscreen (默认) : GPU 加速无头渲染（EGL，无需 DISPLAY/X11）—— 推荐用于服务器/Docker
#   offscreen-x11   : offscreen 模式但使用 X11 DISPLAY（需主机有 X 服务器或 Xvfb）
#   window           : 显示仿真窗口（需 CARLA_GPU_RENDER_MODE=offscreen-x11 + DISPLAY）
#
# GPU 检测逻辑：
#   - 有 nvidia-smi 且 GPU 可用 → 使用 offscreen 模式（EGL GPU 加速）
#   - 无 nvidia-smi → 回退到 offscreen-x11（依赖 DISPLAY）
#   - CARLA_GPU_RENDER_MODE=window → 显示窗口模式
#
CARLA_ROOT="${CARLA_ROOT:-/home/carla}"
CARLA_USER="${CARLA_USER:-carla}"
# 默认无头模式（GPU 加速渲染）；设为 window 时显示仿真窗口（需 DISPLAY 环境变量）
CARLA_SHOW_WINDOW="${CARLA_SHOW_WINDOW:-0}"
# GPU 渲染模式：offscreen=EGL GPU 无头(默认) / offscreen-x11=X11 DISPLAY 无头 / window=显示窗口
CARLA_GPU_RENDER_MODE="${CARLA_GPU_RENDER_MODE:-offscreen}"

# ── GPU 检测 ───────────────────────────────────────────────────────────────────
_HAVE_NVIDIA=0
if command -v nvidia-smi >/dev/null 2>&1 && nvidia-smi >/dev/null 2>&1; then
  _GPU_NAME=$(nvidia-smi --query-gpu=name --format=csv,noheader 2>/dev/null | head -1)
  _GPU_UTIL=$(nvidia-smi --query-gpu=utilization.gpu --format=csv,noheader,nounits 2>/dev/null | head -1)
  echo "[entrypoint] 检测到 NVIDIA GPU: $_GPU_NAME 利用率: ${_GPU_UTIL}%（nvidia-smi 可用）"
  _HAVE_NVIDIA=1
else
  echo "[entrypoint] 未检测到 NVIDIA GPU（nvidia-smi 不可用或失败）"
fi

# ── 构建 CARLA 启动参数 ────────────────────────────────────────────────────────
UE_MAP=""
[ -n "$CARLA_MAP" ] && UE_MAP="/Game/Maps/$CARLA_MAP"

RENDER_OPTS=""
CARLA_LAUNCH_CMD=""

case "${CARLA_GPU_RENDER_MODE}" in
  offscreen)
    # ── GPU 加速无头渲染（EGL，无需 DISPLAY/X11）───
    # Unreal Engine 在容器中通过 EGL/GPU 直接渲染 offscreen。
    # NVIDIA Container Toolkit 确保 GPU 驱动对容器可见。
    RENDER_OPTS="-RenderOffScreen -opengl"
    # 不需要 DISPLAY，清除以避免 Mesa 软件渲染意外激活
    export DISPLAY=""
    echo "[entrypoint] 渲染模式: 无头 GPU 加速（EGL，无 DISPLAY）"
    ;;

  offscreen-x11)
    # ── X11 DISPLAY 无头渲染 ──────────────────────────────────────────────
    # 使用 DISPLAY 环境变量（X11 或 Xvfb）。
    # 若无 X 服务器，CARLA 会回退到软件渲染（llvmpipe）。
    RENDER_OPTS="-RenderOffScreen"
    DISPLAY_VAL="${DISPLAY:-:0}"
    export DISPLAY="$DISPLAY_VAL"
    echo "[entrypoint] 渲染模式: 无头 X11 DISPLAY=$DISPLAY_VAL（若无 DISPLAY 服务器将用 llvmpipe）"
    ;;

  window)
    # ── 窗口模式 ─────────────────────────────────────────────────────────
    # 显示 CARLA 仿真窗口。需要 DISPLAY + xhost +local:docker
    # GPU 渲染由 DISPLAY 后的 X 服务器处理（通常是 NVIDIA 驱动）
    RENDER_OPTS=""
    DISPLAY_VAL="${DISPLAY:-:0}"
    export DISPLAY="$DISPLAY_VAL"
    echo "[entrypoint] 渲染模式: 窗口 DISPLAY=$DISPLAY_VAL（需主机 xhost +local:docker）"
    ;;

  *)
    echo "[entrypoint] 未知 CARLA_GPU_RENDER_MODE=$CARLA_GPU_RENDER_MODE，使用默认 offscreen"
    RENDER_OPTS="-RenderOffScreen -opengl"
    export DISPLAY=""
    ;;
esac

# ── DISPLAY 可用性检查（诊断日志，帮助定位 offscreen/window 切换原因）─────────
echo "[entrypoint] DISPLAY 检查: DISPLAY='$DISPLAY'"
if [ -n "$DISPLAY" ]; then
    _dnum="${DISPLAY#*:}"
    if [ -S "/tmp/.X11-unix/X${_dnum}" ] 2>/dev/null; then
        echo "[entrypoint] DISPLAY=$DISPLAY 可用（X socket 存在）"
    else
        echo "[entrypoint] 警告: DISPLAY=$DISPLAY 但 X socket /tmp/.X11-unix/X${_dnum} 不存在!"
    fi
else
    echo "[entrypoint] DISPLAY 未设置"
fi

# 当 CARLA_SHOW_WINDOW=1 时强制使用 window 模式（兼容旧配置）
# 但如果 GPU 可用（EGL offscreen 已就绪）却无 DISPLAY/X11，自动降级到 offscreen
if [ "${CARLA_SHOW_WINDOW}" = "1" ]; then
    if [ "$RENDER_OPTS" != "" ] && [ "$_HAVE_NVIDIA" = "1" ]; then
        echo "[entrypoint] 强制窗口模式被否决：GPU 可用但无 DISPLAY/X11，降级到 offscreen"
        RENDER_OPTS="-RenderOffScreen -opengl"
        export DISPLAY=""
    else
        RENDER_OPTS=""
        DISPLAY_VAL="${DISPLAY:-:0}"
        export DISPLAY="$DISPLAY_VAL"
        echo "[entrypoint] CARLA_SHOW_WINDOW=1，强制窗口模式 DISPLAY=$DISPLAY_VAL"
    fi
fi

# ── 打印诊断信息 ───────────────────────────────────────────────────────────────
echo "[entrypoint] CARLA 启动参数: UE_MAP=$UE_MAP RENDER_OPTS='$RENDER_OPTS' DISPLAY='$DISPLAY'"

# ── 启动 CARLA ────────────────────────────────────────────────────────────────
# 警告：CARLA 拒绝以 root 身份运行，必须切换回 carla 用户
# 但 ulimit -n 已在脚本开头由 root 执行，容器内所有进程将继承该限制
CARLA_LOG_FILE="/workspace/logs/carla_server_raw.log"
echo "[entrypoint] CARLA 日志将重定向至 $CARLA_LOG_FILE"
CARLA_EXEC="su -s /bin/bash $CARLA_USER -c \"export DISPLAY='$DISPLAY'; cd $CARLA_ROOT && bash CarlaUE4.sh $UE_MAP $RENDER_OPTS -nosound -quality-level=Low\" > $CARLA_LOG_FILE 2>&1"
eval "$CARLA_EXEC" &
CARLA_PID=$!
# 获取真正的 CarlaUE4-Linux-Shipping 进程 PID（eval 启动的是 bash）
sleep 2
REAL_CARLA_PID=$(pgrep -f "CarlaUE4-Linux-Shipping" || echo "$CARLA_PID")
echo "$REAL_CARLA_PID" > /tmp/carla_server.pid
export CARLA_SERVER_PID="$REAL_CARLA_PID"
echo "[entrypoint] CARLA PID: $CARLA_SERVER_PID"

# 4) 等待端口就绪（CARLA 启动较慢，等待 90 秒；Bridge 启动前再额外等待 30 秒）
echo "[entrypoint] 等待 CARLA 端口 $CARLA_PORT ..."
for i in $(seq 1 90); do
  if python3 -c "
import socket
s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.settimeout(2)
try:
  s.connect(('$CARLA_HOST', $CARLA_PORT))
  s.close()
  exit(0)
except Exception:
  exit(1)
" 2>/dev/null; then
    echo "[entrypoint] CARLA 端口已就绪（${i}s），继续等待 CARLA RPC 完全初始化..."
    break
  fi
  [ $i -eq 90 ] && { echo "[entrypoint] CARLA 端口超时（90s）"; exit 1; }
  sleep 1
done

echo "[entrypoint] 额外等待 45 秒让 CARLA RPC 完全就绪..."
sleep 45

# 5) GPU 渲染验证（启动后检查 nvidia-smi 利用率）
if [ "$_HAVE_NVIDIA" = "1" ] && [ "$RENDER_OPTS" != "" ]; then
  _GPU_UTIL_AFTER=$(nvidia-smi --query-gpu=utilization.gpu --format=csv,noheader,nounits 2>/dev/null | head -1)
  echo "[entrypoint] GPU 利用率: 启动前 ${_GPU_UTIL}% → 启动后 ${_GPU_UTIL_AFTER}%（如 >5% 说明 GPU 渲染激活）"
fi

# 5) 运行 CARLA Bridge（前台，连接本机 CARLA + Compose 内 MQTT/ZLM）
export CARLA_HOST CARLA_PORT CARLA_MAP
# 若在 Compose 网络中，MQTT/ZLM 使用服务名
export MQTT_BROKER="${MQTT_BROKER:-mosquitto}"
export ZLM_HOST="${ZLM_HOST:-zlmediakit}"
export ZLM_RTMP_PORT="${ZLM_RTMP_PORT:-1935}"
export ZLM_APP="${ZLM_APP:-teleop}"
export VIN="${VIN:-carla-sim-001}"
[ -n "$CARLA_MAP" ] && export CARLA_MAP

# 优先使用 Python Bridge（CARLA 真实相机推流）；默认 1，与 docker-compose.carla.yml 一致
USE_PYTHON_BRIDGE="${USE_PYTHON_BRIDGE:-1}"
case "${USE_PYTHON_BRIDGE}" in 1|true|yes|on) USE_PYTHON_BRIDGE=1 ;; *) USE_PYTHON_BRIDGE=0 ;; esac
echo "[entrypoint] USE_PYTHON_BRIDGE=$USE_PYTHON_BRIDGE CARLA_PY3_OK=$CARLA_PY3_OK"

# 6) 运行 Bridge（前台，成为 PID 1）
if [ "$USE_PYTHON_BRIDGE" = "1" ] && [ "$CARLA_PY3_OK" = "1" ] && [ -f "$BRIDGE_DIR/carla_bridge.py" ]; then
  # 6.0) 核心安全检查：Python 语法校验（防止由于语法错误导致容器静默无限重启）
  echo "[entrypoint] 正在对 $BRIDGE_DIR/carla_bridge.py 进行 Python 语法检查..."
  # ★ 最终修复：使用 ast.parse() 仅在内存中进行语法树解析，完全不涉及 py_compile 的物理文件写入逻辑。
  # 彻底规避只读文件系统下 py_compile 尝试写入 __pycache__ 导致的 OSError。
  _CHECK_PY="python3"
  [ -x /usr/bin/python3.7 ] && _CHECK_PY="/usr/bin/python3.7"
  if ! $_CHECK_PY -c "import ast; ast.parse(open('$BRIDGE_DIR/carla_bridge.py').read())" >/dev/null 2>&1; then
    echo "[entrypoint] ★★★ FATAL ERROR: $BRIDGE_DIR/carla_bridge.py 语法校验失败！ ★★★"
    echo "[entrypoint] 详情如下："
    $_CHECK_PY -c "import ast; ast.parse(open('$BRIDGE_DIR/carla_bridge.py').read())" 2>&1 | head -n 20
    echo "[entrypoint] 链路中断：由于 Bridge 代码存在语法错误，无法启动推流，链路退出。"
    exit 1
  fi
  echo "[entrypoint] Python 语法检查通过 ✓"

  for whl in /home/carla/PythonAPI/carla/dist/carla-*-py3-none-any.whl /home/carla/PythonAPI/carla/dist/carla-*-cp*.whl; do
    if [ -f "$whl" ]; then
      echo "[entrypoint] 尝试与服务器同版 carla: $whl"
      python3 -m pip install -q --force-reinstall "$whl" 2>/dev/null && break || true
    fi
  done
  # 若镜像内无匹配当前 Python 的 wheel（如仅 cp37 而系统为 3.6），则从 PyPI 安装与服务器同版 0.9.13，避免 rpc::rpc_error (version)
  INSTALLED_CARLA=$(python3 -m pip show carla 2>/dev/null | sed -n 's/^Version: *//p')
  if [ -z "$INSTALLED_CARLA" ] || [ "$INSTALLED_CARLA" != "0.9.13" ]; then
    echo "[entrypoint] 强制安装与服务器同版 client: carla==0.9.13 (当前: ${INSTALLED_CARLA:-未安装})"
    if ! python3 -m pip install -q --force-reinstall 'carla==0.9.13' 2>/dev/null; then
      # 容器内 pip 索引可能只有旧版，使用 PyPI 官方 cp36 wheel 直链
      CARLA_013_CP36_URL="https://files.pythonhosted.org/packages/5f/43/11d2643a5efdb1b3fbed3cccd033cebcf3bb0592ba05abdd54a123194827/carla-0.9.13-cp36-cp36m-manylinux_2_27_x86_64.whl"
      echo "[entrypoint] 从直链安装 carla 0.9.13 cp36: $CARLA_013_CP36_URL"
      python3 -m pip install -q --force-reinstall "$CARLA_013_CP36_URL" 2>/dev/null || true
    fi
  fi
  echo "[entrypoint] 安装 Python Bridge 依赖: paho-mqtt numpy"
  python3 -m pip install -q paho-mqtt numpy 2>/dev/null || true
  # 优先使用 python3.7（镜像固化时已装 carla 0.9.13），避免 3.6 + 0.9.5 的 version 报错；不继承 PYTHONPATH
  PYTHON_BRIDGE_CMD="python3"
  if [ -x /usr/bin/python3.7 ] && env -u PYTHONPATH /usr/bin/python3.7 -c "import carla; getattr(carla, 'Client')" 2>/dev/null; then
    PYTHON_BRIDGE_CMD="/usr/bin/python3.7"
    echo "[entrypoint] 使用 python3.7（carla 0.9.13 已固化）"
    /usr/bin/python3.7 -m pip install -q typing_extensions paho-mqtt numpy 2>/dev/null || true
  fi
  echo "[entrypoint] 阶段: 即将 exec Python Bridge (CARLA 0.9.13 API)"
  echo "[entrypoint] 命令: env -u PYTHONPATH $PYTHON_BRIDGE_CMD $BRIDGE_DIR/carla_bridge.py"
  exec env -u PYTHONPATH $PYTHON_BRIDGE_CMD "$BRIDGE_DIR/carla_bridge.py"
fi
# C++ Bridge（优先从镜像内 /usr/local/bin 找——由 Dockerfile 固化，不被 volume 挂载遮蔽）
if [ -x "/usr/local/bin/carla_bridge" ] 2>/dev/null; then
  echo "[entrypoint] 启动 C++ Bridge (镜像内置): /usr/local/bin/carla_bridge"
  exec /usr/local/bin/carla_bridge
fi
# C++ Bridge（挂载目录，docker-compose.carla.yml 挂载 ./carla-bridge → /workspace/carla-bridge）
if [ -x "$BRIDGE_DIR/build/carla_bridge" ] 2>/dev/null; then
  echo "[entrypoint] 启动 C++ Bridge: $BRIDGE_DIR/build/carla_bridge"
  exec "$BRIDGE_DIR/build/carla_bridge"
fi
if [ -x "$BRIDGE_DIR/carla_bridge" ] 2>/dev/null; then
  echo "[entrypoint] 启动 C++ Bridge: $BRIDGE_DIR/carla_bridge"
  exec "$BRIDGE_DIR/carla_bridge"
fi
if [ "$CARLA_PY3_OK" = "1" ] && [ -f "$BRIDGE_DIR/carla_bridge.py" ]; then
  echo "[entrypoint] 回退到 Python Bridge: $BRIDGE_DIR/carla_bridge.py"
  INSTALLED_CARLA=$(python3 -m pip show carla 2>/dev/null | sed -n 's/^Version: *//p')
  if [ -z "$INSTALLED_CARLA" ] || [ "$INSTALLED_CARLA" != "0.9.13" ]; then
    python3 -m pip install -q --force-reinstall 'carla==0.9.13' 2>/dev/null || \
    python3 -m pip install -q --force-reinstall "https://files.pythonhosted.org/packages/5f/43/11d2643a5efdb1b3fbed3cccd033cebcf3bb0592ba05abdd54a123194827/carla-0.9.13-cp36-cp36m-manylinux_2_27_x86_64.whl" 2>/dev/null || true
  fi
  python3 -m pip install -q paho-mqtt numpy 2>/dev/null || true
  PYTHON_BRIDGE_CMD="python3"
  [ -x /usr/bin/python3.7 ] && env -u PYTHONPATH /usr/bin/python3.7 -c "import carla" 2>/dev/null && PYTHON_BRIDGE_CMD="/usr/bin/python3.7"
  exec env -u PYTHONPATH $PYTHON_BRIDGE_CMD "$BRIDGE_DIR/carla_bridge.py"
fi
echo "[entrypoint] 错误: 无可用的 Bridge（需 carla_bridge 或 carla_bridge.py）"
exit 1
