#include "VideoRenderer.h"
#include "VideoSGNode.h"
#include "../../infrastructure/media/gpu/GpuInteropFactory.h"
#include <QQuickWindow>
#include <QDebug>
#include <QDateTime>
#include <algorithm>
#include <memory>
#include <vector>

namespace {

std::shared_ptr<VideoFrame> qImageToYuv420Frame(const QImage& srcIn)
{
    try {
    const QImage src = srcIn.convertToFormat(QImage::Format_RGB888);
    if (src.isNull())
        return nullptr;
    const int w = src.width();
    const int h = src.height();
    if (w <= 0 || h <= 0)
        return nullptr;
    // 防止超大分辨率导致内存爆炸（8K 上限）
    if (w > 7680 || h > 4320) {
        qWarning() << "[VideoRenderer][WARN] qImageToYuv420Frame: 尺寸过大 w=" << w << " h=" << h
                   << "，拒绝防止内存爆炸";
        return nullptr;
    }

    const int cw = (w + 1) / 2;
    const int ch = (h + 1) / 2;

    auto yData = std::make_shared<std::vector<uint8_t>>(static_cast<size_t>(w * h));
    auto uData = std::make_shared<std::vector<uint8_t>>(static_cast<size_t>(cw * ch));
    auto vData = std::make_shared<std::vector<uint8_t>>(static_cast<size_t>(cw * ch));

    if (!yData || !uData || !vData) {
        qCritical() << "[Client][VideoRenderer] qImageToYuv420Frame: 向量分配失败 w=" << w << " h=" << h;
        return nullptr;
    }

    const int bpl = src.bytesPerLine();
    const uchar* bits = src.constBits();
    if (!bits) {
        qWarning() << "[Client][VideoRenderer] qImageToYuv420Frame: src.constBits() returned nullptr, image may be invalid (w="
                   << w << " h=" << h << "), dropping frame";
        return nullptr;
    }
    if (bpl <= 0) {
        qWarning() << "[Client][VideoRenderer] qImageToYuv420Frame: bytesPerLine() returned" << bpl
                   << "for image w=" << w << " h=" << h << ", dropping frame";
        return nullptr;
    }

    for (int y = 0; y < h; ++y) {
        const uchar* row = bits + y * bpl;
        for (int x = 0; x < w; ++x) {
            const int r = row[x * 3 + 0];
            const int g = row[x * 3 + 1];
            const int b = row[x * 3 + 2];
            const int Y = (77 * r + 150 * g + 29 * b) >> 8;
            (*yData)[static_cast<size_t>(y * w + x)] = static_cast<uint8_t>(std::clamp(Y, 0, 255));
        }
    }

    for (int by = 0; by < ch; ++by) {
        for (int bx = 0; bx < cw; ++bx) {
            int rSum = 0, gSum = 0, bSum = 0, cnt = 0;
            for (int dy = 0; dy < 2; ++dy) {
                const int y = by * 2 + dy;
                if (y >= h)
                    continue;
                const uchar* row = bits + y * bpl;
                for (int dx = 0; dx < 2; ++dx) {
                    const int x = bx * 2 + dx;
                    if (x >= w)
                        continue;
                    rSum += row[x * 3 + 0];
                    gSum += row[x * 3 + 1];
                    bSum += row[x * 3 + 2];
                    ++cnt;
                }
            }
            if (cnt == 0)
                cnt = 1;
            rSum /= cnt;
            gSum /= cnt;
            bSum /= cnt;
            const int U = ((-43 * rSum - 85 * gSum + 128 * bSum) >> 8) + 128;
            const int V = ((128 * rSum - 107 * gSum - 21 * bSum) >> 8) + 128;
            const size_t i = static_cast<size_t>(by * cw + bx);
            (*uData)[i] = static_cast<uint8_t>(std::clamp(U, 0, 255));
            (*vData)[i] = static_cast<uint8_t>(std::clamp(V, 0, 255));
        }
    }

    auto frame = std::make_shared<VideoFrame>();
    frame->memoryType  = VideoFrame::MemoryType::CPU_MEMORY;
    frame->pixelFormat = VideoFrame::PixelFormat::YUV420P;
    frame->width       = static_cast<uint32_t>(w);
    frame->height      = static_cast<uint32_t>(h);
    frame->planes[0].data   = yData->data();
    frame->planes[0].stride = static_cast<uint32_t>(w);
    frame->planes[0].size   = static_cast<uint32_t>(yData->size());
    frame->planes[1].data   = uData->data();
    frame->planes[1].stride = static_cast<uint32_t>(cw);
    frame->planes[1].size   = static_cast<uint32_t>(uData->size());
    frame->planes[2].data   = vData->data();
    frame->planes[2].stride = static_cast<uint32_t>(cw);
    frame->planes[2].size   = static_cast<uint32_t>(vData->size());

    struct YuvHold {
        std::shared_ptr<std::vector<uint8_t>> y;
        std::shared_ptr<std::vector<uint8_t>> u;
        std::shared_ptr<std::vector<uint8_t>> v;
    };
    auto hold = std::make_shared<YuvHold>();
    hold->y = std::move(yData);
    hold->u = std::move(uData);
    hold->v = std::move(vData);
    frame->poolRef = std::shared_ptr<void>(hold.get(), [hold](void*) { (void)hold; });

    return frame;
    } catch (const std::exception& e) {
        qCritical() << "[VideoRenderer][ERROR] qImageToYuv420Frame 总异常:" << e.what();
        return nullptr;
    } catch (...) {
        qCritical() << "[VideoRenderer][ERROR] qImageToYuv420Frame 未知异常";
        return nullptr;
    }
}

} // namespace

