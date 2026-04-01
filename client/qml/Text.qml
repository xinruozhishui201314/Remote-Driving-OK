import QtQuick 2.15

/**
 * 中文文本组件
 * 自动使用中文字体
 */
Text {
    // 自动使用中文字体（如果可用）
    Component.onCompleted: {
        if (typeof window !== "undefined" && window.chineseFont) {
            font.family = window.chineseFont
        }
    }
}
