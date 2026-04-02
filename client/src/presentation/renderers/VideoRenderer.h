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
    // ── 诊断日志增强 ─────────────────────────────────────────────────────────
    int64_t m_lastRenderTime = 0;      // 上次 updatePaintNode 执行时刻
    int m_renderCallCount = 0;         // 渲染调用计数
    quint64 m_lastFrameId = 0;         // 上次交付帧的 frameId（端到端追踪）
    // ── 诊断日志增强结束 ─────────────────────────────────────────────────────

    VideoSGNode*                   m_sgNode    = nullptr;
    std::unique_ptr<IGpuInterop>   m_gpuInterop;    // 在渲染线程延迟初始化
    bool                           m_interopInit = false;
};
