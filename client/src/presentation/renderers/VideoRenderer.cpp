#include "VideoRenderer.h"
#include "VideoSGNode.h"
#include "../../infrastructure/media/gpu/GpuInteropFactory.h"
#include <QQuickWindow>
#include <QQuickRenderControl>
#include <QDebug>
#include <QDateTime>
#include <QOpenGLContext>
#include <QThread>
#include <QSGGeometry>
#include <QSGFlatColorMaterial>
#include <QMetaMethod>
#include <QCoreApplication>
#include <algorithm>
#include <cstring>
#include <memory>
#include <vector>

// ── 静态诊断计数器（全局，用于识别哪个 VideoRenderer 首次创建）─────────────
static QAtomicInt s_videoRendererInstanceCount{0};

namespace {

std::shared_ptr<VideoFrame> qImageToYuv420Frame(const QImage& srcIn)
{
    static QAtomicInt s_totalCount{0};
    static QAtomicInt s_successCount{0};
    static QAtomicInt s_failNullCount{0};
    static QAtomicInt s_failSizeCount{0};
    static QAtomicInt s_failConvertCount{0};
    static QAtomicInt s_failAllocCount{0};
    static QAtomicInt s_failBitsCount{0};
    static QAtomicInt s_failBplCount{0};
    static QAtomicInt s_failOverflowCount{0};
    static QAtomicInt s_failExceptionCount{0};
    const int callSeq = ++s_totalCount;

    if (srcIn.isNull()) {
        const int cnt = ++s_failNullCount;
        if (cnt <= 3) {
            qWarning() << "[Client][VideoRenderer][qImageToYuv420] ★ 输入图像为 Null，跳过"
                       << " call#=" << callSeq << " fail#=" << cnt;
        }
        return nullptr;
    }

    const int srcW = srcIn.width();
    const int srcH = srcIn.height();
    const int srcFmt = static_cast<int>(srcIn.format());

    if (srcW <= 0 || srcH <= 0) {
        const int cnt = ++s_failSizeCount;
        if (cnt <= 3) {
            qWarning() << "[Client][VideoRenderer][qImageToYuv420] ★ 尺寸无效，跳过"
                       << " call#=" << callSeq << " fail#=" << cnt
                       << " srcW=" << srcW << " srcH=" << srcH;
        }
        return nullptr;
    }

    if (srcW > 7680 || srcH > 4320) {
        const int cnt = ++s_failOverflowCount;
        if (cnt <= 3) {
            qWarning() << "[Client][VideoRenderer][qImageToYuv420] ★ 分辨率过大，拒绝防止内存爆炸"
                       << " call#=" << callSeq << " fail#=" << cnt
                       << " srcW=" << srcW << " srcH=" << srcH;
        }
        return nullptr;
    }

    QImage src;
    try {
        if (srcIn.format() == QImage::Format_RGB888) {
            src = srcIn;
        } else if (srcIn.format() == QImage::Format_Grayscale8) {
            src = srcIn.convertToFormat(QImage::Format_RGB888);
            if (src.isNull()) {
                const int cnt = ++s_failConvertCount;
                if (cnt <= 3) {
                    qWarning() << "[Client][VideoRenderer][qImageToYuv420] ★ Grayscale8→RGB888 转换失败"
                               << " call#=" << callSeq << " fail#=" << cnt
                               << " srcFmt=" << srcFmt << " srcSize=" << srcW << "x" << srcH;
                }
                return nullptr;
            }
            if (callSeq <= 5) {
                qInfo() << "[Client][VideoRenderer][qImageToYuv420] format=Grayscale8 转换为 RGB888 成功"
                         << " call#=" << callSeq << " srcFmt=" << srcFmt << " newFmt=" << static_cast<int>(src.format());
            }
        } else {
            src = srcIn.convertToFormat(QImage::Format_RGB888);
            if (src.isNull()) {
                const int cnt = ++s_failConvertCount;
                if (cnt <= 3) {
                    qCritical() << "[Client][VideoRenderer][qImageToYuv420] ★ format 转换失败，帧被丢弃"
                                 << " call#=" << callSeq << " fail#=" << cnt
                                 << " srcFmt=" << srcFmt
                                 << "（0=Invalid, 3=RGB888, 5=Grayscale8, 13=Alpha8）"
                                 << " srcSize=" << srcW << "x" << srcH;
                }
                return nullptr;
            }
            if (callSeq <= 5) {
                qInfo() << "[Client][VideoRenderer][qImageToYuv420] format 转换"
                         << " call#=" << callSeq
                         << " srcFmt=" << srcFmt << " → newFmt=" << static_cast<int>(src.format());
            }
        }
    } catch (const std::exception& e) {
        const int cnt = ++s_failExceptionCount;
        qCritical() << "[Client][VideoRenderer][qImageToYuv420] ★ format 转换异常"
                     << " call#=" << callSeq << " fail#=" << cnt
                     << " error=" << e.what()
                     << " srcFmt=" << srcFmt << " srcSize=" << srcW << "x" << srcH;
        return nullptr;
    } catch (...) {
        const int cnt = ++s_failExceptionCount;
        qCritical() << "[Client][VideoRenderer][qImageToYuv420] ★ format 转换未知异常"
                     << " call#=" << callSeq << " fail#=" << cnt
                     << " srcFmt=" << srcFmt << " srcSize=" << srcW << "x" << srcH;
        return nullptr;
    }

    const int w = src.width();
    const int h = src.height();

    if (w <= 0 || h <= 0) {
        const int cnt = ++s_failSizeCount;
        if (cnt <= 3) {
            qWarning() << "[Client][VideoRenderer][qImageToYuv420] ★ 转换后尺寸无效，跳过"
                       << " call#=" << callSeq << " fail#=" << cnt
                       << " w=" << w << " h=" << h;
        }
        return nullptr;
    }

    const int cw = (w + 1) / 2;
    const int ch = (h + 1) / 2;

    std::shared_ptr<std::vector<uint8_t>> yData, uData, vData;
    try {
        yData = std::make_shared<std::vector<uint8_t>>(static_cast<size_t>(w * h));
        uData = std::make_shared<std::vector<uint8_t>>(static_cast<size_t>(cw * ch));
        vData = std::make_shared<std::vector<uint8_t>>(static_cast<size_t>(cw * ch));
    } catch (const std::exception& e) {
        const int cnt = ++s_failAllocCount;
        qCritical() << "[Client][VideoRenderer][qImageToYuv420] ★ YUV 向量分配失败"
                     << " call#=" << callSeq << " fail#=" << cnt
                     << " error=" << e.what()
                     << " w=" << w << " h=" << h << " cw=" << cw << " ch=" << ch;
        return nullptr;
    }

    if (!yData || !uData || !vData || yData->empty() || uData->empty() || vData->empty()) {
        const int cnt = ++s_failAllocCount;
        qCritical() << "[Client][VideoRenderer][qImageToYuv420] ★ YUV 向量为空或分配失败"
                     << " call#=" << callSeq << " fail#=" << cnt
                     << " yData=" << (bool)yData << " uData=" << (bool)uData << " vData=" << (bool)vData
                     << " ySize=" << (yData ? yData->size() : 0)
                     << " uSize=" << (uData ? uData->size() : 0)
                     << " vSize=" << (vData ? vData->size() : 0);
        return nullptr;
    }

    const int bpl = src.bytesPerLine();
    const uchar* bits = src.constBits();
    if (!bits) {
        const int cnt = ++s_failBitsCount;
        if (cnt <= 3) {
            qWarning() << "[Client][VideoRenderer][qImageToYuv420] ★ constBits() 返回 nullptr，跳过"
                       << " call#=" << callSeq << " fail#=" << cnt
                       << " w=" << w << " h=" << h << " bpl=" << bpl;
        }
        return nullptr;
    }
    if (bpl <= 0) {
        const int cnt = ++s_failBplCount;
        if (cnt <= 3) {
            qWarning() << "[Client][VideoRenderer][qImageToYuv420] ★ bytesPerLine() 异常，跳过"
                       << " call#=" << callSeq << " fail#=" << cnt
                       << " w=" << w << " h=" << h << " bpl=" << bpl;
        }
        return nullptr;
    }

    try {
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
    } catch (const std::exception& e) {
        qCritical() << "[Client][VideoRenderer][qImageToYuv420] ★ RGB→YUV 转换异常"
                     << " call#=" << callSeq
                     << " error=" << e.what()
                     << " w=" << w << " h=" << h;
        return nullptr;
    } catch (...) {
        qCritical() << "[Client][VideoRenderer][qImageToYuv420] ★ RGB→YUV 转换未知异常"
                     << " call#=" << callSeq
                     << " w=" << w << " h=" << h;
        return nullptr;
    }

    std::shared_ptr<VideoFrame> frame;
    try {
        frame = std::make_shared<VideoFrame>();
        if (!frame) {
            qCritical() << "[Client][VideoRenderer][qImageToYuv420] ★ VideoFrame 分配失败";
            return nullptr;
        }

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

        ++s_successCount;
        if (callSeq <= 5) {
            qInfo() << "[Client][VideoRenderer][qImageToYuv420] ★ 转换成功"
                     << " call#=" << callSeq
                     << " srcFmt=" << srcFmt << " → newFmt=" << static_cast<int>(src.format())
                     << " size=" << w << "x" << h
                     << " totalSuccess=" << static_cast<int>(s_successCount)
                     << " totalFail=" << (static_cast<int>(s_totalCount) - static_cast<int>(s_successCount));
        }
        return frame;

    } catch (const std::exception& e) {
        qCritical() << "[Client][VideoRenderer][qImageToYuv420] ★ VideoFrame 构建异常"
                     << " call#=" << callSeq
                     << " error=" << e.what();
        return nullptr;
    } catch (...) {
        qCritical() << "[Client][VideoRenderer][qImageToYuv420] ★ VideoFrame 构建未知异常"
                     << " call#=" << callSeq;
        return nullptr;
    }
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════════
// 辅助函数：创建黑色占位符 Placeholder Node
// ═══════════════════════════════════════════════════════════════════════════════
static QSGGeometryNode* createPlaceholderNode() {
    QSGGeometryNode* node = new QSGGeometryNode();
    QSGGeometry* geo = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 4, 6);
    geo->setDrawingMode(QSGGeometry::DrawTriangles);

    auto* vertices = geo->vertexDataAsPoint2D();
    vertices[0].x = 0.0f; vertices[0].y = 0.0f;
    vertices[1].x = 1.0f; vertices[1].y = 0.0f;
    vertices[2].x = 0.0f; vertices[2].y = 1.0f;
    vertices[3].x = 1.0f; vertices[3].y = 1.0f;

    quint16 indices[] = { 0, 1, 2, 1, 3, 2 };
    memcpy(geo->indexData(), indices, sizeof(indices));

    node->setGeometry(geo);

    QSGFlatColorMaterial* mat = new QSGFlatColorMaterial();
    mat->setColor(Qt::black);
    node->setMaterial(mat);

    node->setFlag(QSGNode::OwnedByParent, false);
    return node;
}

// ═══════════════════════════════════════════════════════════════════════════════
// 辅助函数：更新 Placeholder Node 几何信息
// ═══════════════════════════════════════════════════════════════════════════════
static void updatePlaceholderGeometry(QSGGeometryNode* node, const QRectF& rect) {
    if (!node) return;

    QSGGeometry* geo = node->geometry();
    if (!geo) return;

    auto* vertices = geo->vertexDataAsPoint2D();
    vertices[0].x = rect.left();   vertices[0].y = rect.top();
    vertices[1].x = rect.right();  vertices[1].y = rect.top();
    vertices[2].x = rect.left();   vertices[2].y = rect.bottom();
    vertices[3].x = rect.right();  vertices[3].y = rect.bottom();

    node->markDirty(QSGNode::DirtyGeometry);
}

// ═══════════════════════════════════════════════════════════════════════════════
// VideoRenderer 构造函数
// ═══════════════════════════════════════════════════════════════════════════════
VideoRenderer::VideoRenderer(QQuickItem* parent)
    : QQuickItem(parent)
    , m_pollingTimer(nullptr)
    , m_pendingFrameDetected(false)
    , m_lastPollTime(0)
{
    setFlag(ItemHasContents, true);
    m_fpsWindowStart = TimeUtils::wallClockMs();
    m_lastPaintNodeTime.store(m_fpsWindowStart);

    const int instanceId = ++s_videoRendererInstanceCount;
    setProperty("instanceId", instanceId);

    qInfo() << "[Client][VideoRenderer] created"
            << " instanceId=" << instanceId
            << " title=" << property("title").toString()
            << " timestamp=" << m_fpsWindowStart
            << " thread=" << (void*)QThread::currentThreadId();
}

VideoRenderer::~VideoRenderer()
{
    stopPollingTimer();
    qInfo() << "[Client][VideoRenderer] destroyed"
            << " instanceId=" << property("instanceId").toInt()
            << " title=" << property("title").toString()
            << " ★★★ updatePaintNode 总调用次数 ★★★ instanceId=" << property("instanceId").toInt()
            << " m_updatePaintNodeSeq=" << m_updatePaintNodeSeq;
}

// ═══════════════════════════════════════════════════════════════════════════════
// setFrame：QML → C++ 跨语言调用入口
// ═══════════════════════════════════════════════════════════════════════════════
void VideoRenderer::setFrame(const QImage& image, quint64 frameId)
{
    static QAtomicInt s_callCount{0};
    static QAtomicInt s_nullCount{0};
    static QAtomicInt s_droppedCount{0};
    const int callSeq = ++s_callCount;

    if (callSeq <= 10 || callSeq % 30 == 0) {
        qInfo() << "[Client][VideoRenderer] ★★★ setFrame(QML→C++) 被调用"
                << " call#=" << callSeq
                << " frameId=" << frameId
                << " image.isNull=" << image.isNull()
                << " size=" << image.width() << "x" << image.height()
                << " format=" << static_cast<int>(image.format())
                << " window=" << (void*)window()
                << " componentComplete=" << isComponentComplete()
                << " callingThread=" << (void*)QThread::currentThreadId();
    }

    try {
        std::shared_ptr<VideoFrame> frame = qImageToYuv420Frame(image);
        if (!frame) {
            const int dropSeq = ++s_droppedCount;
            if (dropSeq <= 10 || dropSeq % 30 == 0) {
                qWarning() << "[Client][VideoRenderer][WARN] setFrame: qImageToYuv420Frame 返回空帧，跳过"
                           << " call#=" << callSeq << " drop#=" << dropSeq << " frameId=" << frameId
                           << " image.isNull=" << image.isNull()
                           << " size=" << image.width() << "x" << image.height();
            }
            return;
        }
        m_lastFrameId = frameId;
        deliverFrame(std::move(frame), frameId);
    } catch (const std::exception& e) {
        qCritical() << "[Client][VideoRenderer][ERROR] setFrame 总异常:" << e.what()
                   << " image.size=" << image.size() << " frameId=" << frameId;
    } catch (...) {
        qCritical() << "[Client][VideoRenderer][ERROR] setFrame 未知异常 image.size=" << image.size()
                    << " frameId=" << frameId;
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// deliverFrame：三缓冲写入 + 触发渲染（可在任意线程调用）
// ═══════════════════════════════════════════════════════════════════════════════
void VideoRenderer::deliverFrame(std::shared_ptr<VideoFrame> frame, quint64 frameId)
{
    if (!frame) return;

    frame->frameId = frameId;
    m_lastFrameId = frameId;

    static QAtomicInt s_deliverLogCount{0};
    const int logSeq = ++s_deliverLogCount;
    const int totalDeliver = ++m_totalDeliverCount;
    const int64_t now = TimeUtils::wallClockMs();

    // ── 渲染线程心跳检测 ─────────────────────────────────────────────────
    const int64_t msSinceLastPN = now - m_lastPaintNodeTime.load();
    const int prevPending = m_pendingFramesCount.fetch_add(1);
    const int pendingNow = prevPending + 1;

    if (pendingNow > MAX_PENDING_FRAMES || msSinceLastPN > RENDER_STALL_TIMEOUT_MS) {
        qWarning() << "[Client][VideoRenderer][HEARTBEAT] ★★★ 渲染线程心跳异常 ★★★"
                    << " logSeq=" << logSeq << " frameId=" << frameId
                    << " pendingFrames=" << pendingNow << " (max=" << MAX_PENDING_FRAMES << ")"
                    << " msSinceLastPaint=" << msSinceLastPN << "ms (threshold=" << RENDER_STALL_TIMEOUT_MS << "ms)"
                    << " totalDeliver=" << totalDeliver
                    << " m_updatePaintNodeSeq=" << m_updatePaintNodeSeq
                    << " instanceId=" << property("instanceId").toInt()
                    << " title=" << property("title").toString()
                    << " callingThread=" << (void*)QThread::currentThreadId()
                    << " ★ 渲染线程疑似被阻塞，方案C将尝试渲染线程直接刷新 ★";
        m_renderStalled.store(true);
        emit renderThreadStalled(pendingNow, msSinceLastPN);

        // ── ★★★ 方案 C 核心：渲染线程直接刷新 ★★★ ───────────────────────────
        // 当检测到渲染线程阻塞时，立即触发渲染线程直接刷新
        // 绕过主线程事件循环（modal 对话框阻塞时 window()->update() 完全失效）
        renderThreadRefreshImpl();
    }

    if (logSeq <= 10 || logSeq % 30 == 0 || msSinceLastPN > RENDER_STALL_TIMEOUT_MS) {
        qInfo() << "[Client][VideoRenderer] ★★★ deliverFrame 被调用"
                << " seq=" << logSeq << " frameId=" << m_lastFrameId
                << " window=" << (void*)window()
                << " frame=" << frame->width << "x" << frame->height
                << " pixelFormat=" << static_cast<int>(frame->pixelFormat)
                << " memoryType=" << static_cast<int>(frame->memoryType)
                << " msSinceLastPN=" << msSinceLastPN << "ms"
                << " pendingFrames=" << pendingNow;
    }

    try {
        // 三缓冲原子写（无锁，可在解码线程调用）
        const int writeIdx = m_writeIdx.load(std::memory_order_relaxed);
        m_slots[writeIdx].frame = std::move(frame);
        m_slots[writeIdx].dirty = true;

        int old = m_middleIdx.exchange(writeIdx, std::memory_order_acq_rel);
        m_writeIdx.store(old, std::memory_order_relaxed);
        m_newFrame.store(true, std::memory_order_release);

        // 统计 FPS
        ++m_frameCount;
        const int64_t fpsNow = TimeUtils::wallClockMs();
        if (fpsNow - m_fpsWindowStart >= 1000) {
            const double fps = static_cast<double>(m_frameCount) / ((fpsNow - m_fpsWindowStart) / 1000.0);
            m_fps.store(fps);
            m_frameCount = 0;
            m_fpsWindowStart = fpsNow;
            emit fpsChanged(fps);
            if (logSeq <= 10) {
                qInfo() << "[Client][VideoRenderer] ★★★ FPS emit fps=" << fps
                           << " window=" << (void*)window() << " frameId=" << m_lastFrameId;
            }
        }

        // 触发渲染（线程安全）
        QQuickWindow* win = window();

        // ── ★★★ 方案 C：优先使用渲染线程直接刷新，降级到 window()->update() ★★★
        if (win) {
            // ★★★ 策略 1：渲染线程直接刷新（最优）★★★
            // 通过 QMetaMethod::invoke + Qt::QueuedConnection 向渲染线程投递刷新
            // 绕过主线程事件循环，modal 对话框阻塞时仍然有效
            bool refreshed = false;

            // 获取 QQuickWindow 的 polishAndUpdate 方法（Qt 内部方法）
            // QMetaMethod::invoke 跨线程调用，自动路由到渲染线程事件队列
            const QMetaMethod polishAndUpdateMethod = win->metaObject()->method(
                win->metaObject()->indexOfMethod("polishAndUpdate()"));
            if (polishAndUpdateMethod.isValid()) {
                // Qt::QueuedConnection 确保在渲染线程的事件循环中执行
                refreshed = polishAndUpdateMethod.invoke(win, Qt::QueuedConnection);
                if (refreshed) {
                    m_polishAndUpdateSucceeded.fetch_add(1);
                    if (logSeq <= 5) {
                        qInfo() << "[Client][VideoRenderer][方案C] ★ polishAndUpdate() 跨线程调用成功 ★"
                                << " seq=" << logSeq << " frameId=" << frameId
                                << " window=" << (void*)win
                                << " callingThread=" << (void*)QThread::currentThreadId()
                                << " winThread=" << (void*)win->thread();
                    }
                } else {
                    if (logSeq <= 5) {
                        qWarning() << "[Client][VideoRenderer][方案C] polishAndUpdate() 跨线程调用失败（返回值=false）"
                                   << " seq=" << logSeq << " frameId=" << frameId;
                    }
                }
            } else {
                if (logSeq <= 5) {
                    qWarning() << "[Client][VideoRenderer][方案C] polishAndUpdate() 方法未找到"
                               << " seq=" << logSeq << " frameId=" << frameId;
                }
            }

            // ★★★ 策略 2：QMetaMethod 直接调用窗口 update（次优）★★★
            if (!refreshed) {
                const QMetaMethod updateMethod = win->metaObject()->method(
                    win->metaObject()->indexOfMethod("update()"));
                if (updateMethod.isValid()) {
                    refreshed = updateMethod.invoke(win, Qt::QueuedConnection);
                    if (refreshed) {
                        m_windowMetaMethodSucceeded.fetch_add(1);
                        if (logSeq <= 5) {
                            qInfo() << "[Client][VideoRenderer][方案C] ★ update() QMetaMethod 跨线程调用成功 ★"
                                    << " seq=" << logSeq;
                        }
                    }
                }
            }

            // ★★★ 策略 3：fallback polish() + update()（对话框打开时可能无效）★★★
            if (!refreshed) {
                polish();
                win->update();
                m_fallbackUpdateSucceeded.fetch_add(1);
                if (logSeq <= 5) {
                    qWarning() << "[Client][VideoRenderer][方案C] 使用 fallback polish()+update()"
                               << " seq=" << logSeq << " ★ 对话框打开时可能无效 ★";
                }
            }

            // ★★★ 额外检测：pendingFrames 持续累积时，多次触发刷新 ★★★
            if (pendingNow > MAX_PENDING_FRAMES) {
                m_pendingFrameDetected = true;
                // 立即再次调用（1次额外刷新）
                polish();
                win->update();

                static QAtomicInt s_extraRefreshCount{0};
                const int extraCount = ++s_extraRefreshCount;
                if (extraCount <= 20 || extraCount % 60 == 0) {
                    qWarning() << "[Client][VideoRenderer][FORCE-REFRESH] ★★★ pendingFrames 累积过多，额外刷新 ★★★"
                               << " seq=" << logSeq << " frameId=" << frameId
                               << " pendingFrames=" << pendingNow
                               << " msSinceLastPN=" << msSinceLastPN << "ms"
                               << " isVisible=" << isVisible()
                               << " window=" << (void*)win
                               << " extraRefreshCount=" << extraCount
                               << " instanceId=" << property("instanceId").toInt()
                               << " title=" << property("title").toString()
                               << " ★ Scene Graph 疑似跳过 updatePaintNode，额外刷新 ★";
                }
            }

            if (logSeq <= 5) {
                qInfo() << "[Client][VideoRenderer] ★★★ window()->update() 被调用"
                           "（触发渲染线程下次 paint），window=" << (void*)window();
            }
        } else {
            qCritical() << "[Client][VideoRenderer][FATAL] window() 返回 nullptr！"
                          " VideoRenderer 未加入 QML 场景图，window()->update() 不会被调用，"
                          " 视频将完全不显示！window=" << (void*)window()
                          << " componentComplete=" << isComponentComplete()
                          << " width=" << width() << " height=" << height();
        }
    } catch (const std::exception& e) {
        qCritical() << "[Client][VideoRenderer][deliverFrame] EXCEPTION:" << e.what();
    } catch (...) {
        qCritical() << "[Client][VideoRenderer][deliverFrame] UNKNOWN EXCEPTION";
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// renderThreadRefreshImpl：方案 C 核心，在渲染线程上下文中执行节点更新
// ═══════════════════════════════════════════════════════════════════════════════
void VideoRenderer::renderThreadRefreshImpl()
{
    // 记录渲染线程直接刷新请求
    const int reqCount = m_renderThreadRefreshRequested.fetch_add(1);
    const int64_t now = TimeUtils::wallClockMs();
    const int pendingFrames = m_pendingFramesCount.load();
    const int64_t msSinceLastPN = now - m_lastPaintNodeTime.load();

    qInfo() << "[Client][VideoRenderer][方案C] ★★★ renderThreadRefreshImpl 被调用 ★★★"
            << " reqCount=" << reqCount
            << " pendingFrames=" << pendingFrames
            << " msSinceLastPN=" << msSinceLastPN << "ms"
            << " isVisible=" << isVisible()
            << " instanceId=" << property("instanceId").toInt()
            << " title=" << property("title").toString()
            << " callingThread=" << (void*)QThread::currentThreadId()
            << " window=" << (void*)window();

    QQuickWindow* win = window();
    if (!win) {
        qWarning() << "[Client][VideoRenderer][方案C] window() == nullptr，跳过";
        return;
    }

    // ── 尝试 QMetaMethod 跨线程调用 polishAndUpdate() ───────────────────────────
    const QMetaMethod polishAndUpdateMethod = win->metaObject()->method(
        win->metaObject()->indexOfMethod("polishAndUpdate()"));

    bool success = false;
    if (polishAndUpdateMethod.isValid()) {
        success = polishAndUpdateMethod.invoke(win, Qt::QueuedConnection);
        if (success) {
            const int succCount = m_renderThreadRefreshSucceeded.fetch_add(1);
            qInfo() << "[Client][VideoRenderer][方案C] ★ polishAndUpdate() 跨线程刷新成功 ★"
                    << " reqCount=" << reqCount << " succCount=" << succCount
                    << " pendingFrames=" << pendingFrames
                    << " msSinceLastPN=" << msSinceLastPN << "ms"
                    << " callingThread=" << (void*)QThread::currentThreadId()
                    << " winThread=" << (void*)win->thread();
        } else {
            qWarning() << "[Client][VideoRenderer][方案C] polishAndUpdate() 跨线程调用返回 false"
                       << " reqCount=" << reqCount;
        }
    } else {
        qWarning() << "[Client][VideoRenderer][方案C] polishAndUpdate() 方法未找到"
                   << " reqCount=" << reqCount << " ★ 使用 fallback update() ★";
        // Fallback: 直接调用窗口 update
        polish();
        win->update();
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// tryMetaMethodRefresh：诊断辅助，通过 QMetaMethod 跨线程调用窗口刷新
// ═══════════════════════════════════════════════════════════════════════════════
void VideoRenderer::tryMetaMethodRefresh(const char* methodName)
{
    QQuickWindow* win = window();
    if (!win) return;

    const QMetaMethod method = win->metaObject()->method(
        win->metaObject()->indexOfMethod(methodName));

    if (method.isValid()) {
        const bool ok = method.invoke(win, Qt::QueuedConnection);
        qInfo() << "[Client][VideoRenderer][方案C] QMetaMethod::invoke(" << methodName << ")"
                << " ok=" << ok
                << " callingThread=" << (void*)QThread::currentThreadId()
                << " winThread=" << (void*)win->thread();
    } else {
        qWarning() << "[Client][VideoRenderer][方案C] 方法" << methodName << " 未找到";
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// updatePaintNode：主渲染函数（仅在渲染线程调用）
// ═══════════════════════════════════════════════════════════════════════════════
QSGNode* VideoRenderer::updatePaintNode(QSGNode* old, UpdatePaintNodeData*)
{
    // ── per-instance 序列号（修复静态变量问题：每个实例独立计数）──────────────
    const int seq = ++m_updatePaintNodeSeq;
    const int totalDeliver = m_totalDeliverCount.load();
    const int64_t now = TimeUtils::wallClockMs();

    // ── 渲染线程心跳：计算距上次的时间差（在覆盖前读取）──────────────────
    const int64_t msSinceLastPN = now - m_lastPaintNodeTime.load();

    // ── 渲染线程心跳：记录本次 updatePaintNode 执行时间 ───────────────────
    m_lastPaintNodeTime.store(now);

    // ── per-instance 渲染饥饿检测 ─────────────────────────────────────────
    if (seq != m_lastPaintNodeSeqForHungry && m_lastPaintNodeSeqForHungry > 0) {
        m_skipCount++;
        if (m_skipCount <= 5 || m_skipCount % 30 == 0) {
            qWarning() << "[Client][VideoRenderer][HUNGRY] ★★★ per-instance Scene Graph 跳过检测 ★★★"
                       << " instanceId=" << property("instanceId").toInt()
                       << " title=" << property("title").toString()
                       << " seq=" << seq << " lastRecordedPN=" << m_lastPaintNodeSeqForHungry
                       << " skipCount=" << m_skipCount
                       << " totalDeliver=" << totalDeliver
                       << " ★ 持续增长说明 Scene Graph 已跳过此实例！";
        }
    }
    m_lastPaintNodeSeqForHungry = seq;

    // 渲染成功时减少 pending frames 计数
    static int s_lastReportedPending = 0;
    const int currentPending = m_pendingFramesCount.exchange(0);
    if (currentPending > 0 && (seq <= 10 || currentPending > s_lastReportedPending)) {
        qInfo() << "[Client][VideoRenderer][HEARTBEAT] ★★★ 渲染线程恢复 ★★★"
                << " instanceId=" << property("instanceId").toInt()
                << " title=" << property("title").toString()
                << " seq=" << seq << " consumedPending=" << currentPending
                << " msSinceLastPN=" << msSinceLastPN << "ms"
                << " ★ deliverFrame 期间帧已被渲染，消费 pending=" << currentPending
                << " renderThreadRefreshSucceeded=" << m_renderThreadRefreshSucceeded.load()
                << " polishAndUpdateSucceeded=" << m_polishAndUpdateSucceeded.load()
                << " fallbackUpdateSucceeded=" << m_fallbackUpdateSucceeded.load();
        s_lastReportedPending = currentPending;
        m_renderStalled.store(false);
    }

    // ── 首次调用诊断 ─────────────────────────────────────────────────────────
    if (seq == 1) {
        qInfo() << "[Client][VideoRenderer] ═══════════════════════════════════════"
                << " instanceId=" << property("instanceId").toInt()
                << " title=" << property("title").toString()
                << " firstPaint=true width=" << width() << " height=" << height()
                << " isComponentComplete=" << isComponentComplete()
                << " isVisible=" << isVisible()
                << " window=" << (void*)window()
                << " renderThread=" << (void*)QThread::currentThreadId()
                << " ═══════════════════════════════════════";
    }

    // ── 诊断：渲染线程调用频率 ─────────────────────────────────────────────
    if (seq <= 20 || seq % 60 == 0) {
        qInfo() << "[Client][VideoRenderer] ★ updatePaintNode"
                << " instanceId=" << property("instanceId").toInt()
                << " title=" << property("title").toString()
                << " seq=" << seq << " totalDeliver=" << totalDeliver
                << " old=" << (void*)old
                << " width=" << width() << " height=" << height()
                << " isVisible=" << isVisible()
                << " glCtx=" << (void*)QOpenGLContext::currentContext()
                << " callingThread=" << (void*)QThread::currentThreadId();
    }

    try {
        // ── 初始化 GPU Interop（首次渲染线程调用时）─────────────────────────────
        if (!m_interopInit) {
            m_gpuInterop = GpuInteropFactory::create();
            m_interopInit = true;
            qInfo() << "[Client][VideoRenderer] GPU interop backend:"
                    << (m_gpuInterop ? m_gpuInterop->name() : "none")
                    << " instanceId=" << property("instanceId").toInt();
        }

        // ── 从三缓冲读取最新帧 ─────────────────────────────────────────────────
        if (m_newFrame.exchange(false, std::memory_order_acq_rel)) {
            int old2 = m_middleIdx.exchange(m_readIdx.load(std::memory_order_relaxed),
                                             std::memory_order_acq_rel);
            m_readIdx.store(old2, std::memory_order_relaxed);
            qInfo() << "[Client][VideoRenderer] ★★★ 三缓冲交换 ★★★"
                    << " instanceId=" << property("instanceId").toInt()
                    << " seq=" << seq
                    << " old2=" << old2 << " readIdx=" << m_readIdx.load()
                    << " ★ 有新帧写入 render slot，渲染即将开始！";
        }

        auto& slot = m_slots[m_readIdx.load(std::memory_order_relaxed)];

        // ── 诊断：三缓冲状态（每60帧）───────────────────────────────────────────
        if (seq % 60 == 0) {
            qInfo() << "[Client][VideoRenderer] ★ 三缓冲状态"
                    << " instanceId=" << property("instanceId").toInt()
                    << " seq=" << seq
                    << " slot.dirty=" << slot.dirty
                    << " slot.frame=" << (bool)slot.frame
                    << " m_newFrame=" << m_newFrame.load()
                    << " writeIdx=" << m_writeIdx.load()
                    << " middleIdx=" << m_middleIdx.load()
                    << " readIdx=" << m_readIdx.load();
        }

        // ════════════════════════════════════════════════════════════════════
        // 核心分支：有有效帧 → 渲染视频
        // ════════════════════════════════════════════════════════════════════
        if (slot.dirty && slot.frame) {
            qInfo() << "[Client][VideoRenderer] ★★★ 渲染帧 ★★★"
                    << " instanceId=" << property("instanceId").toInt()
                    << " title=" << property("title").toString()
                    << " seq=" << seq
                    << " frameId=" << slot.frame->frameId
                    << " frame=" << slot.frame->width << "x" << slot.frame->height
                    << " pixelFormat=" << (int)slot.frame->pixelFormat
                    << " memoryType=" << (int)slot.frame->memoryType
                    << " ★★★ 视频即将显示！★★★";

            // 复用或创建 VideoSGNode
            VideoSGNode* node = static_cast<VideoSGNode*>(old);
            const bool nodeJustCreated = (node == nullptr);

            if (!node) {
                node = new VideoSGNode();
                node->setGpuInterop(m_gpuInterop.get());
                m_hasRealNode.store(true);
                qInfo() << "[Client][VideoRenderer] ★★★ VideoSGNode 新建 ★★★"
                        << " instanceId=" << property("instanceId").toInt()
                        << " node=" << (void*)node
                        << " gpuInterop=" << (m_gpuInterop ? m_gpuInterop->name() : "NONE")
                        << " glCtx=" << (void*)QOpenGLContext::currentContext()
                        << " ★★★ GPU 纹理即将创建，视频渲染开始！★★★";
            }

            // 渲染帧
            try {
                node->updateGeometry(QRectF(0, 0, width(), height()), m_mirrorH);
                node->updateFrame(*slot.frame);

                qInfo() << "[Client][VideoRenderer] ★★★ node->updateFrame 完成 ★★★"
                        << " instanceId=" << property("instanceId").toInt()
                        << " title=" << property("title").toString()
                        << " seq=" << seq << " frameId=" << slot.frame->frameId
                        << " node=" << (void*)node
                        << " justCreated=" << nodeJustCreated
                        << " ★★★ 视频帧已上传到 GPU，理论上应该显示！★★★";
            } catch (const std::exception& e) {
                qCritical() << "[Client][VideoRenderer][ERROR] updateFrame 异常:"
                           << e.what() << " w=" << width() << " h=" << height();
            }

            slot.dirty = false;

            // 延迟统计
            if (slot.frame->captureTimestamp > 0) {
                const double latency = static_cast<double>(
                    TimeUtils::wallClockMs() - slot.frame->captureTimestamp);
                m_latencyMs.store(latency);
                try { emit latencyChanged(latency); } catch (...) {}
            }

            // FPS 统计
            ++m_frameCount;
            const int64_t fpsNow = TimeUtils::wallClockMs();
            if (fpsNow - m_fpsWindowStart >= 1000) {
                const double fps = static_cast<double>(m_frameCount) / ((fpsNow - m_fpsWindowStart) / 1000.0);
                m_fps.store(fps);
                m_frameCount = 0;
                m_fpsWindowStart = fpsNow;
                emit fpsChanged(fps);
                qInfo() << "[Client][VideoRenderer] ★★★ FPS emit ★★★ fps=" << fps
                        << " instanceId=" << property("instanceId").toInt()
                        << " window=" << (void*)window() << " frameId=" << slot.frame->frameId
                        << " ★★★ 渲染帧率正常，视频应持续显示！★★★";
            }

            // ★★★ 关键：永不返回 nullptr！★★★
            return node;
        }

        // ════════════════════════════════════════════════════════════════════
        // 核心分支：无有效帧 → 返回 Placeholder（永不返回 nullptr）
        // ════════════════════════════════════════════════════════════════════
        if (seq <= 10 || seq % 30 == 0) {
            qInfo() << "[Client][VideoRenderer] ★ 无有效帧 seq=" << seq
                    << " instanceId=" << property("instanceId").toInt()
                    << " slot.dirty=" << slot.dirty << " slot.frame=" << (bool)slot.frame
                    << " old=" << (void*)old << " m_hasRealNode=" << m_hasRealNode.load();
        }

        // 分支1：有旧 VideoSGNode（显示最后一帧）
        if (old && dynamic_cast<VideoSGNode*>(old)) {
            VideoSGNode* node = static_cast<VideoSGNode*>(old);
            if (seq <= 10 || seq % 30 == 0) {
                qInfo() << "[Client][VideoRenderer] ★ 返回旧 VideoSGNode（显示最后一帧）"
                        << " instanceId=" << property("instanceId").toInt()
                        << " seq=" << seq << " node=" << (void*)node;
            }
            node->updateGeometry(QRectF(0, 0, width(), height()), m_mirrorH);
            return node;
        }

        // 分支2：无 VideoSGNode，使用 Placeholder
        if (!m_placeholderNode) {
            m_placeholderNode = createPlaceholderNode();
            qInfo() << "[Client][VideoRenderer] ★★★ Placeholder Node 创建 ★★★"
                    << " instanceId=" << property("instanceId").toInt()
                    << " seq=" << seq
                    << " node=" << (void*)m_placeholderNode
                    << " 防止 Scene Graph 降级为 static";
        }

        updatePlaceholderGeometry(m_placeholderNode, QRectF(0, 0, width(), height()));

        if (seq <= 10 || seq % 30 == 0) {
            qInfo() << "[Client][VideoRenderer] ★ 返回 Placeholder Node（黑色背景）"
                    << " instanceId=" << property("instanceId").toInt()
                    << " seq=" << seq << " node=" << (void*)m_placeholderNode
                    << " size=" << width() << "x" << height();
        }

        // ★★★ 关键：永不返回 nullptr！★★★
        return m_placeholderNode;

    } catch (const std::exception& e) {
        qCritical() << "[VideoRenderer][ERROR] updatePaintNode 总异常:" << e.what();
        if (!m_placeholderNode) {
            m_placeholderNode = createPlaceholderNode();
        }
        return m_placeholderNode;
    } catch (...) {
        qCritical() << "[VideoRenderer][ERROR] updatePaintNode 未知异常";
        if (!m_placeholderNode) {
            m_placeholderNode = createPlaceholderNode();
        }
        return m_placeholderNode;
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

    startPollingTimer();

    qInfo() << "[Client][VideoRenderer] componentComplete"
            << " instanceId=" << property("instanceId").toInt()
            << " title=" << property("title").toString()
            << " visible=" << isVisible()
            << " size=" << width() << "x" << height()
            << " window=" << (void*)window()
            << " windowThread=" << (void*)(window() ? window()->thread() : nullptr)
            << " currentThread=" << (void*)QThread::currentThreadId()
            << " ★★★ 组件完成，polling timer 已启动 ★★★";
}

void VideoRenderer::updateFpsAndLatency()
{
    // Called internally; FPS/latency updated in deliverFrame/updatePaintNode
}

// ═══════════════════════════════════════════════════════════════════════════════
// forceRefresh：方案 C 核心实现
// 对话框打开/关闭时由 QML 调用，强制触发渲染线程直接刷新
// ═══════════════════════════════════════════════════════════════════════════════
void VideoRenderer::forceRefresh()
{
    const int64_t now = TimeUtils::wallClockMs();
    const int64_t msSinceLastPN = now - m_lastPaintNodeTime.load();
    const int pendingFrames = m_pendingFramesCount.load();
    const int fc = m_forceRefreshCount.fetch_add(1);

    qInfo() << "[Client][VideoRenderer] ★★★ forceRefresh 被调用 ★★★"
            << " instanceId=" << property("instanceId").toInt()
            << " title=" << property("title").toString()
            << " callSeq=" << fc
            << " msSinceLastPaint=" << msSinceLastPN
            << " pendingFrames=" << pendingFrames
            << " totalDeliver=" << m_totalDeliverCount.load()
            << " isVisible=" << isVisible()
            << " window=" << (void*)window()
            << " componentComplete=" << isComponentComplete()
            << " callingThread=" << (void*)QThread::currentThreadId()
            << " windowThread=" << (void*)(window() ? window()->thread() : nullptr)
            << " m_updatePaintNodeSeq=" << m_updatePaintNodeSeq
            << " m_renderThreadRefreshSucceeded=" << m_renderThreadRefreshSucceeded.load()
            << " m_polishAndUpdateSucceeded=" << m_polishAndUpdateSucceeded.load()
            << " m_fallbackUpdateSucceeded=" << m_fallbackUpdateSucceeded.load()
            << " ★ 方案C：优先使用渲染线程直接刷新，绕过主线程事件循环 ★";

    // 诊断：如果距上次渲染超过阈值，发射阻塞信号
    if (msSinceLastPN > RENDER_STALL_TIMEOUT_MS) {
        qWarning() << "[Client][VideoRenderer][STALL] ★★★ 渲染线程疑似阻塞 ★★★"
                    << " instanceId=" << property("instanceId").toInt()
                    << " msSinceLastPaint=" << msSinceLastPN
                    << " pendingFrames=" << pendingFrames
                    << " threshold=" << RENDER_STALL_TIMEOUT_MS << "ms"
                    << " m_updatePaintNodeSeq=" << m_updatePaintNodeSeq
                    << " ★ 对话框关闭后调用 forceRefresh，检测渲染线程是否恢复 ★";
        emit renderThreadStalled(pendingFrames, msSinceLastPN);
    }

    // ── 方案 C：向渲染线程直接发送刷新请求 ───────────────────────────────────
    // 这是与旧方案的本质区别：
    // 旧方案：polish() + window()->update() → postEvent 到主线程 → 主线程被阻塞时完全无效
    // 方案 C：QMetaMethod::invoke + Qt::QueuedConnection → 投递到渲染线程事件队列
    //         → 渲染线程有独立事件循环 → modal 对话框阻塞主线程时仍能响应
    QQuickWindow* win = window();
    if (win) {
        // ★★★ 策略 1：polishAndUpdate()（Qt 内部方法，直接驱动渲染循环）★★★
        // 这是 Qt 内部用于驱动 Scene Graph 的核心方法
        const QMetaMethod polishAndUpdateMethod = win->metaObject()->method(
            win->metaObject()->indexOfMethod("polishAndUpdate()"));

        bool refreshed = false;
        if (polishAndUpdateMethod.isValid()) {
            // Qt::QueuedConnection：跨线程调用，自动投递到目标线程（渲染线程）的事件队列
            refreshed = polishAndUpdateMethod.invoke(win, Qt::QueuedConnection);
            if (refreshed) {
                m_polishAndUpdateSucceeded.fetch_add(1);
                qInfo() << "[Client][VideoRenderer][方案C] ★★★ polishAndUpdate() 跨线程刷新成功 ★★★"
                        << " instanceId=" << property("instanceId").toInt()
                        << " callSeq=" << fc
                        << " callingThread=" << (void*)QThread::currentThreadId()
                        << " winThread=" << (void*)win->thread()
                        << " pendingFrames=" << pendingFrames;
            } else {
                qWarning() << "[Client][VideoRenderer][方案C] polishAndUpdate() 返回 false"
                           << " instanceId=" << property("instanceId").toInt()
                           << " callSeq=" << fc;
            }
        } else {
            qWarning() << "[Client][VideoRenderer][方案C] polishAndUpdate() 方法未找到"
                       << " instanceId=" << property("instanceId").toInt()
                       << " callSeq=" << fc;
        }

        // ★★★ 策略 2：QMetaMethod 直接调用 update()（次优）★★★
        if (!refreshed) {
            const QMetaMethod updateMethod = win->metaObject()->method(
                win->metaObject()->indexOfMethod("update()"));
            if (updateMethod.isValid()) {
                refreshed = updateMethod.invoke(win, Qt::QueuedConnection);
                if (refreshed) {
                    m_windowMetaMethodSucceeded.fetch_add(1);
                    qInfo() << "[Client][VideoRenderer][方案C] ★ update() QMetaMethod 跨线程刷新成功 ★"
                            << " instanceId=" << property("instanceId").toInt()
                            << " callSeq=" << fc;
                }
            }
        }

        // ★★★ 策略 3：fallback（对话框打开时可能无效）★★★
        if (!refreshed) {
            polish();
            win->update();
            m_fallbackUpdateSucceeded.fetch_add(1);
            qWarning() << "[Client][VideoRenderer][方案C] 使用 fallback polish()+update()"
                       << " instanceId=" << property("instanceId").toInt()
                       << " callSeq=" << fc
                       << " ★ 对话框打开时可能无效，参考 msSinceLastPN 确认 ★";
        }

        // ★★★ 立即触发渲染线程直接刷新（最激进策略）★★★
        // 这会在 deliverFrame 检测到阻塞时额外调用
        // 在 forceRefresh 中也调用，确保对话框打开/关闭时都能触发
        if (pendingFrames > 0 || msSinceLastPN > RENDER_STALL_TIMEOUT_MS) {
            renderThreadRefreshImpl();
        }

        qInfo() << "[Client][VideoRenderer] forceRefresh: 刷新策略已执行"
                << " instanceId=" << property("instanceId").toInt()
                << " callSeq=" << fc
                << " refreshed=" << refreshed
                << " totalPolished=" << m_polishAndUpdateSucceeded.load()
                << " totalFallback=" << m_fallbackUpdateSucceeded.load();
    } else {
        qWarning() << "[Client][VideoRenderer] forceRefresh: window() 返回 nullptr，无法触发更新"
                    << " instanceId=" << property("instanceId").toInt()
                    << " isComponentComplete=" << isComponentComplete()
                    << " parent=" << (void*)parentItem();
    }
}

int64_t VideoRenderer::msSinceLastPaint() const
{
    return TimeUtils::wallClockMs() - m_lastPaintNodeTime.load();
}

// ═══════════════════════════════════════════════════════════════════════════════
// 主动轮询定时器实现（防止 Scene Graph 调度器跳过）
// ═══════════════════════════════════════════════════════════════════════════════
void VideoRenderer::startPollingTimer()
{
    if (m_pollingTimer) {
        return;
    }

    m_pollingTimer = new QTimer(this);
    m_pollingTimer->setInterval(16);  // ~60fps，与渲染帧率同步

    QObject::connect(m_pollingTimer, &QTimer::timeout, this, [this]() {
        const int64_t now = TimeUtils::wallClockMs();
        const int64_t msSinceLastPN = now - m_lastPaintNodeTime.load();
        const int pendingFrames = m_pendingFramesCount.load();

        const bool hasUnrenderedFrame = m_newFrame.load(std::memory_order_acquire);
        const bool renderStalled = (msSinceLastPN > 33 && hasUnrenderedFrame);

        if (renderStalled) {
            QQuickWindow* win = window();
            if (win) {
                // ★★★ 轮询检测到渲染被跳过 → 触发方案 C 渲染线程直接刷新 ★★★
                polish();
                win->update();

                // 同时触发渲染线程直接刷新（绕过主线程阻塞）
                renderThreadRefreshImpl();

                static QAtomicInt s_pollForceCount{0};
                const int pfCount = ++s_pollForceCount;
                if (pfCount <= 20 || pfCount % 60 == 0) {
                    qInfo() << "[Client][VideoRenderer][POLL] ★ 轮询强制刷新 ★"
                            << " instanceId=" << property("instanceId").toInt()
                            << " title=" << property("title").toString()
                            << " msSinceLastPN=" << msSinceLastPN << "ms"
                            << " pendingFrames=" << pendingFrames
                            << " hasUnrenderedFrame=" << hasUnrenderedFrame
                            << " isVisible=" << isVisible()
                            << " pollCount=" << pfCount
                            << " m_updatePaintNodeSeq=" << m_updatePaintNodeSeq
                            << " ★ 方案C：渲染线程直接刷新已触发 ★";
                }
            }
        }

        m_lastPollTime = now;
    });

    m_pollingTimer->start();
    qInfo() << "[Client][VideoRenderer] ★ 轮询定时器启动 ★"
            << " instanceId=" << property("instanceId").toInt()
            << " title=" << property("title").toString()
            << " interval=" << m_pollingTimer->interval() << "ms"
            << " callingThread=" << (void*)QThread::currentThreadId()
            << " ★★★ 轮询将在主线程执行，modal 对话框期间可能无效 → 使用方案C补充 ★★★";
}

void VideoRenderer::stopPollingTimer()
{
    if (m_pollingTimer) {
        m_pollingTimer->stop();
        delete m_pollingTimer;
        m_pollingTimer = nullptr;
        qInfo() << "[Client][VideoRenderer] ★ 轮询定时器停止 ★"
                << " instanceId=" << property("instanceId").toInt();
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// itemChange 事件处理
// ═══════════════════════════════════════════════════════════════════════════════
void VideoRenderer::itemChange(ItemChange change, const ItemChangeData & value)
{
    QQuickItem::itemChange(change, value);

    if (change == ItemVisibleHasChanged) {
        qInfo() << "[Client][VideoRenderer][VISIBLE] ★★★ visible 变化 ★★★"
                << " instanceId=" << property("instanceId").toInt()
                << " title=" << property("title").toString()
                << " visible=" << value.boolValue
                << " isVisible=" << isVisible()
                << " window=" << (void*)window()
                << " callingThread=" << (void*)QThread::currentThreadId();

        // visible 变为 true 时，触发方案 C 强制刷新
        if (value.boolValue) {
            QQuickWindow* win = window();
            if (win) {
                polish();
                win->update();
                renderThreadRefreshImpl();
                qInfo() << "[Client][VideoRenderer][VISIBLE] visible=true，方案C强制刷新"
                        << " instanceId=" << property("instanceId").toInt()
                        << " m_updatePaintNodeSeq=" << m_updatePaintNodeSeq;
            }
        }
    }
}
