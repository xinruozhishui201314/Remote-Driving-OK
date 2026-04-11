import QtQuick 2.15

/**
 * 布局诊断日志与定时器（CLIENT_LAYOUT_DEBUG）；依赖 LayoutMetrics + shell。
 */
Item {
    id: root
    width: 0
    height: 0

    property Item host: null
    property Item shell: null
    property var metrics: null

    property bool isLayoutDebugEnabled: (typeof layoutDebugEnabled !== "undefined" && layoutDebugEnabled) || false

    function logLayout(reason) {
        if (!metrics || !host)
            return
        var diw = host.width ? Math.round(host.width) : 0
        var dih = host.height ? Math.round(host.height) : 0
        var mainH = metrics.mainRowRatio > 0 ? Math.round(host.height * metrics.mainRowRatio) : 0
        var rightPrefW = Math.round(host.width * metrics.rightColWidthRatio)
        console.log("[Client][UI][Layout] " + (reason || "onChange") + " drivingInterface=" + diw + "x" + dih + " mainRowH=" + mainH + " rightColPrefW=" + rightPrefW)
    }

    function logLayoutFull() {
        if (!metrics || !host)
            return
        var sh = shell
        if (!sh)
            return
        var rootColumnLayout = sh
        var topBarRect = sh.topBarRect
        var mainRowLayout = sh.mainRowLayout
        var leftColLayout = sh.leftColLayout
        var centerColLayout = sh.centerColLayout
        var rightColMeasurer = sh.rightColMeasurer
        var rightViewVideo = rightColMeasurer ? rightColMeasurer.rightViewVideo : null
        var hdMapRect = rightColMeasurer ? rightColMeasurer.hdMapRect : null
        var centerControlsRect = centerColLayout ? centerColLayout.centerControlsRect : null
        var centerDashboardRect = centerColLayout ? centerColLayout.centerDashboardRect : null
        var centerCameraRect = centerColLayout ? centerColLayout.centerCameraRect : null
        var leftFrontPanel = leftColLayout ? leftColLayout.leftFrontPanel : null
        var leftRearPanel = leftColLayout ? leftColLayout.leftRearPanel : null

        var diW = host.width ? Math.round(host.width) : 0
        var diH = host.height ? Math.round(host.height) : 0
        var topBarH = topBarRect ? Math.round(topBarRect.height) : 0
        var mainRowH = mainRowLayout ? Math.round(mainRowLayout.height) : 0
        var mainRowY = mainRowLayout ? Math.round(mainRowLayout.y) : 0
        var leftW = leftColLayout ? Math.round(leftColLayout.width) : 0
        var leftH = leftColLayout ? Math.round(leftColLayout.height) : 0
        var leftX = leftColLayout ? Math.round(leftColLayout.x) : 0
        var leftY = leftColLayout ? Math.round(leftColLayout.y) : 0
        var centerW = centerColLayout ? Math.round(centerColLayout.width) : 0
        var centerH = centerColLayout ? Math.round(centerColLayout.height) : 0
        var centerX = centerColLayout ? Math.round(centerColLayout.x) : 0
        var centerY = centerColLayout ? Math.round(centerColLayout.y) : 0
        var rightW = rightColMeasurer ? Math.round(rightColMeasurer.width) : 0
        var rightH = rightColMeasurer ? Math.round(rightColMeasurer.height) : 0
        var rightX = rightColMeasurer ? Math.round(rightColMeasurer.x) : 0
        var rightY = rightColMeasurer ? Math.round(rightColMeasurer.y) : 0
        var rightViewW = rightViewVideo ? Math.round(rightViewVideo.width) : 0
        var rightViewH = rightViewVideo ? Math.round(rightViewVideo.height) : 0
        var rightViewContH = rightViewH
        var rightViewRelY = rightViewVideo ? Math.round(rightViewVideo.y) : 0
        var hdMapW = hdMapRect ? Math.round(hdMapRect.width) : 0
        var hdMapH = hdMapRect ? Math.round(hdMapRect.height) : 0
        var hdMapContH = hdMapH
        var hdMapRelY = hdMapRect ? Math.round(hdMapRect.y) : 0
        var ctrlH = centerControlsRect ? Math.round(centerControlsRect.height) : 0
        var dashH = centerDashboardRect ? Math.round(centerDashboardRect.height) : 0
        var rootH = rootColumnLayout && rootColumnLayout.height ? Math.round(rootColumnLayout.height) : (host.height ? Math.round(host.height) : 0)
        var rightColImplicitH = rightColMeasurer ? (rightColMeasurer.implicitHeight || 0) : 0
        var rightColLayoutMinH = rightColMeasurer && rightColMeasurer.Layout ? (rightColMeasurer.Layout.minimumHeight || 0) : 0
        var mainRowMax = rootH > 0 ? Math.max(0, rootH - topBarH - rootColumnLayout.spacing) : 0
        var overflow = mainRowH - mainRowMax
        var leftFrontH = leftFrontPanel ? Math.round(leftFrontPanel.height) : 0
        var leftRearH = leftRearPanel ? Math.round(leftRearPanel.height) : 0
        var centerCamH = centerCameraRect ? Math.round(centerCameraRect.height) : 0
        var leftFrontBudget = Math.round(metrics.mainRowAvailH * metrics.leftVideoRatio)
        var centerCamBudget = Math.round(metrics.mainRowAvailH * metrics.centerCameraRatio)
        var ctrlBudget = Math.round(metrics.mainRowAvailH * metrics.centerControlsRatio)
        var dashBudget = Math.round(metrics.mainRowAvailH * metrics.centerDashboardRatio)
        console.log("[Client][UI][Layout] === 全布局尺寸 ===")
        console.log("[Client][UI][Layout] drivingInterface=" + diW + "x" + diH + " rootColH=" + rootH + " (componentsReady 影响父 visible)")
        console.log("[Client][UI][Layout] topBar=" + topBarH + " mainRow=" + mainRowH + " leftCol=" + leftW + "x" + leftH + " centerCol=" + centerW + "x" + centerH + " rightCol=" + rightW + "x" + rightH + " (rightH=0 则 Item 无 implicitHeight 导致)")
        console.log("[Client][UI][Layout] 垂直预算 mainRowAvailH=" + Math.round(metrics.mainRowAvailH) + " mainRowMax=" + mainRowMax + " overflow=" + overflow)
        console.log("[Client][UI][Layout] 位置: mainRowY=" + mainRowY + " leftCol(x=" + leftX + ",y=" + leftY + ") centerCol(x=" + centerX + ",y=" + centerY + ") rightCol(x=" + rightX + ",y=" + rightY + ")")
        console.log("[Client][UI][Layout] 左列拆分: 上(左视图)=" + leftFrontH + " (预算=" + leftFrontBudget + ") 下(后视图)=" + leftRearH)
        console.log("[Client][UI][Layout] 中列拆分: 摄像头区=" + centerCamH + " (预算=" + centerCamBudget + ") 控制区=" + ctrlH + " (预算=" + ctrlBudget + ") 仪表盘=" + dashH + " (预算=" + dashBudget + ")")
        if (rightH === 0)
            console.log("[Client][UI][Layout] [诊断] rightCol height=0: implicitH=" + rightColImplicitH + " Layout.minimumHeight=" + rightColLayoutMinH + " -> 需 Layout.minimumHeight/preferredHeight")
        console.log("[Client][UI][Layout] 右列拆分: 容器=" + rightH + " 右视图区=" + rightViewContH + " 高精地图区=" + hdMapContH + " 右视图=" + rightViewW + "x" + rightViewH + " 高精地图=" + hdMapW + "x" + hdMapH)
        console.log("[Client][UI][Layout] 右列位置: rightViewY(mainRow基准)=" + rightViewRelY + " hdMapY(mainRow基准)=" + hdMapRelY + " → 绝对Y: rightViewTop=" + (mainRowY + rightViewRelY) + " hdMapTop=" + (mainRowY + hdMapRelY))
        var mainRowW = mainRowLayout ? Math.round(mainRowLayout.width) : 0
        var mainRowChildren = mainRowLayout && mainRowLayout.children ? mainRowLayout.children.length : 0
        var mainRowType = mainRowLayout ? (mainRowLayout.columns !== undefined ? "GridLayout" : "RowLayout") : "null"
        var mainRowCols = mainRowLayout && mainRowLayout.columns !== undefined ? mainRowLayout.columns : -1
        var rightParentId = rightColMeasurer && rightColMeasurer.parent ? (rightColMeasurer.parent.id || "无id") : "null"
        var rightParentSame = rightColMeasurer && mainRowLayout && rightColMeasurer.parent === mainRowLayout
        var rightLayoutCol = rightColMeasurer && rightColMeasurer.Layout && rightColMeasurer.Layout.column !== undefined ? rightColMeasurer.Layout.column : -999
        var rightLayoutRow = rightColMeasurer && rightColMeasurer.Layout && rightColMeasurer.Layout.row !== undefined ? rightColMeasurer.Layout.row : -999
        var colGap = (mainRowLayout && mainRowLayout.columnSpacing !== undefined) ? mainRowLayout.columnSpacing : ((mainRowLayout && mainRowLayout.spacing !== undefined) ? mainRowLayout.spacing : 8)
        var sumWidth = leftW + colGap * 2 + centerW + rightW
        var leftRightDiff = Math.abs(leftW - rightW)
        var fillRatio = mainRowW > 0 ? (sumWidth / mainRowW * 100).toFixed(1) : 0
        console.log("[Client][UI][Layout] === 布局诊断 ===")
        console.log("[Client][UI][Layout] mainRowType=" + mainRowType + " columns=" + mainRowCols + " mainRowW=" + mainRowW + " children=" + mainRowChildren)
        console.log("[Client][UI][Layout] 宽度分配: mainRowAvailW=" + Math.round(metrics.mainRowAvailW) + " sideColAllocW=" + Math.round(metrics.sideColAllocW) + " centerColAllocW=" + Math.round(metrics.centerColAllocW))
        console.log("[Client][UI][Layout] 实际宽度: left=" + leftW + " center=" + centerW + " right=" + rightW + " sum=" + Math.round(sumWidth) + " 铺满率=" + fillRatio + "%")
        console.log("[Client][UI][Layout] 左右对称: left-right=" + leftRightDiff + " (差=0则对称)" + (leftRightDiff > 5 ? " [异常]左右列宽度不一致" : ""))
        console.log("[Client][UI][Layout] rightCol父节点: id=" + rightParentId + " 是否mainRowLayout=" + rightParentSame)
        console.log("[Client][UI][Layout] rightCol Layout.column=" + rightLayoutCol + " Layout.row=" + rightLayoutRow)
        if (mainRowLayout && mainRowLayout.children) {
            for (var i = 0; i < mainRowLayout.children.length; i++) {
                var c = mainRowLayout.children[i]
                var cid = c && c.id ? c.id : ("child" + i)
                var cx = c ? Math.round(c.x) : 0
                var cy = c ? Math.round(c.y) : 0
                if (c === rightColMeasurer)
                    console.log("[Client][UI][Layout] mainRowLayout.children[" + i + "]=" + cid + " (右列) x=" + cx + " y=" + cy)
                else if (i < 3)
                    console.log("[Client][UI][Layout] mainRowLayout.children[" + i + "]=" + cid + " x=" + cx + " y=" + cy)
            }
        }
        if (rightY !== 0)
            console.log("[Client][UI][Layout] [异常] rightCol.y=" + rightY + " 应=0，右列被排到第二行底部")
    }

    function startLayoutLogBurst() {
        layoutLogTimer.repeatCount = 0
        layoutLogTimer.start()
    }

    Timer {
        id: layoutLogTimer
        interval: 500
        repeat: true
        running: false
        property int repeatCount: 0
        onTriggered: {
            root.logLayoutFull()
            repeatCount++
            if (repeatCount >= 8)
                stop()
        }
    }
}
