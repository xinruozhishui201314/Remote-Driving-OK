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

    // ─────────────────────────────────────────────────────────────────
    // 就绪状态（禁止在绑定求值中写其它属性，否则触发 Binding loop）
    // ─────────────────────────────────────────────────────────────────

    readonly property bool isReady: authManager !== null && vehicleManager !== null
                                    && webrtcStreamManager !== null && mqttController !== null

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
        return mqtt !== null && mqtt.isConnected === true
    }

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

    function forceRefreshAllRenderers() {
        var wsm = webrtcStreamManager
        if (wsm && typeof wsm.forceRefreshAllRenderers === "function") {
            console.log("[AppContext] forceRefreshAllRenderers")
            wsm.forceRefreshAllRenderers()
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

    readonly property string logPrefix: "[AppContext]"
}
