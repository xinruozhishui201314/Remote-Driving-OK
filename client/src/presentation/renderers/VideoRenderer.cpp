/**
 * ═══════════════════════════════════════════════════════════════════════════════════
 * Qt 6 视频渲染器 — 四路视频流完整渲染链路文档
 * ═══════════════════════════════════════════════════════════════════════════════════
 *
 * 【四路视频流完整链路（8个步骤）】
 *
 *  Step 1: RTP 包到达（libdatachannel 工作线程）
 *    代码：WebRtcClient::setupVideoDecoder() → m_videoTrack->onMessage()
 *    日志：[Client][WebRTC][RTP-Arrival] ★★★ RTP包到达(libdatachannel工作线程) ★★★
 *          [Client][WebRTC][RTP-Queue] ★ feedRtp ENTER(主线程) ★
 *
 *  Step 2: H264 解码（主线程）
 *    代码：H264Decoder::feedRtp() → avcodec_send_packet() / avcodec_receive_frame()
 *    日志：[H264][emit] ★★★ emitDecodedFrames ENTER ★★★
 *          [H264][feedRtp] ★★★ feedRtp ENTER ★★★
 *
 *  Step 3: 解码帧发射（主线程）
 *    代码：H264Decoder::emitDecodedFrames() → emit frameReady(QImage, frameId)
 *    日志：[H264][emit] ★★★ emit frameReady 完成 ★★★ queuedConnections>0 → 链路完整
 *
 *  Step 4: WebRtcClient 接收（主线程，QueuedConnection）
 *    代码：WebRtcClient::onVideoFrameFromDecoder()
 *    日志：[Client][WebRTC][emitDone] ★★★ 直接调用成功 ★★★ frameId=xxx
 *          [Client][WebRTC][DirectCall] ★★★ 直接调用成功 ★★★
 *
 *  Step 5: C++ 直接调用（主线程，QMetaMethod::invoke QueuedConnection）
 *    代码：WebRtcClient::onVideoFrameFromDecoder() → VideoRenderer::setFrame()
 *    日志：[Client][VideoRenderer] ★★★ setFrame(QML→C++) 被调用 ★★★ frameId=xxx
 *          [Client][VideoRenderer] qImageToYuv420Frame format 转换 call#=xxx
 *
 *  Step 6: 三缓冲写入（任意线程，原子操作）
 *    代码：VideoRenderer::deliverFrame() → 三缓冲原子交换
 *    日志：[Client][VideoRenderer] deliverFrame 被调用 seq=xxx frameId=xxx
 *
 *  Step 7: 渲染触发（Qt 6 兼容方案：scheduleRenderJob）
 *    代码：deliverFrame() → triggerRenderRefresh() → QQuickWindow::scheduleRenderJob()
 *    日志：[Client][VideoRenderer][Refresh] ★★★ scheduleRenderJob 已投递 ★★★
 *          [Client][VideoRenderer][RenderJob] ★ 渲染线程收到刷新请求 ★
 *          [Client][VideoRenderer][方案C] renderThreadRefreshImpl 被调用 ★★★
 *
 *  Step 8: Qt Scene Graph 渲染（Qt Render Thread）
 *    代码：VideoRenderer::updatePaintNode() → VideoSGNode::updateFrame()
 *    日志：[Client][VideoRenderer] ★★★ 三缓冲交换 ★★★
 *          [Client][VideoRenderer] ★★★ 渲染帧 ★★★ instanceId=x seq=x frameId=xxx
 *          [Client][VideoRenderer] ★★★ VideoSGNode 新建 ★★★
 *          [Client][VideoRenderer] ★★★ node->updateFrame 完成 ★★★
 *
 * ═══════════════════════════════════════════════════════════════════════════════════
 *
 * 【根因分析：为什么四路视频流都不显示？】
 *
 * 根本原因（已在日志中确认）：
 * 1. 四路视频流在 ZLM/CARLA 端正常发送 RTP 包
 * 2. cam_left/rear/right 的 PeerConnection Connected 成功
 * 3. H264Decoder 解码正常（emitDecodedFrames 被调用，emit frameReady 成功）
 * 4. onVideoFrameFromDecoder 收到帧（emit videoFrameReady 信号）
 * 5. VideoRenderer.setFrame() 被调用（前端 QML log 有 "setFrame done"）
 * 6. VideoRenderer.deliverFrame() 被调用（totalDeliver 增长到 1900+）
 * 7. updatePaintNode 只在初始化时各调用 1 次（seq=1），之后再也不调用
 * 8. 三缓冲交换日志从未出现
 *
 * 直接原因：
 * VehicleSelectionDialog modal=true 期间阻塞主线程事件循环。
 * window()->update() 向主线程事件队列投递 QEvent::UpdateRequest，
 * 但主线程被 modal 对话框阻塞，UpdateRequest 永远得不到处理。
 *
 * ═══════════════════════════════════════════════════════════════════════════════════
 *
 * 【Qt 6 兼容修复方案】
 *
 * Qt 5：window()->polishAndUpdate() 是内部 API，但可通过 QMetaMethod::invoke 调用。
 * Qt 6：polishAndUpdate() 不再可调用（不是公开 API）。
 *
 * Qt 6 正确做法：
 *   QQuickWindow::scheduleRenderJob(QRunnable*, QQuickWindow::BeforeSynchronizingStage)
 *
 * 效果：
 *   → 任务被投递到 Qt Render Thread 的事件队列
 *   → Render Thread 有独立事件循环，不受主线程 modal 对话框阻塞影响
 *   → 即使 VehicleSelectionDialog 打开，渲染线程仍能正常响应刷新请求
 *
 * 关键区别：
 *   旧方案（Qt 5兼容）：QMetaMethod::invoke(window, "polishAndUpdate()") → Qt 6 中方法不存在
 *   新方案（Qt 6原生）：scheduleRenderJob() → Qt 公开 API，渲染线程独立事件循环
 *
 * ═══════════════════════════════════════════════════════════════════════════════════
 *
 * 【三缓冲架构】
 *
 * VideoRenderer 使用三缓冲（Triple Buffer）实现无锁帧传递：
 *   - 写入线程（解码器）：写入 writeIdx，不阻塞
 *   - 读取线程（渲染线程）：读取 readIdx，不阻塞
 *   - 中间缓冲 middleIdx：用于原子交换
 *
 * 原子操作序列：
 *   1. 新帧写入 slots[writeIdx]
 *   2. middleIdx 与 writeIdx 原子交换
 *   3. writeIdx 更新为原 middleIdx
 *   4. m_newFrame 置 true → 通知渲染线程有新帧
 *
 * ═══════════════════════════════════════════════════════════════════════════════════
 */
