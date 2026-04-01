#!/bin/bash

# --- 配置区域 ---
# 1. 设置你的智谱 API Key
export ZHIPUAI_API_KEY="bc92c2ac475a4e89a8145318c592af33.pLyln7TRLyk3hWMX"

# 2. 设置 API Base 地址 (针对智谱代码计划优化的路径)
# 注意：末尾必须带 /v4/ 
API_BASE="https://open.bigmodel.cn/api/coding/paas/v4"

# 3. 设置使用的模型
MODEL="openai/glm-4.7"

# 4. 项目路径 (自动获取当前目录)
PROJECT_DIR=$(pwd)

# --- 执行逻辑 ---
echo "🚀 正在启动 Aider 工程分析工具..."
echo "📂 当前项目: $PROJECT_DIR"
echo "🤖 使用模型: $MODEL"
echo "🔗 接口地址: $API_BASE"
echo "------------------------------------------------"

# 检查 Aider 是否安装
if ! command -v aider &> /dev/null
then
    echo "❌ 错误: 未找到 aider 命令。请先运行 source ~/.bashrc 或安装 aider。"
    exit 1
fi

# 高级效率参数组
EFFICIENCY_OPTS=(
  "--cache-prompts"
  "--stream"
  "--map-tokens" "6000"           # 注意：参数名和值分开写，不带多余引号
  "--auto-test"
  "--test-command" "make test"    # 直接写，不要在内部套单引号
  "--chat-language" "chinese"
  "--suggest-shell-commands"
)

IGNORE_OPTS=(
  "--ignore" ".docker/*"
  "--ignore" "logs/*"
  "--ignore" "target/*"
)

# 启动 Aider
# --map-tokens 1024 确保大型项目代码地图完整
# --auto-test 可以配合你的 Docker 测试使用
all_proxy="" http_proxy="" https_proxy="" cecli \
  --model "$MODEL" \
  --openai-api-base "$API_BASE" \
  --openai-api-key "$ZHIPUAI_API_KEY" \
  "${EFFICIENCY_OPTS[@]}" \
  "${IGNORE_OPTS[@]}" \
  --commit-prompt "请用简短的中文描述这次代码改动" \
  --cache-prompts \
  --no-show-model-warnings 
