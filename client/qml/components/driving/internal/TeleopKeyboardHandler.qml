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
                        + "（WASD:W/S 车速 A/D 转向；1-4: P/R/N/D 档；已节流 count=" + _unhandledKeyLogCount + "）")
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
        case Qt.Key_1:
        case Qt.Key_2:
        case Qt.Key_3:
        case Qt.Key_4:
            handled = true
            if (!_remoteTeleopOk()) {
                _logTeleopBlocked("远驾未接管：忽略档位键（请先点「远驾接管」并等车端确认）")
                break
            }
            if (event.key === Qt.Key_1) {
                teleop.currentGear = "P"
                _logKey("DOWN", "GEAR_P")
            } else if (event.key === Qt.Key_2) {
                teleop.currentGear = "R"
                _logKey("DOWN", "GEAR_R")
            } else if (event.key === Qt.Key_3) {
                teleop.currentGear = "N"
                _logKey("DOWN", "GEAR_N")
            } else {
                teleop.currentGear = "D"
                _logKey("DOWN", "GEAR_D")
            }
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
            if (!_keyUp) {
                _keyUp = true
                _logKey("DOWN", event.key === Qt.Key_W ? "W_ACCEL" : "ARROW_UP_ACCEL")
                
                // [Fix] 立即增加一个步长，确保“点按”也能有效增加速度
                teleop.targetSpeed = Math.min(100, teleop.targetSpeed + (_speedStep * 0.02))
            }
            break
        case Qt.Key_S:
        case Qt.Key_Down:
            handled = true
            if (!_remoteTeleopOk()) {
                _logTeleopBlocked("远驾未接管：忽略减速键")
                break
            }
            if (!_keyDown) {
                _keyDown = true
                _logKey("DOWN", event.key === Qt.Key_S ? "S_BRAKE" : "ARROW_DOWN_BRAKE")
                
                // [Fix] 立即减少一个步长，确保“点按”也能有效减少速度
                teleop.targetSpeed = Math.max(0, teleop.targetSpeed - (_speedStep * 0.02))
            }
            break
        case Qt.Key_A:
        case Qt.Key_Left:
            handled = true
            if (!_remoteTeleopOk()) {
                _logTeleopBlocked("远驾未接管：忽略转向键")
                break
            }
            if (!_keyLeft) {
                _keyLeft = true
                _logKey("DOWN", event.key === Qt.Key_A ? "A_LEFT" : "ARROW_LEFT")
            }
            break
        case Qt.Key_D:
        case Qt.Key_Right:
            handled = true
            if (!_remoteTeleopOk()) {
                _logTeleopBlocked("远驾未接管：忽略转向键")
                break
            }
            if (!_keyRight) {
                _keyRight = true
                _logKey("DOWN", event.key === Qt.Key_D ? "D_RIGHT" : "ARROW_RIGHT")
            }
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
        return handled
    }

    function handleReleased(event) {
        if (!teleop || event.isAutoRepeat)
            return false

        var handled = false
        switch (event.key) {
        case Qt.Key_W:
        case Qt.Key_Up:    _keyUp = false;    handled = true; _logKey("UP", "ACCEL_RELEASE"); break
        case Qt.Key_S:
        case Qt.Key_Down:  _keyDown = false;  handled = true; _logKey("UP", "BRAKE_RELEASE"); break
        case Qt.Key_A:
        case Qt.Key_Left:  _keyLeft = false;  handled = true; _logKey("UP", "LEFT_RELEASE"); break
        case Qt.Key_Right: _keyRight = false; handled = true; _logKey("UP", "RIGHT_RELEASE"); break
        case Qt.Key_Space: _keySpace = false; handled = true; _logKey("UP", "SPACE_RELEASE"); break
        case Qt.Key_P:
        case Qt.Key_N:
        case Qt.Key_R:
        case Qt.Key_D:
        case Qt.Key_Q:
        case Qt.Key_E:     handled = true; break
        }
        _syncKeyboardLatchFlags()
        return handled
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
            
            // ★ 核心修复：定时上报操作员活跃度，解决长按按键时由于 isAutoRepeat 过滤导致的 Deadman 超时，
            // 且与控制环 pingSafety 脱钩，确保 watchdog uiActive 逻辑正确。
            var sm = (facade && facade.appServices) ? facade.appServices.safetyMonitor : null
            if (sm && typeof sm.notifyOperatorActivity === "function") {
                if (gameControlTimer.telemetryTick % 25 === 0) // 500ms 一次活跃度上报
                    sm.notifyOperatorActivity()
            }

            var dt = interval / 1000.0
            var changed = false

            // 1. 处理目标车速 (W/S)
            var oldSpd = teleop.targetSpeed
            // [Fix] 即使按键已松开，如果目标速度仍 > 0.1，也要维持控制循环以发送维持指令
            if (_keyUp) {
                teleop.targetSpeed = Math.min(100, teleop.targetSpeed + _speedStep * dt)
                changed = true
            } else if (_keyDown) {
                teleop.targetSpeed = Math.max(0, teleop.targetSpeed - _speedStep * dt)
                changed = true
            }

            // 2. 处理转向 (A/D)
            var currentSteer = teleop.steeringAngle / 100.0 // 转回 [-1, 1]
            var oldSteer = currentSteer
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

            if (root._trace && (gameControlTimer.telemetryTick % 10 === 0)) {
                if (changed || Math.abs(currentSteer) > 0.001 || teleop.targetSpeed > 0.1) {
                     console.log("[Client][UI][Teleop][CALC] tick=" + gameControlTimer.telemetryTick 
                                 + " steer=" + oldSteer.toFixed(3) + "->" + currentSteer.toFixed(3)
                                 + " speed=" + oldSpd.toFixed(1) + "->" + teleop.targetSpeed.toFixed(1)
                                 + " keys=W" + (_keyUp?"1":"0") + "S" + (_keyDown?"1":"0") 
                                 + "A" + (_keyLeft?"1":"0") + "R" + (_keyRight?"1":"0"))
                }
            }

            // [SystemicFix] 发送逻辑：
            // 只要在运行，就更新 steeringAngle（确保自动回正生效）
            teleop.steeringAngle = currentSteer * 100.0

            var vc = (facade && facade.appServices) ? facade.appServices.vehicleControl : null
            if (vc) {
                // [Crucial] 即使 changed 为 false，只要目标速 > 0，也要确保 sendDriveCommand 被调用
                // 以便 C++ Worker 维持最新的 InputState (尤其是 physicalBrake 状态)
                // 约定：减至 0.1 以下后触发物理刹车 (给 0.5 刹车量以确保停止并防止溜坡)，与 Bridge PID 阈值对齐
                var physicalBrake = (teleop.targetSpeed < 0.1) ? 0.5 : 0.0
                
                // 策略：有变化立即发，无变化则每 100ms (5 ticks) 重发一次确保状态一致性
                if (changed || gameControlTimer.telemetryTick % 5 === 0) {
                    if (root._trace && !changed) {
                        console.log("[Client][UI][Teleop][SYNC] 周期性同步 drive 指令: spd=" + teleop.targetSpeed.toFixed(1))
                    }
                    vc.sendDriveCommand(currentSteer, 0, physicalBrake, teleop.targetSpeed)
                }
                
                // 目标速度同步：按键按下时高频发，松开后每 500ms (25 ticks) 发一次心跳
                if (_keyUp || _keyDown || (gameControlTimer.telemetryTick % 25 === 0 && teleop.targetSpeed > 0.1)) {
                    if (root._trace && !_keyUp && !_keyDown) {
                        console.log("[Client][UI][Teleop][SYNC] 周期性同步 speed 指令: target=" + teleop.targetSpeed.toFixed(1))
                    }
                    vc.sendUiCommand("speed", { value: teleop.targetSpeed })
                }

                // ★ 增加空档误操作提醒
                if (teleop.currentGear === "N" && (teleop.targetSpeed > 0.1 || Math.abs(currentSteer) > 0.1)) {
                    if (gameControlTimer.telemetryTick % 100 === 1) {
                        console.warn("[Client][UI][Teleop] ⚠ 当前处于空档(N)，车辆无法行驶。按 D 键切换至前进档。")
                    }
                }
            }

            // 如果所有按键都松开、转向已归零、且目标速度已归零，停止定时器
            if (!_keyUp && !_keyDown && !_keyLeft && !_keyRight 
                && Math.abs(currentSteer) < 0.001 
                && teleop.targetSpeed < 0.1) {
                
                if (root._trace && gameControlTimer.running)
                    console.log("[Client][UI][Teleop][TIMER] gameControlTimer stop (keys released, steer ~0, speed ~0)")
                gameControlTimer.stop()
                root._syncKeyboardLatchFlags()
            }
        }
    }
}
