#pragma once
#include <QQuickItem>
#include <QImage>
#include <QSGNode>
#include <QTimer>
#include <QMutex>
#include <QWaitCondition>
#include <QThread>
#include <memory>
#include <atomic>
#include "../../infrastructure/media/IHardwareDecoder.h"
#include "../../infrastructure/media/gpu/IGpuInterop.h"
#include "../../utils/TripleBuffer.h"
#include "../../utils/TimeUtils.h"

class VideoSGNode;

/**
 * GPU 加速视频渲染器（《客户端架构设计》§3.4.1 完整实现）。
 *
 * 零拷贝渲染路径：
 *   MediaPipeline(解码线程) → deliverFrame(原子三缓冲) → updatePaintNode(渲染线程)
 *   → VideoSGNode → QSGTexture → GPU 合成
 *
 * 遵循 Qt Scene Graph 线程模型：
 *   - deliverFrame() 可在任意线程调用（原子操作）
 *   - updatePaintNode() 只在 Qt Render Thread 调用（与 GUI 线程同步）
 *
 * ═══════════════════════════════════════════════════════════════════════════════
 * ★★★ 根因分析与修复方案（方案 C：Qt 6 scheduleRenderJob 渲染线程刷新）★★★
 *
 * 问题现象：四个视频视图都不显示，pendingFrames 累积到 1400+，updatePaintNode 不被调用
 *
 * 根因分析（基于日志证据）：
 * 1. deliverFrame 持续被调用（totalDeliver 增长到 1900+）
 * 2. window() 返回有效指针，不是 nullptr
 * 3. updatePaintNode 只在初始化时被调用 4 次（4 个 VideoRenderer 各一次）
 * 4. isVisible=false 在初始化时出现
 * 5. 三缓冲交换日志从未出现（说明 updatePaintNode 从未收到帧）
 *
 * 直接原因：Qt Scene Graph 在 VehicleSelectionDialog modal=true
 * 显示期间完全阻塞主事件循环，导致 window()->update() 投递的
 * QEvent::UpdateRequest 堆积在主线程队列中，无法被 Scene Graph 处理。
 * forceRefresh() 在 onClosed 后调用，已太晚。
 *
 * 修复方案 C（Qt 6 兼容）：
 * 1. QQuickWindow::update() 只向主线程事件队列投递 UpdateRequest
 *    → 主线程被模态对话框阻塞时完全失效
 * 2. Qt 6 正确做法：使用 QQuickWindow::scheduleRenderJob() 向渲染线程投递任务
 *    QQuickWindow::scheduleRenderJob(RenderRefreshRunnable, BeforeSynchronizingStage)
 *    → 投递到渲染线程事件队列 → 渲染线程有独立事件循环
 *    → modal 对话框阻塞主线程时仍能响应
 * 3. 配合 modality: Qt.WindowModal 减少主线程阻塞时间
 * 4. 对话框打开期间持续调用（非只在关闭时）
 * 5. 主动轮询定时器：dialog 打开期间持续刷新
 * ═══════════════════════════════════════════════════════════════════════════════
 */
class VideoRenderer : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(int cameraId READ cameraId WRITE setCameraId NOTIFY cameraIdChanged)
    Q_PROPERTY(bool mirrorHorizontal READ mirrorHorizontal WRITE setMirrorHorizontal NOTIFY mirrorChanged)
    Q_PROPERTY(double fps READ fps NOTIFY fpsChanged)
    Q_PROPERTY(double latencyMs READ latencyMs NOTIFY latencyChanged)
    QML_ELEMENT

public:
    explicit VideoRenderer(QQuickItem* parent = nullptr);
    ~VideoRenderer() override;

    int cameraId() const         { return m_cameraId; }
    bool mirrorHorizontal() const { return m_mirrorH; }
    double fps() const            { return m_fps.load(); }
    double latencyMs() const      { return m_latencyMs.load(); }

    void setCameraId(int id)         { m_cameraId = id; emit cameraIdChanged(); }
    void setMirrorHorizontal(bool m) { m_mirrorH = m; emit mirrorChanged(); update(); }

    // MediaPipeline 信号连接（可在任意线程调用）
    /** @param frameId  端到端帧序列号（emit→QML→setFrame→deliverFrame→updatePaintNode 追踪）
     *  @param lifecycleId v3 新增：从 RTP 包到达时分配的端到端追踪 ID，用于链路诊断 */
    Q_INVOKABLE void deliverFrame(std::shared_ptr<VideoFrame> frame, quint64 frameId = 0, quint64 lifecycleId = 0);

    /** QML / WebRtcClient::videoFrameReady(QImage)：RGB→YUV420P 后走 deliverFrame（与现有 YUV 着色器一致）。
     *  @param frameId  从 C++ WebRtcClient::emit 传入的帧序列号，用于端到端追踪。
     *  @param lifecycleId v3 新增：从 RTP 包到达时分配的端到端追踪 ID，用于链路诊断。
     *                  QML 端已打印 frameId，可与 C++ 日志对比确认 emit→setFrame→updatePaintNode 不断链。 */
    Q_INVOKABLE void setFrame(const QImage& image, quint64 frameId = 0, quint64 lifecycleId = 0);

signals:
    void cameraIdChanged();
    void mirrorChanged();
    void fpsChanged(double fps);
    void latencyChanged(double latencyMs);
    /** 渲染线程心跳信号：当 deliverFrame 被调用但渲染线程超过阈值未响应时发射 */
    void renderThreadStalled(int pendingFrames, int64_t stalledMs);
    /** 渲染线程直接刷新信号：发送给渲染线程以绕过主线程事件循环 */
    void requestRenderThreadRefresh();

