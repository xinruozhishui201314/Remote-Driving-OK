/**
 * 全局主题常量（单例）
 * 所有 QML 组件通过 Theme.xxx 引用，便于统一修改
 *
 * 使用方式（在 qmldir 已注册 singleton Theme 的前提下）：
 *   import "styles" as ThemeModule
 *   color: ThemeModule.Theme.colorBackground
 *   font.family: ThemeModule.Theme.chineseFont || font.family
 *
 * 颜色命名规范：
 *   - drivingColor* 系列为内部真实属性
 *   - 不含 driving 前缀的颜色名通过别名指向 driving 系列（如 colorBackground → drivingColorBackground）
 *   - 若别名与内部属性名冲突（已有属性 vs 别名），删除旧属性，保留别名
 */
pragma Singleton
import QtQuick 2.15

QtObject {
    // property alias 右侧必须是「id」或「id.属性」（见 Qt 文档 Property Aliases）；不能写裸属性名
    id: themeRoot

    // ─── DrivingInterface 专用颜色（统一来源）───────────────────────────────
    // 新设计：所有颜色统一使用 driving 系列，旧名通过别名兼容
    // 保留旧属性仅当其没有别名冲突时；冲突的已在上方删除
    // 从 DrivingInterface.qml 提取，用于保持视觉一致性
    readonly property color drivingColorBackground: "#0F0F1A"    // 深色背景
    readonly property color drivingColorPanel: "#1A1A2A"        // 面板背景
    readonly property color drivingColorBorder: "#2A2A3E"      // 边框
    readonly property color drivingColorBorderActive: "#4A90E2" // 激活边框
    readonly property color drivingColorAccent: "#50C878"      // 强调色（绿色）
    readonly property color drivingColorWarning: "#E8A030"     // 警告色
    readonly property color drivingColorDanger: "#E85050"      // 危险色
    readonly property color drivingColorTextPrimary: "#FFFFFF" // 主要文字
    readonly property color drivingColorTextSecondary: "#B0B0C0" // 次要文字
    
    // 按钮颜色
    readonly property color drivingColorButtonBg: "#1E2433"
    readonly property color drivingColorButtonBorder: "#3A4A6E"
    readonly property color drivingColorButtonBgHover: "#26314A"
    readonly property color drivingColorButtonBgPressed: "#1A2235"
    
    // ─── 尺寸 ────────────────────────────────────────────────────────────────
    readonly property int topBarHeight:    48
    readonly property int bottomHudHeight: 120
    readonly property int gaugeSize:       100
    
    readonly property int marginSmall:  8
    readonly property int marginNormal: 12
    readonly property int marginLarge:  16
    
    // ─── 字体 ─────────────────────────────────────────────────────────────────
    readonly property int fontSmall:  12
    readonly property int fontNormal: 14
    readonly property int fontLarge:  18
    readonly property int fontTitle:  22
    
    // ─── 中文字体（统一获取）─────────────────────────────────────────────────
    // 所有组件应使用此属性获取中文字体，避免重复代码
    readonly property string chineseFont: {
        if (typeof window !== "undefined" && window.chineseFont) 
            return window.chineseFont
        var fonts = [
            "WenQuanYi Zen Hei",      // 文泉驿正黑
            "WenQuanYi Micro Hei",    // 文泉驿微米黑
            "Noto Sans CJK SC",       // Noto Sans 简体中文
            "Noto Sans CJK TC",       // Noto Sans 繁体中文
            "Source Han Sans SC",     // 思源黑体 简体
            "Droid Sans Fallback",    // Droid 回退字体
            "SimHei",                 // 黑体
            "Microsoft YaHei"          // 微软雅黑
        ]
        var availableFonts = Qt.fontFamilies()
        for (var i = 0; i < fonts.length; i++) {
            if (availableFonts.indexOf(fonts[i]) !== -1) 
                return fonts[i]
        }
        return ""
    }

    /** UI 默认字体族名（与 chineseFont 一致；ControlButton 等使用 Theme.fontFamily） */
    readonly property string fontFamily: chineseFont
    
    // ─── 兼容性别名（兼容旧代码）─────────────────────────────────────────────
    // driving 系列已消除了与别名的命名冲突（冲突的旧属性已删除）
    // 额外补充：旧代码使用但 driving 系列未直接提供的颜色（语义映射）
    // Qt 文档：property alias 语法为 [default] property alias <name>: <ref>，勿与 readonly 混用
    // 别名指向的 driving* 已为 readonly，对外仍不可写
    property alias colorBackground: themeRoot.drivingColorBackground
    property alias colorPanel: themeRoot.drivingColorPanel         // VideoPanel / LoginDialog
    property alias colorSurface: themeRoot.drivingColorPanel       // surface ≈ panel
    property alias colorPrimary: themeRoot.drivingColorBorderActive  // primary ≈ borderActive
    property alias colorText: themeRoot.drivingColorTextPrimary     // text ≈ textPrimary
    property alias colorTextDim: themeRoot.drivingColorTextSecondary  // dim text ≈ secondary
    property alias colorGood: themeRoot.drivingColorAccent          // good ≈ accent（绿色）
    property alias colorCaution: themeRoot.drivingColorWarning      // caution ≈ warning（橙色）
    property alias colorBorder: themeRoot.drivingColorBorder
    property alias colorBorderActive: themeRoot.drivingColorBorderActive
    property alias colorAccent: themeRoot.drivingColorAccent
    property alias colorWarning: themeRoot.drivingColorWarning
    property alias colorDanger: themeRoot.drivingColorDanger
    property alias colorTextPrimary: themeRoot.drivingColorTextPrimary
    property alias colorTextSecondary: themeRoot.drivingColorTextSecondary
    property alias colorButtonBg: themeRoot.drivingColorButtonBg
    property alias colorButtonBorder: themeRoot.drivingColorButtonBorder
    property alias colorButtonBgHover: themeRoot.drivingColorButtonBgHover
    property alias colorButtonBgPressed: themeRoot.drivingColorButtonBgPressed
}
