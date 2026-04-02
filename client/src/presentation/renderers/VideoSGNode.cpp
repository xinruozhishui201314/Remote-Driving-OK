#include "VideoSGNode.h"
#include "VideoMaterial.h"
#include <QSGGeometry>
#include <QDebug>
#include <QDateTime>
#include <cstring>

VideoSGNode::VideoSGNode()
    : QSGGeometryNode()
    , m_geometry(QSGGeometry::defaultAttributes_TexturedPoint2D(), 4, 6)
{
    m_geometry.setDrawingMode(QSGGeometry::DrawTriangles);
    m_material = new VideoMaterial();
    setGeometry(&m_geometry);
    setMaterial(m_material);
    setFlag(OwnsMaterial);
    setFlag(OwnsGeometry, false); // m_geometry is a stack member
}

VideoSGNode::~VideoSGNode() = default;

void VideoSGNode::updateGeometry(const QRectF& rect, bool mirrorH)
{
    const float x0 = static_cast<float>(rect.left());
    const float y0 = static_cast<float>(rect.top());
    const float x1 = static_cast<float>(rect.right());
    const float y1 = static_cast<float>(rect.bottom());

    float u0 = 0.0f, u1 = 1.0f;
    if (mirrorH) std::swap(u0, u1);

    // 与 defaultAttributes_TexturedPoint2D 一致：每顶点 4 个 float（x, y, tx, ty）
    float* v = static_cast<float*>(m_geometry.vertexData());
    const float verts[] = {
        x0, y0, u0, 0.0f,
        x1, y0, u1, 0.0f,
        x0, y1, u0, 1.0f,
        x1, y1, u1, 1.0f,
    };
    std::memcpy(v, verts, sizeof(verts));

    // 2 triangles: (0,1,2) and (1,3,2)
    quint16* idx = m_geometry.indexDataAsUShort();
    idx[0] = 0; idx[1] = 1; idx[2] = 2;
    idx[3] = 1; idx[4] = 3; idx[5] = 2;

    markDirty(QSGNode::DirtyGeometry);
    m_ready = true;
}

