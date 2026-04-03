#pragma once
#include <QQuickItem>
#include <QImage>
#include <QSGNode>
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
    /** @param frameId  端到端帧序列号（emit→QML→setFrame→deliverFrame→updatePaintNode 追踪） */
    Q_INVOKABLE void deliverFrame(std::shared_ptr<VideoFrame> frame, quint64 frameId = 0);

    /** QML / WebRtcClient::videoFrameReady(QImage)：RGB→YUV420P 后走 deliverFrame（与现有 YUV 着色器一致）。
     *  @param frameId  从 C++ WebRtcClient::emit 传入的帧序列号，用于端到端追踪。
     *                  QML 端已打印 frameId，可与 C++ 日志对比确认 emit→setFrame→updatePaintNode 不断链。 */
    Q_INVOKABLE void setFrame(const QImage& image, quint64 frameId = 0);

signals:
    void cameraIdChanged();
    void mirrorChanged();
    void fpsChanged(double fps);
    void latencyChanged(double latencyMs);
    /** 渲染线程心跳信号：当 deliverFrame 被调用但渲染线程超过阈值未响应时发射 */
    void renderThreadStalled(int pendingFrames, int64_t stalledMs);

public:
    // ── 强制刷新机制（方案1）─────────────────────────────────────────────
    // 根因：Qt Scene Graph 在 VehicleSelectionDialog 显示期间可能阻塞渲染线程，
    // 导致 deliverFrame 收到帧但 updatePaintNode 不被调用。
    // 修复：在对话框关闭时强制刷新所有 VideoRenderer。
    /** Q_INVOKABLE：供 QML 在 VehicleSelectionDialog 关闭时调用，强制触发渲染 */
    Q_INVOKABLE void forceRefresh();

    /** 诊断：返回当前帧累积状态，供调试使用 */
    Q_INVOKABLE int pendingFrameCount() const { return m_pendingFramesCount.load(); }
    /** 诊断：返回上次 updatePaintNode 距今的毫秒数 */
    Q_INVOKABLE int64_t msSinceLastPaint() const;

protected:
    QSGNode* updatePaintNode(QSGNode* old, UpdatePaintNodeData* data) override;
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;
    void componentComplete() override;

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
    std::atomic<int> m_totalDeliverCount{0};  // deliverFrame 总计数（用于检测渲染线程饥饿）
    // ── 诊断日志增强 ─────────────────────────────────────────────────────────
    int64_t m_lastRenderTime = 0;      // 上次 updatePaintNode 执行时刻
    int m_renderCallCount = 0;         // 渲染调用计数
    quint64 m_lastFrameId = 0;         // 上次交付帧的 frameId（端到端追踪）

    // ── 渲染饥饿检测（防止 Scene Graph 调度器跳过 VideoRenderer）──────────────
    // Qt Quick Scene Graph 在首次 updatePaintNode 无有效帧时可能将项标记为"静态"，
    // 后续 window()->update() 不再触发 updatePaintNode。通过主动计数检测此问题。
    int m_renderThreadHungryCount = 0;   // deliverFrame 被调用但 updatePaintNode 未执行的次数
    int m_lastDeliverSeqForHungry = 0;   // 上次计算饥饿计数时的 deliverFrame seq
    int m_lastPaintNodeSeqForHungry = 0; // 上次 updatePaintNode 记录的全局 seq
    static constexpr int RENDER_HUNGRY_THRESHOLD = 30;  // 超过 30 次 deliverFrame 但 updatePaintNode 未执行 → 触发强制调度
    // ── 诊断日志增强结束 ─────────────────────────────────────────────────────

    VideoSGNode*                   m_sgNode    = nullptr;
    std::unique_ptr<IGpuInterop>   m_gpuInterop;    // 在渲染线程延迟初始化
    bool                           m_interopInit = false;

    // ── 修复 VideoRenderer 渲染线程饥饿根因 ──────────────────────────────────────
    // Qt Scene Graph 调度器在 updatePaintNode 首次返回 nullptr 时，
    // 会将该 item 标记为"静态"（static），永久停止调度 updatePaintNode。
    // 根因：首次调用 updatePaintNode 时（QML 刚加载，WebRTC 流未建立）无帧，
    // → 返回 nullptr → Scene Graph 降级 item → WebRTC 流建立后帧永远无法渲染。
    // 修复：永不返回 nullptr（即使无帧也返回 placeholder node），确保 Scene Graph 持续调度。

    // 渲染线程饥饿检测：记录 lastPaintNodeSeq，每次 updatePaintNode 被调用时检查
    // 是否比 deliverFrame 的 seq 落后（说明被 Scene Graph 跳过）
    int  m_lastPNSeeByUpdate = 0;          // updatePaintNode 最后看到的 paintNodeSeq
    int  m_skipCount         = 0;         // 连续无帧被跳过的次数（用于诊断）

    // ── 渲染线程心跳检测 ─────────────────────────────────────────────────
    // 当 deliverFrame 被调用但渲染线程超过 RENDER_STALL_TIMEOUT_MS 未执行 updatePaintNode 时，
    // 说明渲染线程可能被阻塞（Dialog 显示期间常见），此时发射 renderThreadStalled 信号。
    static constexpr int64_t RENDER_STALL_TIMEOUT_MS = 1000;  // 1秒无响应视为阻塞
    static constexpr int MAX_PENDING_FRAMES = 10;              // 最多累积10帧
    std::atomic<int> m_pendingFramesCount{0};                 // 待渲染帧计数
    int64_t m_lastDeliverTime = 0;                             // 上次 deliverFrame 时间戳
    std::atomic<bool> m_renderStalled{false};                // 渲染阻塞标志
    std::atomic<int64_t> m_lastPaintNodeTime{0};              // 上次 updatePaintNode 执行时间

    // 当无帧可用时，返回一个 placeholder node 而非 nullptr，
    // 防止 Scene Graph 将 VideoRenderer 降级为"静态"项。
    // 这个 placeholder 节点渲染黑色背景，保持 item 活跃在 Scene Graph 调度队列中。
    QSGGeometryNode* m_placeholderNode = nullptr;
    std::atomic<bool> m_hasRealNode{false};  // 标记是否已创建过真正的 VideoSGNode
};
