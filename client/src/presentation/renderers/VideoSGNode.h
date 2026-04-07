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
 *
 * ═══════════════════════════════════════════════════════════════════════════════
 * ★★★ Qt 6 兼容性修复 v2 ★★★
 *
 * 根因分析：
 * 1. Qt 5：节点树主要在主线程修改
 * 2. Qt 6：渲染线程可以在 BeforeSynchronizingStage 和 AfterSynchronizingStage 修改节点
 * 3. Qt 6 Scene Graph 可能在渲染线程上复制节点以进行优化
 *
 * 问题演进：
 * 1. m_geometry 改为指针 + OwnsGeometry=true 后，Qt 复制节点时释放几何体
 * 2. OwnsGeometry=false 禁用后，仍需手动管理几何体生命周期
 * 3. 需要更强的防御性检查防止崩溃
 *
 * 修复方案 v2：
 * - OwnsGeometry=false：禁用 Qt 的几何体复制机制
 * - 手动管理几何体：在析构函数中手动 delete
 * - 添加防御性检查：在 vertexCount() 调用前检查空指针
 * - 添加节点复制检测：通过 instanceId 追踪节点生命周期
 * - 添加 try-catch：捕获 vertexCount() 异常
 * ═══════════════════════════════════════════════════════════════════════════════
 */
class VideoSGNode : public QSGGeometryNode {
    Q_DISABLE_COPY(VideoSGNode)

public:
    // ★★★ 用于在 updatePaintNode 中区分 VideoSGNode 与 placeholder ★★★
    // Qt 6.8 QSGNode 不是 QObject，没有 setId()/id()/metaObject()
    // 通过构造函数设置 m_isVideoSGNode = true，placeholder 保持 false
    bool isVideoSGNode() const { return m_isVideoSGNode; }

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
    void updateFrame(const VideoFrame& frame, quint64 lifecycleId = 0);

    bool isReady() const { return m_ready; }

private:
    QSGGeometry*    m_geometry = nullptr;  // ★★★ 修复 v2：OwnsGeometry=false，手动管理生命周期 ★★★
    VideoMaterial*  m_material = nullptr;
    IGpuInterop*    m_interop  = nullptr;
    bool            m_ready    = false;
    bool            m_isVideoSGNode = false;  // ★★★ 标记：区分 VideoSGNode 与 placeholder ★★★
    int             m_instanceId = 0;         // ★★★ 调试用：追踪节点实例，便于对比创建/销毁日志 ★★★
    void*           m_creationThis = nullptr; // ★★★ P1 修复：记录自身创建地址，避免跨实例静态变量污染 ★★★
};
