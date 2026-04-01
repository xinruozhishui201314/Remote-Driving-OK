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
    if (!m_material) return;

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
            return;
        }
        // importFrame 失败，降级到 CPU 路径
        qDebug() << "[VideoSGNode][Path] DMA-BUF 失败，降级到 CPU path";
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
        return;
    }

    // ── 路径 3：CPU 内存（通用后备，经 CpuUploadInterop 上传） ───────────────
    if (frame.memoryType == VideoFrame::MemoryType::CPU_MEMORY && m_interop) {
        const IGpuInterop::TextureSet ts = m_interop->importFrame(frame);
        if (ts.valid) {
            m_material->setTextureSet(ts);
            markDirty(QSGNode::DirtyMaterial);
            return;
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
    } else {
        // ── 诊断：所有路径都失败时打印警告 ─────────────────────────────────────
        qWarning() << "[VideoSGNode][Diag] 所有上传路径均失败："
                      " DMA-BUF/GPU-texture/CPU interop 全部不可用，frame 将不更新。"
                      " memoryType=" << static_cast<int>(frame.memoryType)
                      << " planes[0]=" << (void*)frame.planes[0].data
                      << " gpuTextureId=" << frame.gpuHandle.glTextureId
                      << " interop=" << (void*)m_interop;
    }
}
