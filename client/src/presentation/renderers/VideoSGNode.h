#pragma once
#include <QSGGeometryNode>
#include <QSGGeometry>
#include <QSGMaterial>
#include <QSGTexture>
#include <memory>
#include "../../infrastructure/media/IHardwareDecoder.h"
#include "../../infrastructure/media/gpu/IGpuInterop.h"

class VideoMaterial;

/**
 * 自定义 Scene Graph 节点（《客户端架构设计》§3.4.1 零拷贝完整实现）。
 *
 * 帧处理路径（按优先级）：
 *   1. DMA_BUF       → EGLDmaBufInterop → GL 纹理（Intel/AMD 真正零拷贝）
 *   2. GPU_TEXTURE_GL → 直接绑定已有 GL 纹理（外部提供）
 *   3. CPU_MEMORY    → CpuUploadInterop → glTexSubImage2D
 *
 * 所有方法仅在 Qt 渲染线程调用。
 */
class VideoSGNode : public QSGGeometryNode {
public:
    explicit VideoSGNode();
    ~VideoSGNode() override;

    /**
     * 设置 GPU 互操作后端（由 VideoRenderer 在渲染线程初始化后传入）。
     * 必须在第一次 updateFrame 前调用。
     */
    void setGpuInterop(IGpuInterop* interop) { m_interop = interop; }

    void updateGeometry(const QRectF& rect, bool mirrorH = false);

    /**
     * 使用最优路径将帧内容上传/导入到 GL 材质。
     * 根据 frame.memoryType 自动路由。
     */
    void updateFrame(const VideoFrame& frame);

    bool isReady() const { return m_ready; }

private:
    QSGGeometry     m_geometry;
    VideoMaterial*  m_material = nullptr;
    IGpuInterop*    m_interop  = nullptr;
    bool            m_ready    = false;
};
