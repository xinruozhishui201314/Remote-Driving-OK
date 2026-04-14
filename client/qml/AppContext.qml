pragma Singleton
import QtQuick 2.15

/**
 * 应用全局上下文单例（《客户端架构设计》§7 产品化增强）
 *
 * 统一管理所有 QML 全局对象的访问，提供：
 * - 安全对象获取：通过 rd_* 根上下文别名（C++ registerContextProperties 注入），单例内不可用 window[id]
 * - 统一入口：通过 AppContext.xxx 访问所有核心对象
 * - 就绪状态检查：isReady 属性指示核心服务是否就绪（纯绑定，无写副作用）
 * - 安全方法调用：safeCall() 防止空指针调用
 * - 远驾 UI 控制指令唯一入口：sendUiCommand(type, payload) → VehicleControlService（禁止 QML 直调 mqttController 发控）
 *
 * 使用方式：
 *   import RemoteDriving 1.0
 *   AppContext.authManager
 */
QtObject {
    id: root

    // ─────────────────────────────────────────────────────────────────
    // 对象引用（统一入口；rd_* 与根上下文 setContextProperty 一一对应，见 client_app_bootstrap.cpp）
    // ─────────────────────────────────────────────────────────────────

    readonly property var authManager: rd_authManager !== undefined ? rd_authManager : null
    readonly property var vehicleManager: rd_vehicleManager !== undefined ? rd_vehicleManager : null
    readonly property var webrtcClient: rd_webrtcClient !== undefined ? rd_webrtcClient : null
    readonly property var webrtcStreamManager: rd_webrtcStreamManager !== undefined ? rd_webrtcStreamManager : null
    readonly property var mqttController: rd_mqttController !== undefined ? rd_mqttController : null
    readonly property var vehicleStatus: rd_vehicleStatus !== undefined ? rd_vehicleStatus : null
    readonly property var vehicleControl: rd_vehicleControl !== undefined ? rd_vehicleControl : null
    readonly property var safetyMonitor: rd_safetyMonitor !== undefined ? rd_safetyMonitor : null
    readonly property var systemStateMachine: rd_systemStateMachine !== undefined ? rd_systemStateMachine : null
    readonly property var nodeHealthChecker: rd_nodeHealthChecker !== undefined ? rd_nodeHealthChecker : null
    /** 订阅 EventBus 解码/呈现完整性事件并向 QML 发横幅（见 main.qml videoIntegrityBanner） */
    readonly property var videoIntegrityBannerBridge: rd_videoIntegrityBannerBridge !== undefined
                                                      ? rd_videoIntegrityBannerBridge : null

    // ─────────────────────────────────────────────────────────────────
    // 就绪状态（禁止在绑定求值中写其它属性，否则触发 Binding loop）
    // ─────────────────────────────────────────────────────────────────

    readonly property bool isReady: authManager !== null && vehicleManager !== null
                                    && webrtcStreamManager !== null && mqttController !== null

    /** C++ 启动时 GL 探测：非软件光栅且未强制 LIBGL_ALWAYS_SOFTWARE 时为 true（见 rd_hardwarePresentationOk） */
    readonly property bool hardwarePresentationOk: (typeof rd_hardwarePresentationOk !== "undefined")
                                                   ? (rd_hardwarePresentationOk === true) : true

    /**
     * 是否使用系统窗口装饰（标题栏）。
     * - 显式：CLIENT_USE_WINDOW_FRAME=1 / CLIENT_DISABLE_FRAMELESS=1
     * - 自动：xcb + 检测到容器（/.dockerenv、cgroup、CLIENT_IN_CONTAINER=1）时默认启用，缓解 Docker+X11 无框窗客户区透出宿桌面
     * - 关闭自动：CLIENT_AUTO_WINDOW_FRAME_FOR_CONTAINER=0；强制无框：CLIENT_FORCE_FRAMELESS=1
     */
    readonly property bool useWindowFrame: (typeof rd_useWindowFrame !== "undefined")
                                           && (rd_useWindowFrame === true)

    /** C++ 窗口策略决策原因（与 [Client][WindowPolicy][Decision] reason 一致，便于 grep） */
    readonly property string windowFramePolicyReason: (typeof rd_windowFramePolicyReason !== "undefined")
                                                      ? String(rd_windowFramePolicyReason) : ""

    /**
     * 遥操作排障详细日志（键盘/焦点/控制环对齐）。调试阶段默认开启；
     * 一键关闭：CLIENT_TELEOP_TRACE=0（与 C++ 同源，见 client_app_bootstrap.cpp）。
     */
    readonly property bool teleopTraceEnabled: (typeof rd_teleopTraceEnabled !== "undefined")
                                             && (rd_teleopTraceEnabled === true)

    // ─────────────────────────────────────────────────────────────────
    // 便捷状态属性
    // ─────────────────────────────────────────────────────────────────

    readonly property bool isLoggedIn: {
        var am = authManager
        return am !== null && am.isLoggedIn === true
    }

    readonly property bool hasVehicle: {
        var vm = vehicleManager
        return vm !== null && vm.currentVin && vm.currentVin.length > 0
    }

    readonly property bool hasVideoStream: {
        var wsm = webrtcStreamManager
        return wsm !== null && wsm.isConnected === true
    }

    readonly property bool isMqttConnected: {
        var mqtt = mqttController
        return mqtt !== null && mqtt.mqttBrokerConnected === true
    }

    /** ConnectionsDialog 点「连接」后置位；main.qml 在 mqttBrokerConnectionChanged(true) 时发 start_stream 再清零 */
    property bool pendingRequestStreamAfterMqttConnect: false

    // ─────────────────────────────────────────────────────────────────
    // 视频连接状态便捷方法
    // ─────────────────────────────────────────────────────────────────

    readonly property bool videoStreamsConnected: {
        var wsm = webrtcStreamManager
        if (wsm !== null && wsm.anyConnected === true)
            return true
        var wsc = webrtcClient
        if (wsc !== null && wsc.isConnected === true)
            return true
        return false
    }

    // ─────────────────────────────────────────────────────────────────
    // 字体便捷属性
    // ─────────────────────────────────────────────────────────────────

    readonly property string chineseFont: {
        if (rd_applicationChineseFont !== undefined && rd_applicationChineseFont
                && String(rd_applicationChineseFont).length > 0) {
            return String(rd_applicationChineseFont)
        }
        var fonts = ["WenQuanYi Zen Hei", "WenQuanYi Micro Hei", "Noto Sans CJK SC"]
        var availableFonts = Qt.fontFamilies()
        for (var i = 0; i < fonts.length; i++) {
            if (availableFonts.indexOf(fonts[i]) !== -1) {
                return fonts[i]
            }
        }
        return ""
    }

    // ─────────────────────────────────────────────────────────────────
    // 远驾 UI 控制 — 单一门面（与 C++ VehicleControlService::sendUiCommand 对齐）
    // ─────────────────────────────────────────────────────────────────

    function sendUiCommand(type, payload) {
        var vc = vehicleControl
        if (vc !== null && typeof vc.sendUiCommand === "function") {
            vc.sendUiCommand(type, payload || ({}))
            return true
        }
        console.warn(logPrefix, "[Control] sendUiCommand dropped: vehicleControl unavailable type=", type)
        return false
    }

    // ─────────────────────────────────────────────────────────────────
    // 安全方法调用
    // ─────────────────────────────────────────────────────────────────

    function safeCall(obj, method, args) {
        if (!obj) {
            console.warn("[AppContext] safeCall: obj is null for method", method)
            return null
        }
        if (typeof obj[method] !== "function") {
            console.warn("[AppContext] safeCall: method", method, "not found on object")
            return null
        }
        try {
            if (args && Array.isArray(args)) {
                return obj[method].apply(obj, args)
            } else if (args !== undefined) {
                return obj[method](args)
            } else {
                return obj[method]()
            }
        } catch (e) {
            console.error("[AppContext] safeCall failed:", method, e)
            return null
        }
    }

    // ─────────────────────────────────────────────────────────────────
    // 统一刷新管理器
    // ─────────────────────────────────────────────────────────────────

    function forceRefreshAllRenderers(reason) {
        var wsm = webrtcStreamManager
        if (wsm && typeof wsm.forceRefreshAllRenderers === "function") {
            var r = (reason !== undefined && reason !== null && String(reason).length > 0) ? String(reason) : "unspecified"
            console.log("[AppContext] forceRefreshAllRenderers reason=" + r)
            wsm.forceRefreshAllRenderers(r)
            return true
        }
        return false
    }

    function getTotalPendingFrames() {
        var wsm = webrtcStreamManager
        if (wsm && typeof wsm.getTotalPendingFrames === "function") {
            return wsm.getTotalPendingFrames()
        }
        return -1
    }

    /** 与 C++ [Client][VideoFlickerClass][1Hz] 对齐：QML 检测到 everHad→false 等盖层风险时上报 */
    function reportVideoFlickerQmlLayerEvent(where, detail) {
        var wsm = webrtcStreamManager
        if (!wsm || typeof wsm.reportVideoFlickerQmlLayerEvent !== "function") {
            return
        }
        try {
            var w = (where !== undefined && where !== null) ? String(where) : ""
            var d = (detail !== undefined && detail !== null) ? String(detail) : ""
            wsm.reportVideoFlickerQmlLayerEvent(w, d)
        } catch (e) {
            console.warn("[AppContext] reportVideoFlickerQmlLayerEvent failed", e)
        }
    }

    readonly property string logPrefix: "[AppContext]"
}
