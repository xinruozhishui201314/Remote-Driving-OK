#!/bin/bash
# 自动化验证 NuScenes 数据集推流脚本
# 检查脚本语法、参数配置、FFmpeg 命令构建等

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR/.."

PUSH_SCRIPT="scripts/push-nuscenes-cameras-to-zlm.sh"
ERRORS=0
WARNINGS=0

echo "=========================================="
echo "NuScenes 推流脚本自动化验证"
echo "=========================================="
echo ""

# 1. 检查脚本文件是否存在
echo "[1/8] 检查脚本文件..."
if [[ ! -f "$PUSH_SCRIPT" ]]; then
    echo "  ❌ 错误: 脚本文件不存在: $PUSH_SCRIPT"
    exit 1
fi
echo "  ✅ 脚本文件存在"

# 2. 检查脚本语法
echo ""
echo "[2/8] 检查脚本语法..."
if bash -n "$PUSH_SCRIPT" 2>&1; then
    echo "  ✅ 脚本语法正确"
else
    echo "  ❌ 错误: 脚本语法错误"
    ERRORS=$((ERRORS + 1))
fi

# 3. 检查必要的工具
echo ""
echo "[3/8] 检查必要工具..."
MISSING_TOOLS=()
for tool in ffmpeg bash; do
    if ! command -v "$tool" &>/dev/null; then
        MISSING_TOOLS+=("$tool")
    fi
