#include "VideoRenderer.h"
#include "VideoSGNode.h"
#include "../../infrastructure/media/gpu/GpuInteropFactory.h"
#include <QQuickWindow>
#include <QDebug>
#include <QDateTime>
#include <QOpenGLContext>
#include <QThread>
#include <QSGGeometry>
#include <QSGFlatColorMaterial>
#include <algorithm>
#include <cstring>
#include <memory>
#include <vector>

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

    // ── 1. 输入有效性检查 ────────────────────────────────────────────────────
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

    // ── 2. 尺寸有效性检查 ────────────────────────────────────────────────────
    if (srcW <= 0 || srcH <= 0) {
        const int cnt = ++s_failSizeCount;
        if (cnt <= 3) {
            qWarning() << "[Client][VideoRenderer][qImageToYuv420] ★ 尺寸无效，跳过"
                       << " call#=" << callSeq << " fail#=" << cnt
                       << " srcW=" << srcW << " srcH=" << srcH;
        }
        return nullptr;
    }

    // ── 3. 超大分辨率检查（防止内存爆炸）─────────────────────────────────────
    if (srcW > 7680 || srcH > 4320) {
        const int cnt = ++s_failOverflowCount;
        if (cnt <= 3) {
            qWarning() << "[Client][VideoRenderer][qImageToYuv420] ★ 分辨率过大，拒绝防止内存爆炸"
                       << " call#=" << callSeq << " fail#=" << cnt
                       << " srcW=" << srcW << " srcH=" << srcH;
        }
        return nullptr;
    }

    // ── 4. 格式转换（关键：处理各种 QImage 格式）─────────────────────────────
    QImage src;
    try {
        // 如果已经是 RGB888，直接使用；否则转换
        if (srcIn.format() == QImage::Format_RGB888) {
            src = srcIn;
        } else if (srcIn.format() == QImage::Format_Grayscale8) {
            // Grayscale8 转换为 RGB888
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
            // 其他格式统一转换为 RGB888
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

    // ── 5. 转换后尺寸有效性检查 ──────────────────────────────────────────────
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

    // ── 6. YUV 缓冲区分配 ────────────────────────────────────────────────────
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

    // ── 7. 图像数据指针检查 ─────────────────────────────────────────────────
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

    // ── 8. RGB → YUV420 转换 ────────────────────────────────────────────────
    try {
        // Y 平面转换（逐像素）
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

        // U/V 平面转换（2x2 块平均）
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

    // ── 9. 构建 VideoFrame ──────────────────────────────────────────────────
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

        // 保持 YUV 数据引用，避免提前释放
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

        // ── 10. 成功日志 ─────────────────────────────────────────────────────
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

VideoRenderer::VideoRenderer(QQuickItem* parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
    m_fpsWindowStart = TimeUtils::wallClockMs();
    m_lastPaintNodeTime.store(m_fpsWindowStart);  // 初始化渲染线程心跳时间
    qInfo() << "[Client][VideoRenderer] created at timestamp=" << m_fpsWindowStart;
}

VideoRenderer::~VideoRenderer() = default;

void VideoRenderer::setFrame(const QImage& image, quint64 frameId)
{
    static QAtomicInt s_callCount{0};
    static QAtomicInt s_nullCount{0};
    static QAtomicInt s_droppedCount{0};
    const int callSeq = ++s_callCount;

    // ★★★ 关键诊断：确认 QML → C++ setFrame 跨语言调用是否成功 ★★★
    // 如果此日志不出现 → QML VideoRenderer.setFrame() 未被调用，检查：
    //   1. Connections { target: streamClient } 绑定是否生效（见 DrivingInterface.qml）
    //   2. WebRtcClient::videoFrameReady 信号是否被 QML 接收（检查 QML console.warn）
    if (callSeq <= 10 || callSeq % 30 == 0) {
        qInfo() << "[Client][VideoRenderer] ★★★ setFrame(QML→C++) 被调用"
                << " call#=" << callSeq
                << " frameId=" << frameId
                << " image.isNull=" << image.isNull()
                << " size=" << image.width() << "x" << image.height()
                << " format=" << static_cast<int>(image.format())
                << " window=" << (void*)window()
                << " componentComplete=" << isComponentComplete();
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

void VideoRenderer::deliverFrame(std::shared_ptr<VideoFrame> frame, quint64 frameId)
{
    if (!frame) return;

    frame->frameId = frameId;  // 写入端到端追踪 ID
    m_lastFrameId = frameId;

    // ★★★ 关键诊断：确认 deliverFrame 被调用 + window 有效性 + frameId 端到端追踪 ★★★
    // 注意：deliverFrame 由 setFrame（主线程）或 MediaPipeline（解码线程）调用
    // window()->update() 在主线程安全，因为 QQuickWindow::update() 可从任意线程调用
    static QAtomicInt s_deliverLogCount{0};
    const int logSeq = ++s_deliverLogCount;
    const int totalDeliver = ++m_totalDeliverCount;
    const int64_t now = TimeUtils::wallClockMs();

    // ── 渲染线程心跳检测（增强日志）────────────────────────────────────────
    // 检测渲染线程是否被阻塞（对话框显示期间常见）
    const int64_t msSinceLastPN = now - m_lastPaintNodeTime.load();
    const int prevPending = m_pendingFramesCount.fetch_add(1);
    const int pendingNow = prevPending + 1;

    // 当 pending frames 超过阈值或距上次渲染超过阈值时，打印警告
    if (pendingNow > MAX_PENDING_FRAMES || msSinceLastPN > RENDER_STALL_TIMEOUT_MS) {
        qWarning() << "[Client][VideoRenderer][HEARTBEAT] ★★★ 渲染线程心跳异常 ★★★"
                    << " logSeq=" << logSeq << " frameId=" << frameId
                    << " pendingFrames=" << pendingNow << " (max=" << MAX_PENDING_FRAMES << ")"
                    << " msSinceLastPaint=" << msSinceLastPN << "ms (threshold=" << RENDER_STALL_TIMEOUT_MS << "ms)"
                    << " totalDeliver=" << totalDeliver
                    << " ★ 渲染线程可能阻塞（对话框显示期间），考虑调用 forceRefresh() ★";
        m_renderStalled.store(true);
        emit renderThreadStalled(pendingNow, msSinceLastPN);
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
            // ★★★ FPS 计算成功 + window 有效性（首10帧打印） ★★★
            if (logSeq <= 10) {
                qInfo() << "[Client][VideoRenderer] ★★★ FPS emit fps=" << fps
                           << " window=" << (void*)window() << " frameId=" << m_lastFrameId;
            }
        }

        // 触发渲染（线程安全）
        QQuickWindow* win = window();
        // ── ★★★ 增强诊断：跨线程调用检测 ★★★ ────────────────────────────
        // 检测 deliverFrame 是否从错误的线程调用
        static Qt::HANDLE s_expectedThreadId = nullptr;
        Qt::HANDLE currentThreadId = QThread::currentThreadId();
        if (!s_expectedThreadId) {
            s_expectedThreadId = currentThreadId;
        }
        const bool isCrossThreadCall = (currentThreadId != s_expectedThreadId);
        if (isCrossThreadCall && logSeq <= 10) {
            qWarning() << "[Client][VideoRenderer][THREAD] ★★★ 跨线程调用检测！★★★"
                       << " logSeq=" << logSeq << " frameId=" << frameId
                       << " expectedThread=" << (void*)s_expectedThreadId
                       << " currentThread=" << (void*)currentThreadId
                       << " ★ 跨线程调用 deliverFrame 可能导致数据竞争！";
        }
        if (win) {
            // ── ★★★ 核心修复：polish() 强制 Scene Graph 在下次渲染周期处理此项 ★★★
            polish();
            win->update();
            // ★★★ 关键诊断：window()->update() 被调用（触发 Qt 渲染线程下次调用 updatePaintNode）★★★
            if (logSeq <= 10) {
                qInfo() << "[Client][VideoRenderer] ★★★ window()->update() 被调用"
                           "（触发渲染线程下次 paint），window=" << (void*)window();
            }
        } else {
            // ★★★ 致命警告：window() 返回 nullptr！视频将完全不显示！★★★
            // 原因：VideoRenderer QQuickItem 未加入 QML 场景图，或 Item 初始化失败
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
// 辅助函数：创建黑色占位符 Placeholder Node
// ═══════════════════════════════════════════════════════════════════════════════
// 作用：当无帧时返回黑色背景节点，防止 Qt Scene Graph 将 VideoRenderer 降级为
// "static" 项。根因详见 VideoRenderer.h §渲染线程饥饿检测。
// Qt 官方行为：updatePaintNode 返回 nullptr 时，Scene Graph 调度器将该 item
// 标记为 static，后续 window()->update() 不再触发 updatePaintNode。
static QSGGeometryNode* createPlaceholderNode() {
    QSGGeometryNode* node = new QSGGeometryNode();
    // 四边形：2个三角形组成矩形，使用简单2D顶点（FlatColorMaterial不需要纹理坐标）
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
    
    // 纯色材质（黑色）
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
    
    // 设置归一化坐标（全屏）
    auto* vertices = geo->vertexDataAsPoint2D();
    vertices[0].x = rect.left();   vertices[0].y = rect.top();
    vertices[1].x = rect.right();  vertices[1].y = rect.top();
    vertices[2].x = rect.left();   vertices[2].y = rect.bottom();
    vertices[3].x = rect.right();  vertices[3].y = rect.bottom();
    
    node->markDirty(QSGNode::DirtyGeometry);
}

// ═══════════════════════════════════════════════════════════════════════════════
// 主渲染函数：updatePaintNode
// ═══════════════════════════════════════════════════════════════════════════════
// 核心原则：永不返回 nullptr
// - 有帧 → 返回 VideoSGNode（渲染视频）
// - 无帧但有旧 VideoSGNode → 返回旧 VideoSGNode（显示最后一帧）
// - 无帧且无旧 VideoSGNode → 返回 Placeholder Node（黑色背景，保持调度）
QSGNode* VideoRenderer::updatePaintNode(QSGNode* old, UpdatePaintNodeData*)
{
    // ── 全局渲染序列号（用于诊断 Scene Graph 调度是否正常）───────────────────
    static QAtomicInt s_updatePaintNodeCount{0};
    const int seq = ++s_updatePaintNodeCount;
    const int totalDeliver = m_totalDeliverCount.load();
    const int64_t now = TimeUtils::wallClockMs();

    // ── 渲染线程心跳：记录本次 updatePaintNode 执行时间 ────────────────────
    // 用于 deliverFrame 检测渲染线程是否被阻塞
    m_lastPaintNodeTime.store(now);

    // 渲染成功时减少 pending frames 计数
    static int s_lastReportedPending = 0;
    const int currentPending = m_pendingFramesCount.exchange(0);
    if (currentPending > 0 && (seq <= 10 || currentPending > s_lastReportedPending)) {
        qInfo() << "[Client][VideoRenderer][HEARTBEAT] ★ 渲染线程恢复 ★"
                << " seq=" << seq << " consumedPending=" << currentPending
                << " msSinceLastPN=" << (now - m_lastPaintNodeTime.load() + (now - m_lastPaintNodeTime.load())) << "ms"
                << " ★ deliverFrame 期间帧已被渲染，消费 pending=" << currentPending;
        s_lastReportedPending = currentPending;
    }
    
    // ── 首次调用诊断 ─────────────────────────────────────────────────────────
    const bool isFirstCall = (seq == 1);
    if (isFirstCall) {
        qInfo() << "[Client][VideoRenderer] ═══════════════════════════════════════"
                << " title=" << property("title").toString()
                << " firstPaint=true width=" << width() << " height=" << height()
                << " isComponentComplete=" << isComponentComplete()
                << " isVisible=" << isVisible()
                << " ═══════════════════════════════════════";
    }
    
    // ── 诊断：渲染线程调用频率（每秒约60次，超过说明 Scene Graph 正常调度）─────
    if (seq <= 20 || seq % 60 == 0) {
        qInfo() << "[Client][VideoRenderer] ★ updatePaintNode"
                << " seq=" << seq << " totalDeliver=" << totalDeliver
                << " old=" << (void*)old
                << " width=" << width() << " height=" << height()
                << " isVisible=" << isVisible()
                << " glCtx=" << (void*)QOpenGLContext::currentContext();
    }
    
    // ── 渲染饥饿检测：deliverFrame 被调用次数 vs updatePaintNode 调用次数 ──────
    // 如果 deliverFrame 计数增长但 updatePaintNode seq 不增长 → Scene Graph 跳过调度
    const int lastPN = m_lastPaintNodeSeqForHungry;
    if (seq != lastPN && lastPN > 0) {
        m_skipCount++;
        if (m_skipCount <= 5 || m_skipCount % 30 == 0) {
            qWarning() << "[Client][VideoRenderer][HUNGRY] ★★★ Scene Graph 跳过检测 ★★★"
                       << " seq=" << seq << " lastRecordedPN=" << lastPN
                       << " skipCount=" << m_skipCount
                       << " totalDeliver=" << totalDeliver
                       << " ★ 持续增长说明 Scene Graph 已跳过此 item！";
        }
    }
    m_lastPaintNodeSeqForHungry = seq;
    
    try {
        // ── 初始化 GPU Interop（首次渲染线程调用时）─────────────────────────────
        if (!m_interopInit) {
            m_gpuInterop = GpuInteropFactory::create();
            m_interopInit = true;
            qInfo() << "[Client][VideoRenderer] GPU interop backend:"
                    << (m_gpuInterop ? m_gpuInterop->name() : "none");
        }
        
        // ── 从三缓冲读取最新帧 ─────────────────────────────────────────────────
        if (m_newFrame.exchange(false, std::memory_order_acq_rel)) {
            int old2 = m_middleIdx.exchange(m_readIdx.load(std::memory_order_relaxed),
                                             std::memory_order_acq_rel);
            m_readIdx.store(old2, std::memory_order_relaxed);
            qInfo() << "[Client][VideoRenderer] ★ 三缓冲交换★ seq=" << seq
                    << " old2=" << old2 << " readIdx=" << m_readIdx.load()
                    << " ★ 有新帧写入 render slot";
        }
        
        auto& slot = m_slots[m_readIdx.load(std::memory_order_relaxed)];
        
        // ── 诊断：三缓冲状态（每60帧）───────────────────────────────────────────
        if (seq % 60 == 0) {
            qInfo() << "[Client][VideoRenderer] ★ 三缓冲状态"
                    << " seq=" << seq
                    << " slot.dirty=" << slot.dirty
                    << " slot.frame=" << (bool)slot.frame
                    << " m_newFrame=" << m_newFrame.load()
                    << " writeIdx=" << m_writeIdx.load()
                    << " middleIdx=" << m_middleIdx.load()
                    << " readIdx=" << m_readIdx.load();
        }
        
        // ════════════════════════════════════════════════════════════════════
        // 核心分支：有有效帧
        // ════════════════════════════════════════════════════════════════════
        if (slot.dirty && slot.frame) {
            qInfo() << "[Client][VideoRenderer] ★★★ 渲染帧 ★★★ seq=" << seq
                    << " frameId=" << slot.frame->frameId
                    << " frame=" << slot.frame->width << "x" << slot.frame->height
                    << " pixelFormat=" << (int)slot.frame->pixelFormat
                    << " memoryType=" << (int)slot.frame->memoryType;
            
            // 复用或创建 VideoSGNode
            VideoSGNode* node = static_cast<VideoSGNode*>(old);
            const bool nodeJustCreated = (node == nullptr);
            
            if (!node) {
                node = new VideoSGNode();
                node->setGpuInterop(m_gpuInterop.get());
                qInfo() << "[Client][VideoRenderer] ★★★ VideoSGNode 新建 ★★★"
                        << " node=" << (void*)node
                        << " gpuInterop=" << (m_gpuInterop ? m_gpuInterop->name() : "NONE")
                        << " glCtx=" << (void*)QOpenGLContext::currentContext();
            }
            
            // 渲染帧
            try {
                node->updateGeometry(QRectF(0, 0, width(), height()), m_mirrorH);
                node->updateFrame(*slot.frame);
                
                qInfo() << "[Client][VideoRenderer] ★★★ node->updateFrame 完成 ★★★"
                        << " seq=" << seq << " frameId=" << slot.frame->frameId
                        << " node=" << (void*)node
                        << " justCreated=" << nodeJustCreated;
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
            const int64_t now = TimeUtils::wallClockMs();
            if (now - m_fpsWindowStart >= 1000) {
                const double fps = static_cast<double>(m_frameCount) / ((now - m_fpsWindowStart) / 1000.0);
                m_fps.store(fps);
                m_frameCount = 0;
                m_fpsWindowStart = now;
                emit fpsChanged(fps);
                qInfo() << "[Client][VideoRenderer] ★★★ FPS emit ★★★ fps=" << fps
                        << " window=" << (void*)window() << " frameId=" << slot.frame->frameId;
            }
            
            // ★★★ 关键：永不返回 nullptr！★★★
            return node;
        }
        
        // ════════════════════════════════════════════════════════════════════
        // 核心分支：无有效帧
        // 目标：永不返回 nullptr，防止 Scene Graph 降级
        // ════════════════════════════════════════════════════════════════════
        qInfo() << "[Client][VideoRenderer] ★ 无有效帧 seq=" << seq
                << " slot.dirty=" << slot.dirty << " slot.frame=" << (bool)slot.frame
                << " old=" << (void*)old << " m_hasRealNode=" << m_hasRealNode.load();
        
        // 分支1：有旧 VideoSGNode（显示最后一帧）
        if (old && dynamic_cast<VideoSGNode*>(old)) {
            VideoSGNode* node = static_cast<VideoSGNode*>(old);
            if (seq <= 10 || seq % 30 == 0) {
                qInfo() << "[Client][VideoRenderer] ★ 返回旧 VideoSGNode（显示最后一帧）"
                        << " seq=" << seq << " node=" << (void*)node;
            }
            // 更新几何信息（尺寸可能变化）
            node->updateGeometry(QRectF(0, 0, width(), height()), m_mirrorH);
            // ★★★ 关键：永不返回 nullptr！返回旧 node 显示最后一帧
            return node;
        }
        
        // 分支2：无 VideoSGNode，使用 Placeholder
        if (!m_placeholderNode) {
            m_placeholderNode = createPlaceholderNode();
            qInfo() << "[Client][VideoRenderer] ★★★ Placeholder Node 创建 ★★★"
                    << " seq=" << seq
                    << " node=" << (void*)m_placeholderNode
                    << " 防止 Scene Graph 降级为 static";
        }
        
        // 更新 Placeholder 几何
        updatePlaceholderGeometry(m_placeholderNode, QRectF(0, 0, width(), height()));
        
        if (seq <= 10 || seq % 30 == 0) {
            qInfo() << "[Client][VideoRenderer] ★ 返回 Placeholder Node（黑色背景）"
                    << " seq=" << seq << " node=" << (void*)m_placeholderNode
                    << " size=" << width() << "x" << height();
        }
        
        // ★★★ 关键：永不返回 nullptr！★★★
        return m_placeholderNode;
        
    } catch (const std::exception& e) {
        qCritical() << "[VideoRenderer][ERROR] updatePaintNode 总异常:" << e.what();
        // 即使异常也返回 placeholder
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
}

void VideoRenderer::updateFpsAndLatency()
{
    // Called internally; FPS/latency updated in deliverFrame/updatePaintNode
}

// ═══════════════════════════════════════════════════════════════════════════════
// 强制刷新机制（方案1）
// 根因：Qt Scene Graph 在 VehicleSelectionDialog 显示期间可能阻塞渲染线程，
// 导致 deliverFrame 收到帧但 updatePaintNode 不被调用。
// 修复：在对话框关闭时强制刷新所有 VideoRenderer。
// ═══════════════════════════════════════════════════════════════════════════════
void VideoRenderer::forceRefresh()
{
    const int64_t now = TimeUtils::wallClockMs();
    const int64_t msSinceLastPN = now - m_lastPaintNodeTime.load();
    const int pendingFrames = m_pendingFramesCount.load();

    qInfo() << "[Client][VideoRenderer] ★★★ forceRefresh 被调用 ★★★"
            << " title=" << property("title").toString()
            << " msSinceLastPaint=" << msSinceLastPN
            << " pendingFrames=" << pendingFrames
            << " totalDeliver=" << m_totalDeliverCount.load()
            << " isVisible=" << isVisible()
            << " window=" << (void*)window()
            << " componentComplete=" << isComponentComplete();

    // 诊断：如果距离上次渲染超过阈值，发射阻塞信号
    if (msSinceLastPN > RENDER_STALL_TIMEOUT_MS) {
        qWarning() << "[Client][VideoRenderer][STALL] ★★★ 渲染线程疑似阻塞 ★★★"
                    << " msSinceLastPaint=" << msSinceLastPN
                    << " pendingFrames=" << pendingFrames
                    << " threshold=" << RENDER_STALL_TIMEOUT_MS << "ms"
                    << " ★ 对话框关闭后立即调用 forceRefresh，检测渲染线程是否恢复 ★";
        emit renderThreadStalled(pendingFrames, msSinceLastPN);
    }

    // 强制触发 Scene Graph 调度
    QQuickWindow* win = window();
    if (win) {
        polish();
        win->update();
        qInfo() << "[Client][VideoRenderer] forceRefresh: 已调用 polish() + window()->update()"
                << " window=" << (void*)win;
    } else {
        qWarning() << "[Client][VideoRenderer] forceRefresh: window() 返回 nullptr，无法触发更新"
                    << " isComponentComplete=" << isComponentComplete()
                    << " parent=" << (void*)parentItem();
    }
}

int64_t VideoRenderer::msSinceLastPaint() const
{
    return TimeUtils::wallClockMs() - m_lastPaintNodeTime.load();
}

// ═══════════════════════════════════════════════════════════════════════════════
// 增强 deliverFrame：渲染线程心跳检测
// ═══════════════════════════════════════════════════════════════════════════════
// Qt Scene Graph 在正常情况下会以 ~60fps 调用 updatePaintNode。
// 当对话框（QDialog/Popup）显示时，Qt 可能阻塞或暂停渲染线程。
// 检测：当 deliverFrame 被调用但渲染线程超过阈值未响应时，
// 说明渲染线程可能被阻塞，此时发射 renderThreadStalled 信号。
// 注意：这个检测在 deliverFrame 中进行（可能在解码线程或主线程），
// 而 updatePaintNodeTime 在渲染线程更新，两者的时间差即为渲染线程延迟。
// ═══════════════════════════════════════════════════════════════════════════════

// 修改 deliverFrame 开头部分，添加心跳检测逻辑
// 在 deliverFrame 函数内，统计 pending frames 并检测阻塞
