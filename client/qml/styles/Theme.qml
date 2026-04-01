/**
 * 全局主题常量（《客户端架构设计》§3.4.2）。
 * 所有 QML 组件通过 Theme.xxx 引用，便于统一修改。
 */
pragma Singleton
import QtQuick 2.15

QtObject {
    // ─── 颜色 ────────────────────────────────────────────────────────────────
    readonly property color colorBackground:  "#1A1A2E"
    readonly property color colorSurface:     "#16213E"
    readonly property color colorPrimary:     "#0F3460"
    readonly property color colorAccent:      "#E94560"

    readonly property color colorText:        "#E0E0E0"
    readonly property color colorTextDim:     "#9E9E9E"

    readonly property color colorGood:        "#4CAF50"
    readonly property color colorCaution:     "#FFC107"
    readonly property color colorWarn:        "#FF5722"
    readonly property color colorDanger:      "#F44336"
    readonly property color colorEmergency:   "#D50000"

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
}