#include "VideoRenderer.h"
#include <QRunnable>
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
#include <QEvent>
#include <algorithm>
#include <cstring>
#include <memory>
#include <vector>
#include <QtGlobal>  // for quint64 (magic signature type)

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
            // ★★★ YUVDiag：RGB888 格式无需转换（最理想格式）★★★
            if (callSeq <= 5) {
                qInfo() << "[Renderer][YUVDiag] RGB888 格式无需转换"
                        << " call#=" << callSeq << " size=" << srcW << "x" << srcH;
            }
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
// RenderRefreshRunnable：向渲染线程投递刷新的 QRunnable（Qt 6 兼容方案）
// Qt 6 中 QQuickWindow::polishAndUpdate() 不是公开 API，无法通过 QMetaMethod::invoke 调用。
// 正确做法：用 QQuickWindow::scheduleRenderJob() 向渲染线程投递任务。
// 渲染线程有独立事件循环，modal 对话框阻塞主线程时仍能响应。
//
// 执行流程：
//   deliverFrame / forceRefresh → scheduleRenderJob → 渲染线程事件循环 → run() → window()->update() → updatePaintNode()
// ═══════════════════════════════════════════════════════════════════════════════
class RenderRefreshRunnable : public QRunnable {
public:
    explicit RenderRefreshRunnable(QQuickWindow* win, int rendererInstanceId, int64_t msSinceLastPaint, int pendingFrames)
        : m_window(win), m_instanceId(rendererInstanceId)
        , m_msSinceLastPaint(msSinceLastPaint), m_pendingFrames(pendingFrames)
    {}

    void run() override {
        if (!m_window) return;

        // 全部操作在渲染线程中执行（update() 是线程安全的）
        m_window->update();

        if (++s_logCount <= 10 || (s_logCount % 60 == 0)) {
            qInfo() << "[Client][VideoRenderer][RenderJob] ★ 渲染线程收到刷新请求 ★"
                     << " instanceId=" << m_instanceId
                     << " window=" << (void*)m_window.data()
                     << " msSinceLastPN=" << m_msSinceLastPaint << "ms"
                     << " pendingFrames=" << m_pendingFrames
                     << " logCount=" << int(s_logCount)
                     << " ★ window()->update() 已投递，下次渲染循环将触发 updatePaintNode ★";
        }
    }

private:
    QPointer<QQuickWindow> m_window;
    int m_instanceId;
    int64_t m_msSinceLastPaint;
    int m_pendingFrames;
    static QAtomicInt s_logCount;
};

QAtomicInt RenderRefreshRunnable::s_logCount{0};

