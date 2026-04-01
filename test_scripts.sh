#!/bin/bash
# 脚本测试验证脚本
# 测试所有编译、运行、调试脚本的语法和基本功能

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "=========================================="
echo "脚本测试验证"
echo "=========================================="
echo ""

ERRORS=0
WARNINGS=0

# 测试函数
test_script() {
    local script_path="$1"
    local script_name="$2"
    
    echo -n "测试 $script_name... "
    
    if [ ! -f "$script_path" ]; then
        echo "✗ 文件不存在"
        ERRORS=$((ERRORS + 1))
        return 1
    fi
    
    if [ ! -x "$script_path" ]; then
        echo "⚠ 文件不可执行（已修复）"
        chmod +x "$script_path"
        WARNINGS=$((WARNINGS + 1))
    fi
    
    # 语法检查
    if bash -n "$script_path" 2>&1; then
        echo "✓ 语法正确"
        return 0
    else
        echo "✗ 语法错误"
        ERRORS=$((ERRORS + 1))
        return 1
    fi
}

# 测试 Client 脚本
echo "1. 测试 Client 工程脚本"
echo "----------------------------------------"
test_script "client/build.sh" "client/build.sh"
test_script "client/run.sh" "client/run.sh"
test_script "client/debug.sh" "client/debug.sh"
echo ""

# 测试 Media 脚本
echo "2. 测试 Media 工程脚本"
echo "----------------------------------------"
test_script "media/build.sh" "media/build.sh"
test_script "media/run.sh" "media/run.sh"
test_script "media/debug.sh" "media/debug.sh"
echo ""

# 测试 Vehicle-side 脚本
echo "3. 测试 Vehicle-side 工程脚本"
echo "----------------------------------------"
test_script "Vehicle-side/build.sh" "Vehicle-side/build.sh"
test_script "Vehicle-side/run.sh" "Vehicle-side/run.sh"
test_script "Vehicle-side/debug.sh" "Vehicle-side/debug.sh"
echo ""

# 测试 Makefile
echo "4. 测试 Makefile"
echo "----------------------------------------"
if [ -f "Makefile" ]; then
    if make -n help > /dev/null 2>&1; then
        echo "✓ Makefile 语法正确"
    else
        echo "✗ Makefile 语法错误"
        ERRORS=$((ERRORS + 1))
    fi
else
    echo "✗ Makefile 不存在"
    ERRORS=$((ERRORS + 1))
fi
echo ""

# 测试 CMakeLists.txt
echo "5. 测试 CMakeLists.txt"
echo "----------------------------------------"

# Client CMakeLists.txt
if [ -f "client/CMakeLists.txt" ]; then
    if cmake --version > /dev/null 2>&1; then
        cd client
        if cmake -S . -B build_test_cmake -DCMAKE_BUILD_TYPE=Debug > /dev/null 2>&1; then
            echo "✓ client/CMakeLists.txt 语法正确"
            rm -rf build_test_cmake
        else
            echo "⚠ client/CMakeLists.txt 配置可能有问题（依赖缺失）"
            WARNINGS=$((WARNINGS + 1))
        fi
        cd ..
    else
        echo "⚠ CMake 未安装，跳过 CMakeLists.txt 测试"
        WARNINGS=$((WARNINGS + 1))
    fi
else
    echo "✗ client/CMakeLists.txt 不存在"
    ERRORS=$((ERRORS + 1))
fi

# Vehicle-side CMakeLists.txt
if [ -f "Vehicle-side/CMakeLists.txt" ]; then
    if cmake --version > /dev/null 2>&1; then
        cd Vehicle-side
        if cmake -S . -B build_test_cmake -DCMAKE_BUILD_TYPE=Debug > /dev/null 2>&1; then
            echo "✓ Vehicle-side/CMakeLists.txt 语法正确"
            rm -rf build_test_cmake
        else
            echo "⚠ Vehicle-side/CMakeLists.txt 配置可能有问题（依赖缺失）"
            WARNINGS=$((WARNINGS + 1))
        fi
        cd ..
    fi
else
    echo "✗ Vehicle-side/CMakeLists.txt 不存在"
    ERRORS=$((ERRORS + 1))
fi
echo ""

# 测试文件权限
echo "6. 检查文件权限"
echo "----------------------------------------"
SCRIPTS=(
    "client/build.sh"
    "client/run.sh"
    "client/debug.sh"
    "media/build.sh"
    "media/run.sh"
    "media/debug.sh"
    "Vehicle-side/build.sh"
    "Vehicle-side/run.sh"
    "Vehicle-side/debug.sh"
)

for script in "${SCRIPTS[@]}"; do
    if [ -f "$script" ]; then
        if [ -x "$script" ]; then
            echo "✓ $script 可执行"
        else
            echo "⚠ $script 不可执行（正在修复）"
            chmod +x "$script"
            WARNINGS=$((WARNINGS + 1))
        fi
    fi
done
echo ""

# 总结
echo "=========================================="
echo "测试结果总结"
echo "=========================================="
echo "错误: $ERRORS"
echo "警告: $WARNINGS"
echo ""

if [ $ERRORS -eq 0 ]; then
    echo "✓ 所有脚本语法检查通过！"
    echo ""
    echo "下一步："
    echo "1. 安装必要的依赖（Qt6、libdatachannel、Paho MQTT C++ 等）"
    echo "2. 运行编译脚本: cd client && ./build.sh"
    echo "3. 运行程序: ./run.sh"
    exit 0
else
    echo "✗ 发现 $ERRORS 个错误，请检查上述输出"
    exit 1
fi
