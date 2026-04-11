pragma Singleton
import QtQuick 2.15

/**
 * SessionConstants — 客户端会话阶段常量（与 StackLayout.currentIndex 对齐）
 *
 * 使用：import RemoteDriving 1.0 → SessionConstants.stageLogin 等
 * 禁止在业务层硬编码 0/1/2，统一引用本单例以便重构与测试。
 */
QtObject {
    readonly property int stageLogin: 0
    readonly property int stageVehiclePick: 1
    readonly property int stageDriving: 2
}