// ═══════════════════════════════════════════════════════════════════════════════
// triggerRenderRefresh：Qt 6 兼容的渲染触发函数
// 使用 QQuickWindow::scheduleRenderJob() 向渲染线程投递刷新任务，
// 绕过主线程阻塞（modal 对话框打开时仍有效）。
// ═══════════════════════════════════════════════════════════════════════════════
static void triggerRenderRefresh(QQuickWindow* win, int instanceId, int64_t msSinceLastPaint, int pendingFrames) {
    if (!win) return;
    static QAtomicInt s_triggerCount{0};
    const int seq = ++s_triggerCount;

    // Qt 6 正确做法：向渲染线程投递任务
    win->scheduleRenderJob(
        new RenderRefreshRunnable(win, instanceId, msSinceLastPaint, pendingFrames),
        QQuickWindow::BeforeSynchronizingStage);

    if (seq <= 5 || seq % 60 == 0) {
        qInfo() << "[Client][VideoRenderer][Refresh] ★★★ scheduleRenderJob 已投递 ★★★"
                 << " seq=" << seq << " instanceId=" << instanceId
                 << " window=" << (void*)win
                 << " winThread=" << (void*)win->thread()
                 << " msSinceLastPN=" << msSinceLastPaint << "ms"
                 << " pendingFrames=" << pendingFrames
                 << " ★ BeforeSynchronizingStage → 渲染线程下次同步前执行 ★";
    }
}

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
void VideoRenderer::setFrame(const QImage& image, quint64 frameId, quint64 lifecycleId)
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
        // ★★★ 帧处理链路追踪：setFrame 入口 ★★
        // 此日志用于区分「帧未到达」和「帧到达但被丢弃」两类问题
        qInfo() << "[Renderer][FrameDiag] setFrame"
                << " call#=" << callSeq << " frameId=" << frameId
                << " image.isNull=" << image.isNull()
                << " image.format=" << static_cast<int>(image.format())
                << " image.size=" << image.width() << "x" << image.height()
                << " callingThread=" << (void*)QThread::currentThreadId()
                << " renderThread=" << (void*)this->thread()
                << " componentComplete=" << isComponentComplete()
                << " window=" << (void*)window();

        // ── v3 新增：在 frame 中记录 lifecycleId（端到端追踪）──────────────
        if (lifecycleId > 0) {
            if (callSeq <= 5) {
                qInfo() << "[Renderer][FrameDiag] setFrame lifecycleId=" << lifecycleId
                        << " frameId=" << frameId;
            }
        }

        std::shared_ptr<VideoFrame> frame = qImageToYuv420Frame(image);
        if (!frame) {
            const int dropSeq = ++s_droppedCount;
            if (dropSeq <= 10 || dropSeq % 30 == 0) {
                qWarning() << "[Client][VideoRenderer][WARN] setFrame: qImageToYuv420Frame 返回空帧，跳过"
                           << " call#=" << callSeq << " drop#=" << dropSeq << " frameId=" << frameId
                           << " lifecycleId=" << lifecycleId
                           << " image.isNull=" << image.isNull()
                           << " size=" << image.width() << "x" << image.height();
            }
            return;
        }
        // ── v3 新增：在 frame 中记录 lifecycleId（端到端追踪）──────────────
        frame->lifecycleId = lifecycleId;
        m_lastFrameId = frameId;
        deliverFrame(std::move(frame), frameId, lifecycleId);
    } catch (const std::exception& e) {
        qCritical() << "[Client][VideoRenderer][ERROR] setFrame 总异常:" << e.what()
                   << " image.size=" << image.size() << " frameId=" << frameId
                   << " lifecycleId=" << lifecycleId;
    } catch (...) {
        qCritical() << "[Client][VideoRenderer][ERROR] setFrame 未知异常 image.size=" << image.size()
                    << " frameId=" << frameId << " lifecycleId=" << lifecycleId;
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// deliverFrame：三缓冲写入 + 触发渲染（可在任意线程调用）
// ═══════════════════════════════════════════════════════════════════════════════
void VideoRenderer::deliverFrame(std::shared_ptr<VideoFrame> frame, quint64 frameId, quint64 lifecycleId)
{
    if (!frame) return;

    frame->frameId = frameId;
    frame->lifecycleId = lifecycleId;  // v3：保留端到端追踪 ID
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
                    << " ★ Qt 6 scheduleRenderJob 将尝试绕过主线程阻塞 ★";
        m_renderStalled.store(true);
        emit renderThreadStalled(pendingNow, msSinceLastPN);

        // ── ★★★ Qt 6 兼容方案：渲染线程直接刷新 ★★★ ─────────────────────────
        // 当检测到渲染线程阻塞时，立即通过 scheduleRenderJob 触发刷新
        // 绕过主线程事件循环（modal 对话框阻塞时 window()->update() 完全失效）
        renderThreadRefreshImpl();

        // ── ★★★ 额外修复：直接调用 QQuickItem::update() ★★★
        // scheduleRenderJob 可能在某些 Qt 6 版本中被 Scene Graph 优化跳过。
        // 直接调用 QQuickItem::update() 显式标记此项目需要重绘，
        // 确保 Scene Graph 在下次渲染循环时重新调用 updatePaintNode。
        update();
        if (logSeq <= 5) {
            qInfo() << "[Client][VideoRenderer] ★★★ 直接调用 update() 标记重绘 ★★★"
                    << " seq=" << logSeq << " frameId=" << frameId
                    << " msSinceLastPN=" << msSinceLastPN << "ms";
        }
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

        // ★★★ 帧处理链路追踪：deliverFrame 入口 ★★
        qInfo() << "[Renderer][FrameDiag] deliverFrame"
                << " seq=" << logSeq << " frameId=" << frameId
                << " lifecycleId=" << frame->lifecycleId
                << " window=" << (void*)window()
                << " frame=" << frame->width << "x" << frame->height
                << " pixelFormat=" << static_cast<int>(frame->pixelFormat)
                << " memoryType=" << static_cast<int>(frame->memoryType)
                << " msSinceLastPN=" << msSinceLastPN << "ms"
                << " pendingFrames=" << pendingNow;

        try {
            // 三缓冲原子写（无锁，可在解码线程调用）
            const int writeIdx = m_writeIdx.load(std::memory_order_relaxed);
            m_slots[writeIdx].frame = std::move(frame);
            m_slots[writeIdx].dirty = true;

            int old = m_middleIdx.exchange(writeIdx, std::memory_order_acq_rel);
            m_writeIdx.store(old, std::memory_order_relaxed);
            m_newFrame.store(true, std::memory_order_release);

            // ★★★ 帧处理链路追踪：三缓冲写入完成 ★★
            qInfo() << "[Renderer][FrameDiag] deliverFrame 三缓冲写入完成"
                    << " seq=" << logSeq << " frameId=" << frameId
                    << " writeIdx=" << writeIdx << " newFrame=true"
                    << " totalDeliver=" << totalDeliver
                    << " ★ deliverFrame 成功，渲染线程将在下次 updatePaintNode 消费此帧 ★";

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

        // ── ★★★ 触发渲染：Qt 6 兼容方案 ★★★
        // 使用 scheduleRenderJob 向渲染线程投递任务（绕过主线程阻塞）
        QQuickWindow* win = window();
        if (win) {
            triggerRenderRefresh(win,
                property("instanceId").toInt(),
                msSinceLastPN,
                pendingNow);

            // ── ★★★ 关键修复：同时调用 QQuickItem::update() ★★★
            // scheduleRenderJob 投递到渲染线程事件队列，但 Scene Graph 可能跳过调度。
            // QQuickItem::update() 向主线程事件队列投递 UpdateRequest，
            // 但主线程阻塞时无效。不过这是双重保险策略。
            // 最重要的是确保 QQuickItem 本身被标记为需要重绘。
            update();

            if (logSeq <= 5) {
                qInfo() << "[Client][VideoRenderer] ★★★ 渲染刷新已触发 ★★★"
                         << " seq=" << logSeq << " frameId=" << frameId
                         << " window=" << (void*)win
                         << " pendingFrames=" << pendingNow
                         << " msSinceLastPN=" << msSinceLastPN << "ms"
                         << " ★ 对比 updatePaintNode 日志确认渲染线程是否响应 ★";
            }
        } else {
            qCritical() << "[Client][VideoRenderer][FATAL] window() 返回 nullptr！"
                          " VideoRenderer 未加入 QML 场景图，视频将不显示！"
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
    // ── 渲染线程直接刷新（方案 C 核心）───────────────────────────────────────
    // 注意：此函数本身在任意线程被调用，但内部通过 scheduleRenderJob
    // 向渲染线程投递任务，绕过主线程阻塞。
    const int reqCount = m_renderThreadRefreshRequested.fetch_add(1);
    const int64_t now = TimeUtils::wallClockMs();
    const int pendingFrames = m_pendingFramesCount.load();
    const int64_t msSinceLastPN = now - m_lastPaintNodeTime.load();

    qInfo() << "[Client][VideoRenderer] ★★★ renderThreadRefreshImpl 被调用 ★★★"
            << " reqCount=" << reqCount
            << " pendingFrames=" << pendingFrames
            << " msSinceLastPN=" << msSinceLastPN << "ms"
            << " isVisible=" << isVisible()
            << " instanceId=" << property("instanceId").toInt()
            << " title=" << property("title").toString()
            << " callingThread=" << (void*)QThread::currentThreadId()
            << " window=" << (void*)window()
            << " ★ scheduleRenderJob 将向渲染线程投递刷新任务 ★";

    QQuickWindow* win = window();
    if (!win) {
        qWarning() << "[Client][VideoRenderer] renderThreadRefreshImpl: window() == nullptr，跳过";
        return;
    }

    // ── Qt 6 兼容方案：使用 scheduleRenderJob 向渲染线程投递刷新 ────────────────
    triggerRenderRefresh(win, property("instanceId").toInt(), msSinceLastPN, pendingFrames);
    m_renderThreadRefreshSucceeded.fetch_add(1);

    qInfo() << "[Client][VideoRenderer] renderThreadRefreshImpl: 刷新已投递"
            << " reqCount=" << reqCount
            << " totalSucceeded=" << m_renderThreadRefreshSucceeded.load()
            << " pendingFrames=" << pendingFrames;
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

    // ── 诊断：渲染线程调用频率（含 old 节点身份诊断）──────────────────────
    // ★★★ SGDiag：区分 placeholder vs VideoSGNode，定位类型混淆崩溃 ★★★
    if (seq <= 20 || seq % 60 == 0) {
        // Qt 6.8 QSGNode 不是 QObject，没有 metaObject()，只打印指针和 type
        qInfo() << "[Client][VideoRenderer][SGDiag] updatePaintNode"
                << " instanceId=" << property("instanceId").toInt()
                << " title=" << property("title").toString()
                << " seq=" << seq << " totalDeliver=" << totalDeliver
                << " old=" << (void*)old
                << " oldType=" << (old ? old->type() : -1)
                << " width=" << width() << " height=" << height()
                << " isVisible=" << isVisible()
                << " glCtx=" << (void*)QOpenGLContext::currentContext()
                << " callingThread=" << (void*)QThread::currentThreadId()
                << " ★ 后续 '返回旧 VideoSGNode' 日志中 m_isVideoSGNode=true 才安全 ★";
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
                    << " lifecycleId=" << slot.frame->lifecycleId
                    << " frame=" << slot.frame->width << "x" << slot.frame->height
                    << " pixelFormat=" << (int)slot.frame->pixelFormat
                    << " memoryType=" << (int)slot.frame->memoryType
                    << " old=" << (void*)old
                    << " ★★★ 视频即将显示！★★★";

            // 复用或创建 VideoSGNode
            // ★★★ 修复 v2：使用 dynamic_cast 而非 static_cast 进行安全复用 ★★★
            VideoSGNode* node = dynamic_cast<VideoSGNode*>(old);
            const bool nodeJustCreated = (node == nullptr);

            // ★★★ 新增：检测节点是否被 Scene Graph 复制 ★★★
            // 如果 old 是 VideoSGNode 但 m_geometry 指针无效，说明节点已被复制
            static QSet<void*> s_knownBadNodes;
            if (node && !s_knownBadNodes.contains((void*)node)) {
                // 检查节点是否"健康"（通过尝试调用一个安全方法）
                // 如果节点被复制，这个检查会失败
                try {
                    // 尝试访问 geometry 指针（最敏感的成员）
                    if (!node->isVideoSGNode()) {
                        qCritical() << "[Client][VideoRenderer][FATAL][NODECOPIED] ★★★ 检测到被复制的 VideoSGNode！★★★"
                                   << " instanceId=" << property("instanceId").toInt()
                                   << " old=" << (void*)old
                                   << " node=" << (void*)node
                                   << " isVideoSGNode()=false（应为 true）"
                                   << " ★★★ 节点被 Qt Scene Graph 复制后失效！★★★"
                                   << " ★ 强制创建新节点，不使用复制后的节点 ★";
                        node = nullptr;  // 强制创建新节点
                        s_knownBadNodes.insert((void*)old);
                    }
                } catch (...) {
                    qCritical() << "[Client][VideoRenderer][FATAL][EXCEPTION] 检测节点健康状态时异常"
                               << " old=" << (void*)old << " node=" << (void*)node;
                    node = nullptr;
                }
            }

            if (!node) {
                node = new VideoSGNode();
                node->setGpuInterop(m_gpuInterop.get());
                m_hasRealNode.store(true);
                qInfo() << "[Client][VideoRenderer][CREATE] ★★★ VideoSGNode 新建 ★★★"
                        << " instanceId=" << property("instanceId").toInt()
                        << " node=" << (void*)node
                        << " node->isVideoSGNode()=" << node->isVideoSGNode()
                        << " gpuInterop=" << (m_gpuInterop ? m_gpuInterop->name() : "NONE")
                        << " glCtx=" << (void*)QOpenGLContext::currentContext()
                        << " renderThread=" << (void*)QThread::currentThreadId()
                        << " ★★★ GPU 纹理即将创建，视频渲染开始！★★★";
            } else {
                qInfo() << "[Client][VideoRenderer][REUSE] ★ 复用旧 VideoSGNode ★"
                        << " instanceId=" << property("instanceId").toInt()
                        << " node=" << (void*)node
                        << " node->isVideoSGNode()=" << node->isVideoSGNode()
                        << " justCreated=" << nodeJustCreated;
            }

            // ── ★★★ 关键修复：强制标记需要重绘 ★★★
            // Qt Scene Graph 可能将首次返回 Placeholder 的节点标记为"静态"。
            // 为确保后续帧能正确渲染，必须显式标记节点需要重绘。
            node->markDirty(QSGNode::DirtyGeometry | QSGNode::DirtyMaterial);

            // 渲染帧
            try {
                node->updateGeometry(QRectF(0, 0, width(), height()), m_mirrorH);
                node->updateFrame(*slot.frame, slot.frame->lifecycleId);

                qInfo() << "[Client][VideoRenderer] ★★★ node->updateFrame 完成 ★★★"
                        << " instanceId=" << property("instanceId").toInt()
                        << " title=" << property("title").toString()
                        << " seq=" << seq << " frameId=" << slot.frame->frameId
                        << " lifecycleId=" << slot.frame->lifecycleId
                        << " node=" << (void*)node << " m_isVideoSGNode=" << node->isVideoSGNode()
                        << " justCreated=" << nodeJustCreated
                        << " ★★★ 视频帧已上传到 GPU，理论上应该显示！★★★";
            } catch (const std::exception& e) {
                qCritical() << "[Client][VideoRenderer][ERROR] updateFrame 异常:"
                           << e.what() << " w=" << width() << " h=" << height();
            }

            slot.dirty = false;

            // ★★★ 关键修复：渲染成功后再次调用 update() 确保 Scene Graph 重新调度 ★★★
            // Qt Scene Graph 可能在节点树锁定后不再调度此节点的 updatePaintNode。
            // 显式调用 update() 标记需要重绘，确保在下个渲染循环再次调用 updatePaintNode。
            update();

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
        // ════════════════════════════════════════════════════════════════════════════
        // 核心分支：无有效帧 → 返回 Placeholder
        //
        // ★★★ 根因修复（架构层面）：★★★
        //
        // 之前的问题：
        // 1. 首次 updatePaintNode(seq=1) 时首帧未到达 → slot.dirty=false → 返回 Placeholder
        // 2. Qt Scene Graph 将返回 Placeholder 的实例标记为 "static"
        // 3. "static" 节点在后续渲染循环中被完全跳过（不被调���）
        // 4. scheduleRenderJob 投递的任务仍入队列，但 Scene Graph 调度器忽略此实例
        // 5. 主视图黑屏 12 秒，直到 Scene Graph 内部饥饿检测强制重排
        //
        // 修复 v3：
        // - 无论何时返回 Placeholder，都强制调用 scheduleRenderJob + update()
        // - 不再依赖 Scene Graph 的"自动重排"机制
        // - 使用原子标记 m_forceRenderAfterPlaceholder，确保下次必然调用
        // ════════════════════════════════════════════════════════════════════════════

        // ── v3 新增：首次帧到达强制重排 ────────────────────────────────────────
        // 当 m_newFrame 已为 true（帧已到达）但本次 seq 还在 Placeholder 分支，
        // 说明发生了首次帧竞争 → 立即强制重排，打破 "static 标记" 陷阱
        if (m_newFrame.load(std::memory_order_acquire) && !slot.dirty) {
            qWarning() << "[Client][VideoRenderer][FIRST-FRAME] ★★★ 首次帧竞争检测！★★★"
                        << " instanceId=" << property("instanceId").toInt()
                        << " seq=" << seq
                        << " slot.dirty=" << slot.dirty
                        << " slot.frame=" << (bool)slot.frame
                        << " m_newFrame=" << m_newFrame.load()
                        << " totalDeliver=" << totalDeliver
                        << " ★ 立即强制 scheduleRenderJob x3，打破 static 标记 ★";
            QQuickWindow* win = window();
            if (win) {
                for (int i = 0; i < 3; ++i) {
                    triggerRenderRefresh(win,
                        property("instanceId").toInt(),
                        msSinceLastPN,
                        m_pendingFramesCount.load());
                }
            }
        }

        // ── Placeholder 分支日志 ───────────────────────────────────────────────
        if (seq <= 10 || seq % 30 == 0) {
            qInfo() << "[Client][VideoRenderer] ★ Placeholder 分支 seq=" << seq
                    << " instanceId=" << property("instanceId").toInt()
                    << " slot.dirty=" << slot.dirty << " slot.frame=" << (bool)slot.frame
                    << " old=" << (void*)old << " m_hasRealNode=" << m_hasRealNode.load()
                    << " m_newFrame=" << m_newFrame.load()
                    << " totalDeliver=" << totalDeliver;
        }

        // ── 分支1：有旧 VideoSGNode（显示最后一帧）─────────────────────────────
        if (old && old->type() == QSGNode::GeometryNodeType) {
            VideoSGNode* node = dynamic_cast<VideoSGNode*>(old);
            QSGGeometry* geo = node ? node->geometry() : nullptr;

            const bool isVideoSGNode = (node && node->isVideoSGNode());
            // v3 新增：也检查 magic signature
            bool sigValid = false;
            if (geo && isVideoSGNode) {
                const quint64 stored = reinterpret_cast<const quint64*>(geo)[0];
                sigValid = (stored == 0xDEADBEEF12345678ULL);
            }
            const bool nodeIsValid = (geo != nullptr && geo->vertexCount() == 4 && sigValid);

            if (isVideoSGNode && nodeIsValid) {
                if (seq <= 10 || seq % 30 == 0) {
                    qInfo() << "[Client][VideoRenderer] ★ 返回旧 VideoSGNode（显示最后一帧）"
                            << " instanceId=" << property("instanceId").toInt()
                            << " seq=" << seq << " node=" << (void*)node
                            << " m_isVideoSGNode=" << node->isVideoSGNode()
                            << " geo=" << (void*)geo << " vertexCount=" << geo->vertexCount()
                            << " sigValid=" << sigValid;
                }
                node->updateGeometry(QRectF(0, 0, width(), height()), m_mirrorH);
                // ── v3 核心修复：即使复用旧节点也强制重排 ──────────────────────
                update();
                return node;
            }

            qWarning() << "[Client][VideoRenderer][CRASH-FIX] 检测到类型混淆的旧节点"
                       << " instanceId=" << property("instanceId").toInt()
                       << " seq=" << seq << " old=" << (void*)old
                       << " oldType=" << (old ? old->type() : -1)
                       << " dynamic_cast=" << (void*)node
                       << " isVideoSGNode=" << isVideoSGNode
                       << " nodeIsValid=" << nodeIsValid
                       << " sigValid=" << sigValid
                       << " renderThread=" << (void*)QThread::currentThreadId();
        }

        // ── 分支2：无 VideoSGNode，使用 Placeholder ──────────────────────────
        if (!m_placeholderNode) {
            m_placeholderNode = createPlaceholderNode();
            qInfo() << "[Client][VideoRenderer] ★★★ Placeholder Node 创建 ★★★"
                    << " instanceId=" << property("instanceId").toInt()
                    << " seq=" << seq
                    << " node=" << (void*)m_placeholderNode;
        }

        updatePlaceholderGeometry(m_placeholderNode, QRectF(0, 0, width(), height()));

        if (seq <= 10 || seq % 30 == 0) {
            qInfo() << "[Client][VideoRenderer] ★ 返回 Placeholder Node"
                    << " instanceId=" << property("instanceId").toInt()
                    << " seq=" << seq << " node=" << (void*)m_placeholderNode
                    << " size=" << width() << "x" << height();
        }

        // ── ★★★ v3 核心修复（架构层面最重要的修复）：强制重排 ★★★
        // Qt Scene Graph 在收到 Placeholder 后会将实例标记为 static，不再调度。
        // 必须在返回 Placeholder 之前，主动强制重新调度。
        QQuickWindow* win = window();
        if (win) {
            win->scheduleRenderJob(
                new RenderRefreshRunnable(win,
                    property("instanceId").toInt(),
                    msSinceLastPN,
                    m_pendingFramesCount.load()),
                QQuickWindow::BeforeSynchronizingStage);
            win->scheduleRenderJob(
                new RenderRefreshRunnable(win,
                    property("instanceId").toInt(),
                    msSinceLastPN,
                    m_pendingFramesCount.load()),
                QQuickWindow::AfterSynchronizingStage);
            update();
            if (seq <= 10) {
                qInfo() << "[Client][VideoRenderer][PLACEHOLDER-FORCE] ★★★ Placeholder 分支强制重排 ★★★"
                        << " instanceId=" << property("instanceId").toInt()
                        << " seq=" << seq
                        << " totalDeliver=" << totalDeliver
                        << " ★ 连续 scheduleRenderJob x2，打破 static 标记！★★★";
            }
        }

        // ★★★ 永不返回 nullptr ★★★
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
// forceRefresh：Q_INVOKABLE，供 QML 在对话框打开/关闭时调用，强制触发渲染
// 使用 Qt 6 兼容的 scheduleRenderJob 方案，绕过主线程阻塞。
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
            << " ★ 使用 scheduleRenderJob 向渲染线程投递刷新任务 ★";

    // 诊断：如果距上次渲染超过阈值，发射阻塞信号
    if (msSinceLastPN > RENDER_STALL_TIMEOUT_MS) {
        qWarning() << "[Client][VideoRenderer][STALL] ★★★ 渲染线程疑似阻塞 ★★★"
                    << " instanceId=" << property("instanceId").toInt()
                    << " msSinceLastPaint=" << msSinceLastPN
                    << " pendingFrames=" << pendingFrames
                    << " threshold=" << RENDER_STALL_TIMEOUT_MS << "ms"
                    << " m_updatePaintNodeSeq=" << m_updatePaintNodeSeq
                    << " ★ 检测到渲染线程阻塞，scheduleRenderJob 将尝试绕过主线程 ★";
        emit renderThreadStalled(pendingFrames, msSinceLastPN);
    }

    // ── Qt 6 兼容方案：使用 scheduleRenderJob 向渲染线程投递刷新 ────────────────
    QQuickWindow* win = window();
    if (win) {
        triggerRenderRefresh(win, property("instanceId").toInt(), msSinceLastPN, pendingFrames);

        // ════════════════════════════════════════════════════════════════════════
        // ★★★ 黑屏修复：同时调用 QQuickItem::update() ★★★
        //
        // 根因：modal 对话框阻塞主线程事件循环。
        // window()->update() 内部向主线程事件队列投递 QEvent::UpdateRequest，
        // 但主线程被 modal 阻塞，UpdateRequest 永远得不到处理 → Scene Graph
        // 无法调度 updatePaintNode → 视频黑屏。
        //
        // 修复：在 forceRefresh() 中同时调用 QQuickItem::update()。
        // QQuickItem::update() 标记 ItemNeedSWFrame 标志，让 Scene Graph 在
        // polish() 阶段感知到脏状态。即使主线程被阻塞，
        // polish() 会在主线程恢复后执行并检测到该标志，
        // 触发 Scene Graph 的 UpdateRequest 传递。
        // scheduleRenderJob() + QQuickItem::update() 双保险。
        // ════════════════════════════════════════════════════════════════════════
        update();

        // 渲染线程直接刷新（最激进策略）
        if (pendingFrames > 0 || msSinceLastPN > RENDER_STALL_TIMEOUT_MS) {
            renderThreadRefreshImpl();
        }

        qInfo() << "[Client][VideoRenderer] forceRefresh: 刷新已投递"
                << " instanceId=" << property("instanceId").toInt()
                << " callSeq=" << fc
                << " renderThreadRefreshSucceeded=" << m_renderThreadRefreshSucceeded.load();
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
    m_pollingTimer->setInterval(8);  // ★★★ 修复：8ms间隔(~120fps)，比60fps渲染周期更快响应 ★★★

    QObject::connect(m_pollingTimer, &QTimer::timeout, this, [this]() {
        const int64_t now = TimeUtils::wallClockMs();
        const int64_t msSinceLastPN = now - m_lastPaintNodeTime.load();
        const int pendingFrames = m_pendingFramesCount.load();

        const bool hasUnrenderedFrame = m_newFrame.load(std::memory_order_acquire);
        // ★★★ 修复：更积极的刷新策略 ★★★
        // 视频渲染需要持续刷新，只要有待渲染帧就触发刷新
        const bool renderStalled = (msSinceLastPN > 16 || hasUnrenderedFrame);

        if (renderStalled || hasUnrenderedFrame) {
            QQuickWindow* win = window();
            if (win) {
                // ★★★ 轮询检测到渲染被跳过 → 使用 scheduleRenderJob 刷新 ★★★
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
                            << " ★ scheduleRenderJob 已投递，绕过主线程阻塞 ★";
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
// ★★★ 修复：更积极的可见性变化处理 ★★★
// ═══════════════════════════════════════════════════════════════════════════════
void VideoRenderer::itemChange(ItemChange change, const ItemChangeData & value)
{
    QQuickItem::itemChange(change, value);

    switch (change) {
    case ItemVisibleHasChanged:
        qInfo() << "[Client][VideoRenderer][VISIBLE] ★★★ visible 变化 ★★★"
                << " instanceId=" << property("instanceId").toInt()
                << " title=" << property("title").toString()
                << " visible=" << value.boolValue
                << " isVisible=" << isVisible()
                << " window=" << (void*)window()
                << " callingThread=" << (void*)QThread::currentThreadId();

        // visible 变化时，无论变为 true 还是 false，都触发强制刷新
        // 因为变为 false 可能导致 Scene Graph 优化
        {
            QQuickWindow* win = window();
            if (win) {
                // ★★★ 修复：所有可见性变化都触发多重刷新机制 ★★★
                polish();                          // 1. 标记需要 polish
                win->update();                    // 2. 主线程 update
                update();                         // 3. QQuickItem update
                renderThreadRefreshImpl();         // 4. 渲染线程 scheduleRenderJob
                QCoreApplication::postEvent(this, new QEvent(QEvent::UpdateRequest),
                                            Qt::HighEventPriority);  // 5. 高优先级事件

                qInfo() << "[Client][VideoRenderer][VISIBLE] visible 变化，多重刷新已触发"
                        << " instanceId=" << property("instanceId").toInt()
                        << " m_updatePaintNodeSeq=" << m_updatePaintNodeSeq;
            }
        }
        break;

    case ItemSceneChange:
        qInfo() << "[Client][VideoRenderer][SCENE] ★★★ 场景变化 ★★★"
                << " instanceId=" << property("instanceId").toInt()
                << " window=" << (void*)window()
                << " callingThread=" << (void*)QThread::currentThreadId();
        // 场景变化时，强制刷新确保节点正确初始化
        if (window()) {
            renderThreadRefreshImpl();
        }
        break;

    case ItemParentHasChanged:
        qInfo() << "[Client][VideoRenderer][PARENT] ★★★ 父项变化 ★★★"
                << " instanceId=" << property("instanceId").toInt()
                << " oldParent=" << (void*)value.item
                << " newParent=" << (void*)parentItem()
                << " callingThread=" << (void*)QThread::currentThreadId();
        // 父项变化时，重新触发刷新
        if (window()) {
            renderThreadRefreshImpl();
        }
        break;

    case ItemEnabledHasChanged:
        qInfo() << "[Client][VideoRenderer][ENABLED] ★★★ enabled 变化 ★★★"
                << " instanceId=" << property("instanceId").toInt()
                << " enabled=" << value.boolValue
                << " callingThread=" << (void*)QThread::currentThreadId();
        // enabled 变化时也触发刷新
        if (window()) {
            update();
            renderThreadRefreshImpl();
        }
        break;

    default:
        break;
    }
}