VideoRenderer::VideoRenderer(QQuickItem* parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
    m_fpsWindowStart = TimeUtils::wallClockMs();
    qInfo() << "[Client][VideoRenderer] created";
}

VideoRenderer::~VideoRenderer() = default;

void VideoRenderer::setFrame(const QImage& image)
{
    try {
        std::shared_ptr<VideoFrame> frame = qImageToYuv420Frame(image);
        if (!frame) {
            qWarning() << "[VideoRenderer][WARN] setFrame: qImageToYuv420Frame 返回空帧，跳过";
            return;
        }
        deliverFrame(std::move(frame));
    } catch (const std::exception& e) {
        qCritical() << "[VideoRenderer][ERROR] setFrame 总异常:" << e.what()
                   << " image.size=" << image.size();
    } catch (...) {
        qCritical() << "[VideoRenderer][ERROR] setFrame 未知异常 image.size=" << image.size();
    }
}

void VideoRenderer::deliverFrame(std::shared_ptr<VideoFrame> frame)
{
    if (!frame) return;

    try {
        // 三缓冲原子写（无锁，可在解码线程调用）
        const int writeIdx = m_writeIdx.load(std::memory_order_relaxed);
        m_slots[writeIdx].frame = std::move(frame);
        m_slots[writeIdx].dirty = true;

        // 原子交换 write 和 middle
        int old = m_middleIdx.exchange(writeIdx, std::memory_order_acq_rel);
        m_writeIdx.store(old, std::memory_order_relaxed);
        m_newFrame.store(true, std::memory_order_release);

        // 统计 FPS
        ++m_frameCount;
        const int64_t now = TimeUtils::wallClockMs();
        if (now - m_fpsWindowStart >= 1000) {
            const double fps = static_cast<double>(m_frameCount) / ((now - m_fpsWindowStart) / 1000.0);
            m_fps.store(fps);
            m_frameCount = 0;
            m_fpsWindowStart = now;
            emit fpsChanged(fps);
        }

        // 触发渲染（线程安全）
        if (window()) {
            window()->update();
        }
    } catch (const std::exception& e) {
        qCritical() << "[VideoRenderer][deliverFrame] EXCEPTION:" << e.what();
    } catch (...) {
        qCritical() << "[VideoRenderer][deliverFrame] UNKNOWN EXCEPTION";
    }
}

