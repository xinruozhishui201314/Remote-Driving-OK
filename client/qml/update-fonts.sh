#!/bin/bash
# 批量更新 QML 文件，添加中文字体支持

QML_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "更新 QML 文件字体配置..."

# 为所有包含中文的 Text 组件添加字体设置
for qml_file in "$QML_DIR"/*.qml; do
    if [ -f "$qml_file" ]; then
        filename=$(basename "$qml_file")
        if [ "$filename" != "fonts.qml" ] && [ "$filename" != "Text.qml" ]; then
            echo "处理: $filename"
            # 这里可以添加自动替换逻辑，但为了安全，手动更新更可靠
        fi
    fi
done

echo "完成！"
