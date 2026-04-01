#!/bin/bash
# 验证 Dev Container 配置是否正确

set -e

echo "=========================================="
echo "Verifying Dev Container Configuration"
echo "=========================================="

ERRORS=0

# 检查 Docker 是否运行
echo -n "Checking Docker daemon... "
if docker info > /dev/null 2>&1; then
    echo "✓"
else
    echo "✗ Docker is not running"
    ERRORS=$((ERRORS + 1))
fi

# 检查镜像是否存在
echo -n "Checking Docker image... "
if docker images | grep -q "docker.1ms.run/stateoftheartio/qt6.*6.8-gcc-aqt"; then
    echo "✓"
else
    echo "✗ Image not found: docker.1ms.run/stateoftheartio/qt6:6.8-gcc-aqt"
    echo "  Run: docker pull docker.1ms.run/stateoftheartio/qt6:6.8-gcc-aqt"
    ERRORS=$((ERRORS + 1))
fi

# 检查 devcontainer.json 语法（支持 JSONC 注释）
echo -n "Checking devcontainer.json syntax... "
if [ -f ".devcontainer/devcontainer.json" ]; then
    # devcontainer.json 支持 JSONC（带注释的 JSON），所以只做基本检查
    if grep -q '"name"' .devcontainer/devcontainer.json && \
       grep -q '"image"' .devcontainer/devcontainer.json; then
        echo "✓ (JSONC format - comments allowed)"
    else
        echo "✗ Missing required fields"
        ERRORS=$((ERRORS + 1))
    fi
else
    echo "✗ File not found"
    ERRORS=$((ERRORS + 1))
fi

# 检查必需文件
echo -n "Checking required files... "
if [ -f ".devcontainer/devcontainer.json" ] && [ -f ".devcontainer/setup.sh" ]; then
    echo "✓"
else
    echo "✗ Missing required files"
    ERRORS=$((ERRORS + 1))
fi

# 检查 setup.sh 权限
echo -n "Checking setup.sh permissions... "
if [ -x ".devcontainer/setup.sh" ]; then
    echo "✓"
else
    echo "⚠ (not executable, fixing...)"
    chmod +x .devcontainer/setup.sh
fi

echo ""
if [ $ERRORS -eq 0 ]; then
    echo "=========================================="
    echo "✓ All checks passed!"
    echo "=========================================="
    echo ""
    echo "Next steps:"
    echo "1. Open Cursor/VSCode"
    echo "2. Press F1 and select 'Dev Containers: Reopen in Container'"
    echo "3. Wait for container to start"
    echo ""
    exit 0
else
    echo "=========================================="
    echo "✗ Found $ERRORS error(s)"
    echo "=========================================="
    echo ""
    echo "Please fix the errors above before proceeding."
    echo ""
    exit 1
fi