public:
    // ── 方案 C：Qt 6 scheduleRenderJob 渲染线程刷新 ───────────────────────────
    /** Q_INVOKABLE：供 QML 在 VehicleSelectionDialog 打开/关闭时调用，强制触发渲染。
     *  核心修复：使用 QQuickWindow::scheduleRenderJob() 向渲染线程投递刷新任务，
     *  绕过主线程事件循环（modal 对话框阻塞时 window()->update() 完全失效）。
     *  Qt 6 兼容方案（polishAndUpdate() 不是公开 API）：
     *    1. triggerRenderRefresh() → scheduleRenderJob() → 渲染线程事件队列
     *    2. renderThreadRefreshImpl() → scheduleRenderJob() → 渲染线程事件队列
     *    → 渲染线程有独立事件循环 → modal 对话框阻塞主线程时仍能响应 */
    Q_INVOKABLE void forceRefresh();

    /** 诊断：返回当前帧累积状态，供调试使用 */
    Q_INVOKABLE int pendingFrameCount() const { return m_pendingFramesCount.load(); }
    /** 诊断：返回上次 updatePaintNode 距今的毫秒数 */
    Q_INVOKABLE int64_t msSinceLastPaint() const;

    // ── 主动轮询定时器（防止 Scene Graph 调度器跳过）────────────────────
    /** 启动轮询定时器（componentComplete 时调用）*/
    void startPollingTimer();
    /** 停止轮询定时器（析构时调用）*/
    void stopPollingTimer();

protected:
    QSGNode* updatePaintNode(QSGNode* old, UpdatePaintNodeData* data) override;
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;
    void componentComplete() override;
    void itemChange(ItemChange change, const ItemChangeData & value) override;

private:
    // ── 方案 C 核心：向渲染线程直接发送刷新 ───────────────────────────────
    /** 在渲染线程上下文中执行实际的节点更新。
     *  通过 QMetaMethod::invoke + Qt::QueuedConnection 从任意线程调用，
     *  投递到渲染线程事件队列，绕过主线程事件循环。 */
    Q_INVOKABLE void renderThreadRefreshImpl();

    /** 诊断辅助：尝试通过 QMetaMethod 跨线程调用窗口刷新 */
    void tryMetaMethodRefresh(const char* methodName);

    // ── 主动轮询定时器 ─────────────────────────────────────────────────
    QTimer* m_pollingTimer = nullptr;  // 主动轮询定时器，防止 Scene Graph 跳过
    bool m_pendingFrameDetected = false;  // 标记是否有待渲染帧
    int64_t m_lastPollTime = 0;  // 上次轮询时间

private:
    void updateFpsAndLatency();

    struct FrameSlot {
        std::shared_ptr<VideoFrame> frame;
        bool dirty = false;
    };

    // 三缓冲（渲染无锁交换）
    std::atomic<int> m_writeIdx{0};
    std::atomic<int> m_readIdx{2};
    std::atomic<int> m_middleIdx{1};
    std::atomic<bool> m_newFrame{false};
    std::array<FrameSlot, 3> m_slots;

    int  m_cameraId = 0;
    bool m_mirrorH  = false;

    // 统计（原子，由任意线程更新）
    std::atomic<double> m_fps{0.0};
    std::atomic<double> m_latencyMs{0.0};
    int64_t m_lastFrameTime = 0;
    uint32_t m_frameCount = 0;
    int64_t m_fpsWindowStart = 0;
    std::atomic<int> m_totalDeliverCount{0};
    quint64 m_lastFrameId = 0;  // 上次交付帧的 frameId（端到端追踪）

    // ── 渲染线程心跳检测 ─────────────────────────────────────────────────
    static constexpr int64_t RENDER_STALL_TIMEOUT_MS = 1000;
    static constexpr int MAX_PENDING_FRAMES = 10;
    std::atomic<int> m_pendingFramesCount{0};
    int64_t m_lastDeliverTime = 0;
    std::atomic<bool> m_renderStalled{false};
    std::atomic<int64_t> m_lastPaintNodeTime{0};

    // ── 方案 C 诊断 ─────────────────────────────────────────────────────
    std::atomic<int> m_renderThreadRefreshRequested{0};  // 请求渲染线程刷新次数
    std::atomic<int> m_renderThreadRefreshSucceeded{0};   // 渲染线程刷新成功次数
    std::atomic<int> m_forceRefreshCount{0};             // 总 forceRefresh 调用次数
    std::atomic<int> m_windowMetaMethodSucceeded{0};     // QMetaMethod 刷新成功次数
    std::atomic<int> m_polishAndUpdateSucceeded{0};      // polishAndUpdate 成功次数
    std::atomic<int> m_fallbackUpdateSucceeded{0};       // 降级 update() 成功次数

    // ── per-instance updatePaintNode 序列号（修复：每个实例独立计数）───────
    int m_updatePaintNodeSeq = 0;  // 替换原静态变量：每个实例独立序列号
    int m_lastPaintNodeSeqForHungry = 0;
    int m_skipCount = 0;

    // ── Placeholder node ───────────────────────────────────────────────
    QSGGeometryNode* m_placeholderNode = nullptr;
    std::atomic<bool> m_hasRealNode{false};

    VideoSGNode*                   m_sgNode    = nullptr;
    std::unique_ptr<IGpuInterop>   m_gpuInterop;
    bool                           m_interopInit = false;
};
