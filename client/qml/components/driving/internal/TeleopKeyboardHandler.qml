import QtQuick 2.15

/**
 * 远驾键盘快捷键逻辑（由 DrivingInterface.Keys.onPressed 调用，保持根 Item focus）。
 * 安全死手经 facade.appServices.safetyMonitor（DrivingFacade v3）。
 */
Item {
    id: root
    width: 0
    height: 0

    readonly property bool _trace: (typeof rd_teleopTraceEnabled !== "undefined")
                                   && (rd_teleopTraceEnabled === true)

    property Item facade: null
    /** TeleopPresentationState */
    property var teleop: null

    /** 与 DrivingCenterColumn / DrivingTopChrome 一致：仅车端确认远驾后可发控（空格急停仍始终发送） */
    function _remoteTeleopOk() {
        var vs = (facade && facade.appServices) ? facade.appServices.vehicleStatus : null
        if (!vs)
            return false
        if (vs.remoteControlEnabled === true)
            return true
        return (vs.drivingMode === "远驾")
    }

    property int _teleopBlockedLogCount: 0

    function _logTeleopBlocked(hint) {
        if (++_teleopBlockedLogCount === 1 || _teleopBlockedLogCount % 120 === 0)
            console.warn("[Client][UI][Teleop] " + hint + "（已节流） count=" + _teleopBlockedLogCount)
    }

    function _syncKeyboardLatchFlags() {
        if (!teleop)
            return
        teleop.keyboardSpeedAdjustActive = (_keyUp || _keyDown)
        teleop.keyboardSteerActive = (_keyLeft || _keyRight)
    }

    function _logKey(tag, keyName) {
        if (!root._trace)
            return
        console.log("[Client][UI][Teleop][KEY] " + tag + " key=" + keyName
                    + " remoteOk=" + (_remoteTeleopOk() ? "1" : "0")
                    + " mqtt=" + ((facade && facade.appServices && facade.appServices.mqttController
                                  && facade.appServices.mqttController.mqttBrokerConnected) ? "1" : "0"))
    }

    property int _unhandledKeyLogCount: 0
    function _logUnhandledKey(event) {
        if (!root._trace || event.isAutoRepeat)
            return
        if (++_unhandledKeyLogCount === 1 || _unhandledKeyLogCount % 30 === 0)
            console.log("[Client][UI][Teleop][KEY][unhandled] key=" + event.key
                        + " text=" + (event.text ? JSON.stringify(event.text) : "")
                        + "（WASD:W/S 车速 A/← 左转 → 右转；D=前进档非右转；已节流 count=" + _unhandledKeyLogCount + "）")
    }

    // ── 游戏模式按键状态 ──
    property bool _keyUp:    false
    property bool _keyDown:  false
    property bool _keyLeft:  false
    property bool _keyRight: false
    property bool _keySpace: false

    // ── 配置参数 ──
    // 约定 (@CLIENT_UI_OPERATION_GUIDE.md)：
    // 1. 速度按一次(tick)步长 0.1m/s = 0.36km/h；50Hz 下为 18.0 km/h/s
    // 2. 转向角度按一次(tick)步长 2度；50Hz 下为 (2/450)*50 = 0.2222 normalized/s
    readonly property real _steerSpeed: 0.2222 // 转向速度 (2度/tick @ 50Hz)
    readonly property real _steerReturn: 0.6   // 回正速度 (约 3倍转向速度，更符合直觉)
    readonly property real _speedStep: 18.0    // 加减速步长 (0.1m/s / tick @ 50Hz)

    function handlePressed(event) {
        if (!teleop || event.isAutoRepeat)
            return
        
        var sm = (facade && facade.appServices) ? facade.appServices.safetyMonitor : null
        if (sm && typeof sm.notifyOperatorActivity === "function")
            sm.notifyOperatorActivity()

        var handled = false
        switch (event.key) {
        // 档位控制 (点按)
        case Qt.Key_P:
        case Qt.Key_N:
        case Qt.Key_R:
        case Qt.Key_D:
            handled = true
            if (!_remoteTeleopOk()) {
                _logTeleopBlocked("远驾未接管：忽略档位键（请先点「远驾接管」并等车端确认）")
                break
            }
            if (event.key === Qt.Key_P) teleop.currentGear = "P"
            else if (event.key === Qt.Key_N) teleop.currentGear = "N"
            else if (event.key === Qt.Key_R) teleop.currentGear = "R"
            else teleop.currentGear = "D"
            _logKey("DOWN", "GEAR")
            break

        // 转向灯 (Q/E) — 与面板一致走 sendControlCommand("light", …)
        case Qt.Key_Q:
            handled = true
            if (!_remoteTeleopOk()) {
                _logTeleopBlocked("远驾未接管：忽略转向灯键")
                break
            }
            teleop.leftTurnActive = !teleop.leftTurnActive
            if (teleop.rightTurnActive) teleop.rightTurnActive = false
            if (facade && typeof facade.sendControlCommand === "function")
                facade.sendControlCommand("light", { name: "leftTurn", active: teleop.leftTurnActive })
            _logKey("DOWN", "Q_LEFT_TURN active=" + teleop.leftTurnActive)
            break
        case Qt.Key_E:
            handled = true
            if (!_remoteTeleopOk()) {
                _logTeleopBlocked("远驾未接管：忽略转向灯键")
                break
            }
            teleop.rightTurnActive = !teleop.rightTurnActive
            if (teleop.leftTurnActive) teleop.leftTurnActive = false
            if (facade && typeof facade.sendControlCommand === "function")
                facade.sendControlCommand("light", { name: "rightTurn", active: teleop.rightTurnActive })
            _logKey("DOWN", "E_RIGHT_TURN active=" + teleop.rightTurnActive)
            break

        // 急停 (空格)
        case Qt.Key_Space:
            handled = true
            _keySpace = true
            _logKey("DOWN", "SPACE_ESTOP")
            // 约定：按下急停后，立即将 UI 层的目标速度清零，确保恢复后不会突然冲出
            if (teleop) teleop.targetSpeed = 0.0
            
            var vc = (facade && facade.appServices) ? facade.appServices.vehicleControl : null
            if (vc) {
                // 同步发送速度清零指令，确保车端状态同步
                vc.sendUiCommand("speed", { value: 0.0 })
                vc.requestEmergencyStop()
            }
            break

        // 方向控制 (WASD + 方向键)
        case Qt.Key_W:
        case Qt.Key_Up:
            handled = true
            if (!_remoteTeleopOk()) {
                _logTeleopBlocked("远驾未接管：忽略加速键")
                break
            }
            _keyUp = true
            _logKey("DOWN", "W_ACCEL")
            break
        case Qt.Key_S:
        case Qt.Key_Down:
            handled = true
            if (!_remoteTeleopOk()) {
                _logTeleopBlocked("远驾未接管：忽略减速键")
                break
            }
            _keyDown = true
            _logKey("DOWN", "S_BRAKE")
            break
        case Qt.Key_A:
        case Qt.Key_Left:
            handled = true
            if (!_remoteTeleopOk()) {
                _logTeleopBlocked("远驾未接管：忽略转向键")
                break
            }
            _keyLeft = true
            _logKey("DOWN", "A_LEFT")
            break
        // 注意：Qt.Key_D 仅用于前进档（见上），右转只用方向键 →
        case Qt.Key_Right:
            handled = true
            if (!_remoteTeleopOk()) {
                _logTeleopBlocked("远驾未接管：忽略转向键")
                break
            }
            _keyRight = true
            _logKey("DOWN", "ARROW_RIGHT")
            break
        }

        if (!handled)
            _logUnhandledKey(event)

        _syncKeyboardLatchFlags()
        if (_keyUp || _keyDown || _keyLeft || _keyRight) {
            if (!gameControlTimer.running) {
                if (root._trace)
                    console.log("[Client][UI][Teleop][TIMER] gameControlTimer start keys=W"
                                + (_keyUp ? "1" : "0") + "S" + (_keyDown ? "1" : "0")
                                + "A" + (_keyLeft ? "1" : "0") + "→" + (_keyRight ? "1" : "0"))
                gameControlTimer.start()
            }
        }
    }

    function handleReleased(event) {
        if (!teleop || event.isAutoRepeat)
            return

        switch (event.key) {
        case Qt.Key_W:
        case Qt.Key_Up:    _keyUp = false;    break
        case Qt.Key_S:
        case Qt.Key_Down:  _keyDown = false;  break
        case Qt.Key_A:
        case Qt.Key_Left:  _keyLeft = false;  break
        case Qt.Key_Right: _keyRight = false; break
        case Qt.Key_Space: _keySpace = false; break
        }
        _syncKeyboardLatchFlags()
    }

    // ── 游戏控制循环 (50Hz) ──
    Timer {
        id: gameControlTimer
        interval: 20
        repeat: true
        running: false
        property int telemetryTick: 0
        onTriggered: {
            if (!root._remoteTeleopOk()) {
                root._logTeleopBlocked("远驾已断开：停止键盘控车循环")
                stop()
                return
            }
            root._syncKeyboardLatchFlags()
            gameControlTimer.telemetryTick++
            var dt = interval / 1000.0
            var changed = false

            // 1. 处理目标车速 (W/S)
            if (_keyUp) {
                teleop.targetSpeed = Math.min(100, teleop.targetSpeed + _speedStep * dt)
                changed = true
            } else if (_keyDown) {
                teleop.targetSpeed = Math.max(0, teleop.targetSpeed - _speedStep * dt)
                changed = true
            }

            // 2. 处理转向 (A/D)
            var currentSteer = teleop.steeringAngle / 100.0 // 转回 [-1, 1]
            if (_keyLeft) {
                currentSteer = Math.max(-1.0, currentSteer - _steerSpeed * dt)
                changed = true
            } else if (_keyRight) {
                currentSteer = Math.min(1.0, currentSteer + _steerSpeed * dt)
                changed = true
            } else {
                // 自动回正
                if (Math.abs(currentSteer) < (_steerReturn * dt)) {
                    if (currentSteer !== 0) {
                        currentSteer = 0
                        changed = true
                    }
                } else {
                    currentSteer += (currentSteer > 0 ? -_steerReturn : _steerReturn) * dt
                    changed = true
                }
            }

            if (changed) {
                teleop.steeringAngle = currentSteer * 100.0

                // 发送控制指令 (模拟滑块操作)
                var vc = (facade && facade.appServices) ? facade.appServices.vehicleControl : null
                if (vc) {
                    // 同步发送：转向 (normalized), 目标速度指令
                    // 注意：UI 滑块通常发送 steering, throttle, brake。
                    // 这里我们通过 sendDriveCommand 发送 steering，通过 sendUiCommand("speed") 发送速度。
                    // 约定：减至 0 后触发物理刹车 (给 0.5 刹车量以确保停止并防止溜坡)
                    var physicalBrake = (teleop.targetSpeed <= 0.01) ? 0.5 : 0.0
                    vc.sendDriveCommand(currentSteer, 0, physicalBrake) // 这里不给手动油门，依靠 Bridge 的速度 PID
                    if (_keyUp || _keyDown) {
                        vc.sendUiCommand("speed", { value: teleop.targetSpeed })
                    }
                    if (root._trace && gameControlTimer.telemetryTick % 25 === 0) {
                        var rep = (facade.appServices && facade.appServices.vehicleStatus)
                                ? facade.appServices.vehicleStatus.speed : -1
                        console.log("[Client][UI][Teleop][TICK] targetSpd=" + teleop.targetSpeed.toFixed(1)
                                    + " reportedSpd=" + rep
                                    + " steerNorm=" + currentSteer.toFixed(3)
                                    + " keys=W" + (_keyUp ? "1" : "0") + "S" + (_keyDown ? "1" : "0")
                                    + "A" + (_keyLeft ? "1" : "0") + "→" + (_keyRight ? "1" : "0"))
                    }
                } else if (root._trace && changed && gameControlTimer.telemetryTick % 50 === 0) {
                    console.warn("[Client][UI][Teleop][TICK] changed but vehicleControl=null（无法 sendDriveCommand）")
                }
            }

            // 如果所有按键都松开且转向已归零，停止定时器
            if (!_keyUp && !_keyDown && !_keyLeft && !_keyRight && Math.abs(currentSteer) < 0.001) {
                if (root._trace && gameControlTimer.running)
                    console.log("[Client][UI][Teleop][TIMER] gameControlTimer stop (keys released, steer ~0)")
                gameControlTimer.stop()
                root._syncKeyboardLatchFlags()
            }
        }
    }
}