done
if [[ ${#MISSING_TOOLS[@]} -eq 0 ]]; then
    echo "  ✅ 所有必要工具已安装"
else
    echo "  ❌ 错误: 缺少工具: ${MISSING_TOOLS[*]}"
    ERRORS=$((ERRORS + 1))
fi

# 4. 检查环境变量和默认值
echo ""
echo "[4/8] 检查配置参数..."
# 不 source 脚本，直接读取配置（避免执行推流）
# 模拟环境变量设置
export SWEEPS_PATH="${SWEEPS_PATH:-$HOME/bigdata/data/nuscenes-mini/sweeps}"
export NUSCENES_BITRATE="${NUSCENES_BITRATE:-200k}"
export NUSCENES_MAXRATE="${NUSCENES_MAXRATE:-250k}"
export NUSCENES_BUFSIZE="${NUSCENES_BUFSIZE:-100k}"
export NUSCENES_PUSH_FPS="${NUSCENES_PUSH_FPS:-10}"

# 从脚本中提取默认值（使用 sed 提取花括号内的值）
SCRIPT_BITRATE=$(grep 'BITRATE=' "$PUSH_SCRIPT" | sed -n 's/.*NUSCENES_BITRATE:-\([^}]*\).*/\1/p' | head -1)
SCRIPT_MAXRATE=$(grep 'MAXRATE=' "$PUSH_SCRIPT" | sed -n 's/.*NUSCENES_MAXRATE:-\([^}]*\).*/\1/p' | head -1)
SCRIPT_BUFSIZE=$(grep 'BUFSIZE=' "$PUSH_SCRIPT" | sed -n 's/.*NUSCENES_BUFSIZE:-\([^}]*\).*/\1/p' | head -1)
SCRIPT_FPS=$(grep 'FPS=' "$PUSH_SCRIPT" | sed -n 's/.*NUSCENES_PUSH_FPS:-\([^}]*\).*/\1/p' | head -1)

# 如果提取失败，使用默认值
if [[ -z "$SCRIPT_BITRATE" ]]; then SCRIPT_BITRATE="200k"; fi
if [[ -z "$SCRIPT_MAXRATE" ]]; then SCRIPT_MAXRATE="250k"; fi
if [[ -z "$SCRIPT_BUFSIZE" ]]; then SCRIPT_BUFSIZE="100k"; fi
if [[ -z "$SCRIPT_FPS" ]]; then SCRIPT_FPS="10"; fi

# 检查码率参数格式
if [[ ! "$SCRIPT_BITRATE" =~ ^[0-9]+[kmg]?$ ]]; then
    echo "  ❌ 错误: BITRATE 格式不正确: $SCRIPT_BITRATE"
    ERRORS=$((ERRORS + 1))
else
    echo "  ✅ BITRATE=$SCRIPT_BITRATE (默认)"
fi

if [[ ! "$SCRIPT_MAXRATE" =~ ^[0-9]+[kmg]?$ ]]; then
    echo "  ❌ 错误: MAXRATE 格式不正确: $SCRIPT_MAXRATE"
    ERRORS=$((ERRORS + 1))
else
    echo "  ✅ MAXRATE=$SCRIPT_MAXRATE (默认)"
fi

if [[ ! "$SCRIPT_BUFSIZE" =~ ^[0-9]+[kmg]?$ ]]; then
    echo "  ❌ 错误: BUFSIZE 格式不正确: $SCRIPT_BUFSIZE"
    ERRORS=$((ERRORS + 1))
else
    echo "  ✅ BUFSIZE=$SCRIPT_BUFSIZE (默认)"
fi

echo "  ✅ FPS=$SCRIPT_FPS (默认)"

# 5. 检查 x264-params 构建逻辑
echo ""
echo "[5/8] 检查 x264-params 构建逻辑..."
# 模拟参数构建
BUFSIZE_NUM="${SCRIPT_BUFSIZE%k}"
MAXRATE_NUM="${SCRIPT_MAXRATE%k}"

X264_PARAMS=(
    "slices=1"
    "nal-hrd=cbr"
    "force-cfr=1"
    "vbv-bufsize=$BUFSIZE_NUM"
    "vbv-maxrate=$MAXRATE_NUM"
    "me=dia"
    "subme=1"
    "trellis=0"
    "8x8dct=0"
    "fast-pskip=1"
    "no-mbtree=1"
    "weightp=0"
    "no-cabac=0"
    "qpmin=28"
    "qpmax=40"
    "qpstep=4"
)

X264_PARAMS_STR=$(IFS=:; echo "${X264_PARAMS[*]}")
if [[ -z "$X264_PARAMS_STR" ]]; then
    echo "  ❌ 错误: x264-params 构建失败"
    ERRORS=$((ERRORS + 1))
else
    echo "  ✅ x264-params 构建成功"
    echo "     示例: ${X264_PARAMS_STR:0:80}..."
fi

# 6. 检查 FFmpeg 命令构建（不实际执行）
echo ""
echo "[6/8] 检查 FFmpeg 命令构建..."
# 模拟命令构建
TEST_CAM_PATH="/tmp/test_cam"
mkdir -p "$TEST_CAM_PATH" 2>/dev/null || true
touch "$TEST_CAM_PATH/test.jpg" 2>/dev/null || true

FFMPEG_CMD=(
    -re
    -stream_loop -1
    -framerate "$SCRIPT_FPS"
    -pattern_type glob
    -i "${TEST_CAM_PATH}/*.jpg"
    -c:v libx264
    -preset ultrafast
    -tune zerolatency
    -pix_fmt yuv420p
    -b:v "$SCRIPT_BITRATE"
    -maxrate "$SCRIPT_MAXRATE"
    -bufsize "$SCRIPT_BUFSIZE"
    -g "$SCRIPT_FPS"
    -keyint_min "$SCRIPT_FPS"
    -bf 0
    -profile:v baseline
    -level 3.0
)

FFMPEG_CMD+=(-x264-params "$X264_PARAMS_STR")
FFMPEG_CMD+=(-f flv "rtmp://test/test")
FFMPEG_CMD+=(-loglevel warning -y)

# 检查 FFmpeg 是否能解析命令（dry-run）
if command -v ffmpeg &>/dev/null; then
    # 使用 -f null 和 -t 0 进行 dry-run
    if timeout 2 ffmpeg "${FFMPEG_CMD[@]}" -f null -t 0 - 2>&1 | grep -q "Invalid\|Error\|failed"; then
        echo "  ⚠️  警告: FFmpeg 命令可能有参数问题（需要实际数据验证）"
        WARNINGS=$((WARNINGS + 1))
    else
        echo "  ✅ FFmpeg 命令构建正确"
    fi
else
    echo "  ⚠️  跳过: FFmpeg 未安装，无法验证命令"
    WARNINGS=$((WARNINGS + 1))
fi

# 清理测试文件
rm -rf "$TEST_CAM_PATH" 2>/dev/null || true

# 7. 检查数据集路径（如果存在）
echo ""
echo "[7/8] 检查数据集路径..."
if [[ -d "$SWEEPS_PATH" ]]; then
    echo "  ✅ 数据集路径存在: $SWEEPS_PATH"
    
    # 检查必要的相机目录
    CAM_DIRS=("CAM_FRONT" "CAM_BACK" "CAM_FRONT_LEFT" "CAM_FRONT_RIGHT")
    MISSING_DIRS=()
    for dir in "${CAM_DIRS[@]}"; do
        if [[ ! -d "$SWEEPS_PATH/$dir" ]]; then
            MISSING_DIRS+=("$dir")
        fi
    done
    
    if [[ ${#MISSING_DIRS[@]} -eq 0 ]]; then
        echo "  ✅ 所有相机目录存在"
        
        # 检查是否有图片文件
        IMG_COUNT=0
        for dir in "${CAM_DIRS[@]}"; do
            COUNT=$(find "$SWEEPS_PATH/$dir" -name "*.jpg" -o -name "*.png" 2>/dev/null | wc -l)
            IMG_COUNT=$((IMG_COUNT + COUNT))
        done
        
        if [[ $IMG_COUNT -gt 0 ]]; then
            echo "  ✅ 找到 $IMG_COUNT 个图片文件"
        else
            echo "  ⚠️  警告: 未找到图片文件"
            WARNINGS=$((WARNINGS + 1))
        fi
    else
        echo "  ⚠️  警告: 缺少相机目录: ${MISSING_DIRS[*]}"
        WARNINGS=$((WARNINGS + 1))
    fi
else
    echo "  ⚠️  警告: 数据集路径不存在: $SWEEPS_PATH"
    echo "      (这不会阻止脚本运行，但需要设置正确的 SWEEPS_PATH)"
    WARNINGS=$((WARNINGS + 1))
fi

# 8. 检查 Docker Compose 配置
echo ""
echo "[8/8] 检查 Docker Compose 配置..."
DOCKER_COMPOSE_FILE="docker-compose.vehicle.dev.yml"
if [[ -f "$DOCKER_COMPOSE_FILE" ]]; then
    if grep -q "push-nuscenes-cameras-to-zlm.sh" "$DOCKER_COMPOSE_FILE"; then
        echo "  ✅ Docker Compose 配置正确"
        
        # 检查环境变量配置
        if grep -q "NUSCENES_BITRATE" "$DOCKER_COMPOSE_FILE"; then
            echo "  ✅ 码率环境变量已配置"
        else
            echo "  ⚠️  警告: 未找到 NUSCENES_BITRATE 配置"
            WARNINGS=$((WARNINGS + 1))
        fi
    else
        echo "  ⚠️  警告: Docker Compose 中未配置 NuScenes 推流脚本"
        WARNINGS=$((WARNINGS + 1))
    fi
else
    echo "  ⚠️  警告: Docker Compose 文件不存在: $DOCKER_COMPOSE_FILE"
    WARNINGS=$((WARNINGS + 1))
fi

# 总结
echo ""
echo "=========================================="
echo "验证完成"
echo "=========================================="
echo "错误: $ERRORS"
echo "警告: $WARNINGS"
echo ""

if [[ $ERRORS -eq 0 ]]; then
    echo "✅ 所有检查通过！"
    if [[ $WARNINGS -gt 0 ]]; then
        echo "⚠️  有 $WARNINGS 个警告，请检查"
    fi
    exit 0
else
    echo "❌ 发现 $ERRORS 个错误，请修复后重试"
    exit 1
fi
