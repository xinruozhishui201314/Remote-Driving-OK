import QtQuick 2.15

/**
 * 字体配置组件
 * 提供中文字体支持
 */
QtObject {
    id: fontConfig
    
    // 中文字体列表（按优先级）
    readonly property var chineseFonts: [
        "WenQuanYi Zen Hei",      // 文泉驿正黑
        "WenQuanYi Micro Hei",     // 文泉驿微米黑
        "Noto Sans CJK SC",        // Noto Sans 简体中文
        "Noto Sans CJK TC",        // Noto Sans 繁体中文
        "Source Han Sans SC",      // 思源黑体 简体
        "Droid Sans Fallback",     // Droid 回退字体
        "SimHei",                  // 黑体
        "Microsoft YaHei"          // 微软雅黑
    ]
    
    // 获取可用的中文字体
    function getChineseFont() {
        var availableFonts = Qt.fontFamilies()
        for (var i = 0; i < chineseFonts.length; i++) {
            if (availableFonts.indexOf(chineseFonts[i]) !== -1) {
                return chineseFonts[i]
            }
        }
        return ""  // 如果没有找到，返回空字符串，使用默认字体
    }
    
    // 默认中文字体
    property string defaultChineseFont: getChineseFont()
}
