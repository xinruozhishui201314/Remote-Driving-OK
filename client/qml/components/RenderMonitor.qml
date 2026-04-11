import ".."
import QtQuick 2.15
import RemoteDriving 1.0

/**
 * 渲染监控组件（《客户端架构设计》§7 产品化增强）
 *
 * 功能：
 * - 监控渲染健康状态
 * - 检测待处理帧积压
 * - 自动尝试恢复渲染
 * - 提供渲染状态报告
 *
 * 使用方式（在 main.qml 中）：
 *   RenderMonitor {
 *       id: renderMonitor
 *       checkInterval: 1000
 *       maxPendingFrames: 20
 *   }
 */
Item {
    id: root

    // ─────────────────────────────────────────────────────────────────
    // 配置属性
    // ─────────────────────────────────────────────────────────────────

    /** 检查间隔（毫秒） */
    property int checkInterval: 1000

    /** 最大允许的待处理帧数 */
    property int maxPendingFrames: 20

    /** 自动恢复启用 */
    property bool autoRecover: true

    /** 连续恢复失败计数 */
    property int recoveryFailCount: 0

    /** 最大连续恢复失败次数 */
    property int maxRecoveryFails: 3

    // ─────────────────────────────────────────────────────────────────
    // 只读状态属性
    // ─────────────────────────────────────────────────────────────────

    /** 是否健康（无积压） */
    readonly property bool isHealthy: _pendingFrames <= maxPendingFrames

    /** 当前待处理帧数 */
    readonly property int pendingFrames: _pendingFrames

    /** 上次检查时间戳 */
    readonly property int lastCheckTime: _lastCheckTime

    /** 是否正在恢复 */
    property bool isRecovering: false

    /** 恢复尝试次数 */
    property int recoveryAttempts: 0

    // ─────────────────────────────────────────────────────────────────
    // 内部状态
    // ─────────────────────────────────────────────────────────────────

    property int _pendingFrames: 0
    property int _lastCheckTime: 0
    property int _consecutiveUnhealthyCount: 0

    // ─────────────────────────────────────────────────────────────────
    // 健康检查
    // ─────────────────────────────────────────────────────────────────

    /**
     * 执行健康检查
     * @return true 如果健康
     */
    function checkHealth() {
        _lastCheckTime = Date.now()
        _pendingFrames = _getPendingFrames()
        
        if (_pendingFrames > maxPendingFrames) {
            _consecutiveUnhealthyCount++
            console.warn("[RenderMonitor] Unhealthy: pendingFrames=" + _pendingFrames 
                       + " max=" + maxPendingFrames 
                       + " consecutiveCount=" + _consecutiveUnhealthyCount)
            return false
        }
        
        if (_consecutiveUnhealthyCount > 0) {
            console.log("[RenderMonitor] Recovered: pendingFrames=" + _pendingFrames)
            _consecutiveUnhealthyCount = 0
        }
        
        return true
    }

    /**
     * 获取总待处理帧数
     */
    function _getPendingFrames() {
        return AppContext.getTotalPendingFrames()
    }

    // ─────────────────────────────────────────────────────────────────
    // 恢复逻辑
    // ─────────────────────────────────────────────────────────────────

    /**
     * 尝试恢复渲染
     */
    function tryRecover() {
        if (isRecovering) {
            console.log("[RenderMonitor] Already recovering, skipping")
            return false
        }

        isRecovering = true
        recoveryAttempts++
        
        var pending = _getPendingFrames()
        console.log("[RenderMonitor] Attempting recovery, attempt=" + recoveryAttempts
                    + " totalPendingVideoHandlers=" + pending
                    + " ★ >0 表示主线程视频处理堆积")

        var success = AppContext.forceRefreshAllRenderers("RenderMonitor.tryRecover")

        if (success) {
            recoveryFailCount = 0
            console.log("[RenderMonitor] Recovery successful")
        } else {
            recoveryFailCount++
            console.warn("[RenderMonitor] Recovery failed, failCount=" + recoveryFailCount)
            
            if (recoveryFailCount >= maxRecoveryFails) {
                console.error("[RenderMonitor] Max recovery fails reached, giving up")
                _emitCriticalSignal()
            }
        }

        isRecovering = false
        return success
    }

    /**
     * 重置状态
     */
    function reset() {
        _pendingFrames = 0
        _consecutiveUnhealthyCount = 0
        recoveryFailCount = 0
        recoveryAttempts = 0
        isRecovering = false
        console.log("[RenderMonitor] Reset")
    }

    // ─────────────────────────────────────────────────────────────────
    // 信号
    // ─────────────────────────────────────────────────────────────────

    signal unhealthyDetected(int pendingFrames, int maxFrames)
    signal recovered()
    signal criticalFailure()

    function _emitCriticalSignal() {
        criticalFailure()
    }

    // ─────────────────────────────────────────────────────────────────
    // 定时检查
    // ─────────────────────────────────────────────────────────────────

    Timer {
        id: healthCheckTimer
        interval: root.checkInterval
        repeat: true
        running: true
        onTriggered: {
            var wasHealthy = root.isHealthy
            checkHealth()
            
            if (!root.isHealthy) {
                unhealthyDetected(_pendingFrames, maxPendingFrames)
                
                if (autoRecover && _consecutiveUnhealthyCount >= 2) {
                    tryRecover()
                }
            } else if (!wasHealthy) {
                recovered()
            }
        }
    }

    // ─────────────────────────────────────────────────────────────────
    // 状态报告
    // ─────────────────────────────────────────────────────────────────

    /**
     * 获取状态报告
     * @return 状态报告字符串
     */
    function getStatusReport() {
        return JSON.stringify({
            isHealthy: isHealthy,
            pendingFrames: _pendingFrames,
            maxPendingFrames: maxPendingFrames,
            isRecovering: isRecovering,
            recoveryAttempts: recoveryAttempts,
            recoveryFailCount: recoveryFailCount,
            consecutiveUnhealthyCount: _consecutiveUnhealthyCount,
            lastCheckTime: _lastCheckTime
        }, null, 2)
    }

    Component.onCompleted: {
        console.log("[RenderMonitor] Initialized, checkInterval=" + checkInterval 
                  + "ms, maxPendingFrames=" + maxPendingFrames)
    }
}