void VideoSGNode::updateFrame(const VideoFrame& frame)
{
    if (!m_material) {
        qWarning() << "[VideoSGNode][WARN] updateFrame: m_material is null, skipping";
        return;
    }

    // ★ 关键诊断：确认 updateFrame 被调用 + 每30帧打印一次
    static int s_logCount = 0;
    ++s_logCount;
    const int logSeq = s_logCount;
    // 首5次 + 每30帧打印关键信息；CPU interop结果无论何时都打印
    if (logSeq <= 5 || logSeq % 30 == 0) {
        qInfo() << "[VideoSGNode] ★★★ updateFrame 被调用"
                << " seq=" << logSeq
                << " frameId=" << frame.frameId
                << " frame=" << frame.width << "x" << frame.height
                << " memoryType=" << static_cast<int>(frame.memoryType)
                << " pixelFormat=" << static_cast<int>(frame.pixelFormat)
                << " planes[0].data=" << (void*)frame.planes[0].data
                << " interop=" << (void*)m_interop
                << " (interop=" << (m_interop ? m_interop->name() : "NONE") << ")"
                   " ★ 对比 QML setFrame frameId 确认 emit→updateFrame 不断链";
    }

    // ── 诊断：每 30 帧打印一次 GPU 上传路径 + 耗时（microsecond）───────────────
    static int s_uploadLogCount = 0;
    static int64_t s_uploadLogStart = QDateTime::currentMSecsSinceEpoch();
    ++s_uploadLogCount;
    const int64_t now = QDateTime::currentMSecsSinceEpoch();
    if (s_uploadLogCount % 30 == 0 && s_uploadLogCount > 0) {
        const int64_t elapsed = now - s_uploadLogStart;
        s_uploadLogStart = now;
        const char* path =
            (frame.memoryType == VideoFrame::MemoryType::DMA_BUF) ? "DMA-BUF" :
            (frame.memoryType == VideoFrame::MemoryType::GPU_TEXTURE_GL) ? "GPU-texture" :
            "CPU-upload";
        qInfo() << "[VideoSGNode][Path] 每30帧统计: path=" << path
                 << " frames=" << s_uploadLogCount << " elapsed=" << elapsed << "ms"
                 << " fps=" << (elapsed > 0 ? (30000.0 / elapsed) : -1.0)
                 << " size=" << frame.width << "x" << frame.height;
        s_uploadLogCount = 0;
    }

    // ── 路径 1：DMA-BUF 零拷贝（Intel/AMD VAAPI） ────────────────────────────
    if (frame.memoryType == VideoFrame::MemoryType::DMA_BUF && m_interop) {
        const IGpuInterop::TextureSet ts = m_interop->importFrame(frame);
        if (ts.valid) {
            m_material->setTextureSet(ts);
            markDirty(QSGNode::DirtyMaterial);
            // ★★★ DMA-BUF 路径成功 ★★★（如果走这里说明 VAAPI/EglInterop 零拷贝工作正常）
            if (logSeq <= 10) {
                qInfo() << "[VideoSGNode] ★★★ DMA-BUF 路径成功 ★★★"
                        << " ts.yTexId=" << ts.yTexId
                        << " interop=" << (m_interop ? m_interop->name() : "none")
                        << " frameId=" << frame.frameId;
            }
            return;
        }
        // importFrame 失败，降级到 CPU 路径
        // ★★★ 诊断：DMA-BUF 降级到 CPU（常见原因：VAAPI 未初始化、DMA-BUF fd 无效） ★★★
        qDebug() << "[VideoSGNode][Path] DMA-BUF 失败，降级到 CPU path"
                 << " interop=" << (m_interop ? m_interop->name() : "none")
                 << " frameId=" << frame.frameId;
    }

    // ── 路径 2：已有 GL 纹理（外部 CUDA/interop 路径） ───────────────────────
    if (frame.memoryType == VideoFrame::MemoryType::GPU_TEXTURE_GL &&
        frame.gpuHandle.glTextureId != 0)
    {
        IGpuInterop::TextureSet ts;
        ts.yTexId  = frame.gpuHandle.glTextureId;
        ts.width   = static_cast<int>(frame.width);
        ts.height  = static_cast<int>(frame.height);
        ts.isNv12  = false; // 外部纹理已完成 YUV→RGB，视为 RGBA
        ts.valid   = true;
        m_material->setTextureSet(ts);
        markDirty(QSGNode::DirtyMaterial);
        // ★★★ GPU-texture 路径成功 ★★★（如果走这里说明 CUDA/NVDEC interop 工作正常）
        if (logSeq <= 10) {
            qInfo() << "[VideoSGNode] ★★★ GPU-texture 路径成功 ★★★"
                    << " texId=" << ts.yTexId
                    << " frameId=" << frame.frameId;
        }
        return;
    }

    // ── 路径 3：CPU 内存（通用后备，经 CpuUploadInterop 上传） ───────────────
    // ★★★ 这是最常见的路径：软件解码 + CPU→GPU upload ★★★
    if (frame.memoryType == VideoFrame::MemoryType::CPU_MEMORY && m_interop) {
        const IGpuInterop::TextureSet ts = m_interop->importFrame(frame);
        if (ts.valid) {
            m_material->setTextureSet(ts);
            markDirty(QSGNode::DirtyMaterial);
            // ★★★ CPU-interop 路径成功（每次都打！诊断关键路径） ★★★
            qInfo() << "[VideoSGNode] ★★★ CPU-interop 路径成功 ★★★"
                    << " seq=" << logSeq
                    << " frameId=" << frame.frameId
                    << " interop=" << (m_interop ? m_interop->name() : "none")
                    << " yTex=" << ts.yTexId
                    << " size=" << frame.width << "x" << frame.height;
            return;
        }
        // ★★★ 诊断：m_interop 不为空但 importFrame 失败 ★★★
        if (logSeq <= 10 || logSeq % 30 == 0) {
            qWarning() << "[VideoSGNode][Path] CPU_MEMORY + interop 不为空，但 importFrame 失败"
                       << " seq=" << logSeq
                       << " frameId=" << frame.frameId
                       << " interop=" << (m_interop ? m_interop->name() : "none")
                       << " 降级到直接上传路径";
        }
    }

    // ── 最终后备：直接三平面上传（无 interop 时的安全路径） ─────────────────
    if (frame.planes[0].data) {
        m_material->uploadYuvFrame(
            static_cast<const uint8_t*>(frame.planes[0].data),
            static_cast<int>(frame.planes[0].stride),
            static_cast<const uint8_t*>(frame.planes[1].data),
            static_cast<int>(frame.planes[1].stride),
            static_cast<const uint8_t*>(frame.planes[2].data),
            static_cast<int>(frame.planes[2].stride),
            static_cast<int>(frame.width),
            static_cast<int>(frame.height),
            frame.pixelFormat == VideoFrame::PixelFormat::NV12 ||
            frame.pixelFormat == VideoFrame::PixelFormat::NV21);
        markDirty(QSGNode::DirtyMaterial);
        // ★★★ 直接上传路径（m_interop==nullptr 时走这里，仍可正常显示）★★★
        if (logSeq <= 10) {
            qInfo() << "[VideoSGNode] ★★★ CPU-upload 直接路径成功 ★★★"
                    << " frameId=" << frame.frameId
                    << " interop=" << (m_interop ? m_interop->name() : "NONE")
                    << " size=" << frame.width << "x" << frame.height;
        }
    } else {
        // ── 诊断：所有路径都失败时打印警告 ─────────────────────────────────────
        qWarning() << "[VideoSGNode][FATAL] 所有上传路径均失败："
                      " frame 将不更新（黑屏）！"
                      " frameId=" << frame.frameId
                      << " memoryType=" << static_cast<int>(frame.memoryType)
                      << " planes[0]=" << (void*)frame.planes[0].data
                      << " gpuTextureId=" << frame.gpuHandle.glTextureId
                      << " interop=" << (void*)m_interop
                      << " (NONE=需检查 GpuInteropFactory::create())";
    }
}
