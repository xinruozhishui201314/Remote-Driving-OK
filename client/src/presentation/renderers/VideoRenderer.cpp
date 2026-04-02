#include "VideoRenderer.h"
#include "VideoSGNode.h"
#include "../../infrastructure/media/gpu/GpuInteropFactory.h"
#include <QQuickWindow>
#include <QDebug>
#include <QDateTime>
#include <QOpenGLContext>
#include <algorithm>
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
    qInfo() << "[Client][VideoRenderer] created";
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
    if (logSeq <= 10 || logSeq % 30 == 0) {
        qInfo() << "[Client][VideoRenderer] ★★★ deliverFrame 被调用"
                << " seq=" << logSeq << " frameId=" << m_lastFrameId
                << " window=" << (void*)window()
                << " frame=" << frame->width << "x" << frame->height
                << " pixelFormat=" << static_cast<int>(frame->pixelFormat)
                << " memoryType=" << static_cast<int>(frame->memoryType);
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
        if (window()) {
            window()->update();
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

QSGNode* VideoRenderer::updatePaintNode(QSGNode* old, UpdatePaintNodeData*)
{
    // ★★★ 关键诊断：确认 updatePaintNode 被 Qt 渲染线程调用（每秒 ~60 次） ★★★
    static QAtomicInt s_updatePaintNodeCount{0};
    const int seq = ++s_updatePaintNodeCount;
    if (seq <= 10 || seq % 60 == 0) {
        qInfo() << "[Client][VideoRenderer] ★★★ updatePaintNode(Qt渲染线程) 被调用"
                << " seq=" << seq
                << " old=" << (void*)old
                << " width=" << width() << " height=" << height()
                << " isComponentComplete=" << isComponentComplete()
                << " isVisible=" << isVisible()
                << " glCtx=" << (void*)QOpenGLContext::currentContext();
    }

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
        // ★ 关键诊断：无新帧或 slot 无效（每60帧打印一次，避免日志刷屏）
        if (!slot.dirty || !slot.frame) {
            if (seq <= 5 || seq % 60 == 0) {
                qInfo() << "[Client][VideoRenderer] ★ updatePaintNode: 无新帧，跳过渲染"
                        << " seq=" << seq
                        << " slot.dirty=" << slot.dirty
                        << " slot.frame valid=" << (bool)slot.frame
                        << " m_newFrame=" << m_newFrame.load()
                        << " writeIdx=" << m_writeIdx.load()
                        << " readIdx=" << m_readIdx.load();
            }
            return old;
        }

        // ★★★ 关键诊断：开始渲染帧（每60帧打印） ★★★
        if (seq <= 10 || seq % 60 == 0) {
            qInfo() << "[Client][VideoRenderer] ★★★ updatePaintNode: 开始渲染帧"
                    << " seq=" << seq
                    << " frameId=" << slot.frame->frameId
                    << " frame=" << slot.frame->width << "x" << slot.frame->height
                    << " pixelFormat=" << static_cast<int>(slot.frame->pixelFormat)
                    << " memoryType=" << static_cast<int>(slot.frame->memoryType);
        }

        VideoSGNode* node = static_cast<VideoSGNode*>(old);
        if (!node) {
            node = new VideoSGNode();
            node->setGpuInterop(m_gpuInterop.get());
            // ★★★ 关键诊断：VideoSGNode 新建 + GPU interop 状态确认 ★★★
            // 如果这里 interop=none，说明 GpuInteropFactory::create() 失败（检查 GPU 驱动）
            auto* gl = QOpenGLContext::currentContext();
            qInfo() << "[Client][VideoRenderer] ★★★ VideoSGNode 新建"
                    << " node=" << (void*)node
                    << " gpuInterop=" << (m_gpuInterop ? m_gpuInterop->name() : "NONE")
                    << " glContext=" << (void*)gl
                    << " (NONE=CPU-upload fallback 仍可正常显示)";
        }

        // ★ 关键诊断：每60帧打印渲染上下文状态（确认 GL 上下文 + interop 有效性）
        if (seq % 60 == 0) {
            auto* gl = QOpenGLContext::currentContext();
            qInfo() << "[Client][VideoRenderer] ★ updatePaintNode 渲染状态"
                    << " seq=" << seq
                    << " slot.dirty=" << slot.dirty
                    << " glContext=" << (void*)gl
                    << " gpuInterop=" << (m_gpuInterop ? m_gpuInterop->name() : "none");
        }

        try {
            node->updateGeometry(QRectF(0, 0, width(), height()), m_mirrorH);
            node->updateFrame(*slot.frame);
            // ★★★ 关键诊断：每60帧打印帧更新完成 + frameId 端到端追踪 ★★★
            if (seq <= 10 || seq % 60 == 0) {
                qInfo() << "[Client][VideoRenderer] ★★★ updatePaintNode: node->updateFrame 完成"
                        << " seq=" << seq << " frameId=" << slot.frame->frameId
                        << " （对比: setFrame frameId=" << m_lastFrameId
                        << " match=" << (slot.frame->frameId == m_lastFrameId ? "YES" : "NO(正常，三缓冲延迟)") << ")";
            }
        } catch (const std::exception& e) {
            qCritical() << "[Client][VideoRenderer][ERROR] updatePaintNode: node->updateGeometry/updateFrame 异常:"
                       << " w=" << width() << " h=" << height() << " error=" << e.what();
        } catch (...) {
            qCritical() << "[Client][VideoRenderer][ERROR] updatePaintNode: node->updateGeometry/updateFrame 未知异常";
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
