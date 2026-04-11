import QtQuick 2.15

/**
 * 远驾键盘快捷键逻辑（由 DrivingInterface.Keys.onPressed 调用，保持根 Item focus）。
 * 安全死手经 facade.appServices.safetyMonitor（DrivingFacade v3）。
 */
Item {
    width: 0
    height: 0

    property Item facade: null
    /** TeleopPresentationState */
    property var teleop: null

    function handlePressed(event) {
        if (!teleop)
            return
        var sm = (facade && facade.appServices) ? facade.appServices.safetyMonitor : null
        if (sm && typeof sm.notifyOperatorActivity === "function")
            sm.notifyOperatorActivity()
        switch (event.key) {
        case Qt.Key_P:
            teleop.currentGear = "P"
            break
        case Qt.Key_N:
            teleop.currentGear = "N"
            break
        case Qt.Key_R:
            teleop.currentGear = "R"
            break
        case Qt.Key_D:
            teleop.currentGear = "D"
            break
        case Qt.Key_Left:
            teleop.leftTurnActive = !teleop.leftTurnActive
            if (teleop.rightTurnActive)
                teleop.rightTurnActive = false
            break
        case Qt.Key_Right:
            teleop.rightTurnActive = !teleop.rightTurnActive
            if (teleop.leftTurnActive)
                teleop.leftTurnActive = false
            break
        case Qt.Key_Up:
            teleop.targetSpeed = Math.min(100, teleop.targetSpeed + 5)
            teleop.speedCommandSent(teleop.targetSpeed)
            break
        case Qt.Key_Down:
            teleop.targetSpeed = Math.max(0, teleop.targetSpeed - 5)
            teleop.speedCommandSent(teleop.targetSpeed)
            break
        }
    }
}