QSGNode* VideoRenderer::updatePaintNode(QSGNode* old, UpdatePaintNodeData*)
{
    try {
        // ── 渲染线程首次：初始化 GPU Interop（此处 GL 上下文已激活） ────────────
        if (!m_interopInit) {
            m_gpuInterop  = GpuInteropFactory::create();
            m_interopInit = true;
            qInfo() << "[Client][VideoRenderer] GPU interop backend:"
                    << (m_gpuInterop ? m_gpuInterop->name() : "none");
        }

        // 从三缓冲读取最新帧（渲染线程调用，无锁）
        if (m_newFrame.exchange(false, std::memory_order_acq_rel)) {
            int old2 = m_middleIdx.exchange(m_readIdx.load(std::memory_order_relaxed),
                                             std::memory_order_acq_rel);
            m_readIdx.store(old2, std::memory_order_relaxed);
        }

        auto& slot = m_slots[m_readIdx.load(std::memory_order_relaxed)];
        if (!slot.dirty || !slot.frame) {
            return old;
        }

        VideoSGNode* node = static_cast<VideoSGNode*>(old);
        if (!node) {
            node = new VideoSGNode();
            node->setGpuInterop(m_gpuInterop.get());
        }

        try {
            node->updateGeometry(QRectF(0, 0, width(), height()), m_mirrorH);
            node->updateFrame(*slot.frame);
        } catch (const std::exception& e) {
            qCritical() << "[VideoRenderer][ERROR] updatePaintNode: node->updateGeometry/updateFrame 异常:"
                       << " w=" << width() << " h=" << height() << " error=" << e.what();
        } catch (...) {
            qCritical() << "[VideoRenderer][ERROR] updatePaintNode: node->updateGeometry/updateFrame 未知异常";
        }
        slot.dirty = false;

        // 延迟统计
        if (slot.frame->captureTimestamp > 0) {
            const double latency = static_cast<double>(
                TimeUtils::wallClockMs() - slot.frame->captureTimestamp);
            m_latencyMs.store(latency);
            try { emit latencyChanged(latency); } catch (...) {}
        }

        // ── 诊断：三缓冲状态 + 渲染耗时（每 10 帧打印一次）────────────────────────
        const int64_t now = QDateTime::currentMSecsSinceEpoch();
        ++m_renderCallCount;
        if (m_renderCallCount % 10 == 0) {
            int64_t renderInterval = (m_lastRenderTime > 0) ? (now - m_lastRenderTime) : -1;
            m_lastRenderTime = now;
            const int w = m_writeIdx.load(std::memory_order_relaxed);
            const int r = m_readIdx.load(std::memory_order_relaxed);
            const int m = m_middleIdx.load(std::memory_order_relaxed);
            const bool newF = m_newFrame.load(std::memory_order_relaxed);
            const double avgRenderMs = (renderInterval > 0 && renderInterval < 10000)
                                          ? (static_cast<double>(renderInterval) / 10.0)
                                          : -1.0;
            qInfo() << "[VideoRenderer][Stats] fps=" << m_fps.load()
                     << " latency=" << m_latencyMs.load() << "ms"
                     << " tripleBuffer(w=" << w << " m=" << m << " r=" << r << ") newFrame=" << newF
                     << " avgRenderMs=" << avgRenderMs
                     << " calls=" << m_renderCallCount;
        }

        return node;
    } catch (const std::exception& e) {
        qCritical() << "[VideoRenderer][ERROR] updatePaintNode 总异常:" << e.what();
        return old;
    } catch (...) {
        qCritical() << "[VideoRenderer][ERROR] updatePaintNode 未知异常";
        return old;
    }
}

void VideoRenderer::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) {
        update();
    }
}

void VideoRenderer::componentComplete()
{
    QQuickItem::componentComplete();
    setFlag(ItemHasContents, true);
}

void VideoRenderer::updateFpsAndLatency()
{
    // Called internally; FPS/latency updated in deliverFrame/updatePaintNode
}
