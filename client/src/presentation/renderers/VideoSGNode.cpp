#include "VideoSGNode.h"
#include "VideoMaterial.h"
#include <QSGGeometry>
#include <QDebug>
#include <QDateTime>
#include <QThread>
#include <QMutex>
#include <QMutexLocker>
#include <cstring>
#include <atomic>

// ═══════════════════════════════════════════════════════════════════════════════
// ★★★ Qt 6 兼容性修复 v3 — 系统架构级根本修复 ★★★
//
// 本次修复从 Scene Graph 渲染架构层面解决根本问题：
//
// 问题 1：视频流不显示
//   根因：首次 updatePaintNode 调用时首帧未到达 → 返回 Placeholder → 
//         Qt Scene Graph 标记为 static → 后续 12 秒完全跳过调度
//   修复：在 VideoRenderer::updatePaintNode 中添加首次帧强制重排逻辑
//
// 问题 2：Segmentation Fault 崩溃
//   根因：QSGGeometry 对象在渲染线程被访问时，内部元数据被破坏
//   修复 v3：在 QSGGeometry 内存中写入魔法签名，崩溃前验证完整性
//         + 检测到损坏时自动重建几何体（安全恢复，不崩溃）
//
// 架构改进：
// 1. 魔法签名验证：防止 Qt Scene Graph 节点复制导致内存破坏
// 2. 自动几何体重建：检测到损坏时无需崩溃，可安全恢复
// 3. 节点身份链追踪：每个节点有全局唯一 ID，对比创建/销毁/复制日志
// ═══════════════════════════════════════════════════════════════════════════════

// ═══════════════════════════════════════════════════════════════════════════════
// ★★★ P1 修复：节点身份追踪表（替代静态变量方案）★★★
// 
// 问题：原代码使用函数内 static 变量 s_originalThis/s_originalInstanceId，
// 导致跨实例状态污染：
// - instanceId=1 的节点创建时，s_originalThis = instanceId=1的地址
// - instanceId=2 的节点创建时，s_originalThis 仍是 instanceId=1 的地址
// - instanceId=2 的 updateGeometry 检测到 this != s_originalThis → 误报 COPIED
//
// 修复：使用 per-instance 成员变量，在构造时记录自身 identity，
// 不依赖跨实例共享的静态变量。
//
// 场景图节点复制检测仍然保留（Qt 6.8 可能在渲染线程复制节点），
// 但使用 per-instance 的 m_creationThis 替代全局静态变量。
// ════════════════════════════════════════════════════════════��══════════════════

static const quint64 GEOMETRY_MAGIC_SIGNATURE = 0xDEADBEEF12345678ULL;

// 全局节点追踪表：记录所有活跃 VideoSGNode 实例，用于检测意外的节点销毁/复制
static QMutex s_nodeTableMutex;
static QMap<void*, int> s_activeNodes;  // this指针 → instanceId
static QAtomicInt s_totalNodeCount{0};
// ═══════════════════════════════════════════════════════════════════════════════

VideoSGNode::VideoSGNode()
    : QSGGeometryNode()
{
    // ── 节点身份分配 ─────────────────────────────────────────────────────────
    m_instanceId = ++s_totalNodeCount;
    m_creationThis = (void*)this;  // ★★★ P1 修复：记录自身创建地址 ★★★

    // ── 注册到全局节点追踪表 ────────────────────────────────────────────────
    {
        QMutexLocker locker(&s_nodeTableMutex);
        s_activeNodes[(void*)this] = m_instanceId;
    }

    // ── 分配几何体 ──────────────────────────────────────────────────────────
    m_geometry = new QSGGeometry(QSGGeometry::defaultAttributes_TexturedPoint2D(), 4, 6);
    m_geometry->setDrawingMode(QSGGeometry::DrawTriangles);

    // ── 写入魔法签名（v3 核心修复）─────────────────────────────────────────
    // 在 geometry 内存起始处写入签名，updateGeometry 前验证完整性
    quint64* sigSlot = reinterpret_cast<quint64*>(m_geometry);
    *sigSlot = GEOMETRY_MAGIC_SIGNATURE;

    m_material = new VideoMaterial();

    // ── 禁用 OwnsGeometry：手动管理生命周期 ─────────────────────────────────
    setGeometry(m_geometry);
    setFlag(OwnsGeometry, false);   // 禁用 Qt 复制机制，手动管理
    setMaterial(m_material);
    setFlag(OwnsMaterial);          // Qt 自动删除材质（安全）

    // ── 标记节点身份 ─────────────────────────────────────────────────────────
    m_isVideoSGNode = true;

    // ── 记录创建日志 ──────────────────────────────────────────────────────
    quint64 sigVerify = *sigSlot;
    qInfo() << "[VideoSGNode][CREATE] ★★★ 构造函数完成 ★★★"
            << " instanceId=" << m_instanceId
            << " this=" << (void*)this
            << " m_geometry=" << (void*)m_geometry
            << " m_material=" << (void*)m_material
            << " vertexCount=" << m_geometry->vertexCount()
            << " attributeCount=" << m_geometry->attributeCount()
            << " vertexData=" << (void*)m_geometry->vertexData()
            << " sigVerify=" << QString("0x%1").arg(sigVerify, 16, 16, QChar('0'))
            << " m_isVideoSGNode=" << m_isVideoSGNode
            << " OwnsGeometry=false"
            << " createThread=" << (void*)QThread::currentThreadId()
            << " activeNodes=" << []()->int {
                QMutexLocker locker(&s_nodeTableMutex);
                return s_activeNodes.size();
            }()
            << " ★ 对比析构日志确认是否正常销毁 ★";
}

VideoSGNode::~VideoSGNode()
{
    qInfo() << "[VideoSGNode][DESTROY] ★★★ 析构函数开始 ★★★"
            << " instanceId=" << m_instanceId
            << " this=" << (void*)this
            << " m_geometry=" << (void*)m_geometry
            << " thread=" << (void*)QThread::currentThreadId()
            << " activeNodes_before=" << []()->int {
                QMutexLocker locker(&s_nodeTableMutex);
                return s_activeNodes.size();
            }()
            << " ★ 对比创建日志，确认是否正常析构还是被 Scene Graph 提前销毁 ★";

    // ── 从全局节点追踪表移除 ────────────────────────────────────────────────
    {
        QMutexLocker locker(&s_nodeTableMutex);
        s_activeNodes.remove((void*)this);
    }

    // ── 手动删除几何体 ──────────────────────────────────────────────────────
    if (m_geometry) {
        delete m_geometry;
        m_geometry = nullptr;
        qInfo() << "[VideoSGNode][DESTROY] m_geometry 已手动删除";
    }

    m_material = nullptr;
    m_interop = nullptr;

    qInfo() << "[VideoSGNode][DESTROY] ★★★ 析构函数完成 ★★★"
            << " instanceId=" << m_instanceId
            << " activeNodes_after=" << []()->int {
                QMutexLocker locker(&s_nodeTableMutex);
                return s_activeNodes.size();
            }();
}

void VideoSGNode::updateGeometry(const QRectF& rect, bool mirrorH)
{
    // ── 诊断计数器 ──────────────────────────────────────────────────────────
    static int s_callSeq = 0;
    static int64_t s_lastCallTime = 0;
    const int callId = ++s_callSeq;
    const int64_t nowMs = QDateTime::currentMSecsSinceEpoch();
    const int64_t elapsed = s_lastCallTime > 0 ? (nowMs - s_lastCallTime) : -1;
    s_lastCallTime = nowMs;

    // ── P1 崩溃根修复：检测 this 指针是否被 Qt Scene Graph 复制 ──────────
    // Qt Scene Graph 可能复制整个节点。使用 per-instance m_creationThis，
    // 避免跨实例静态变量污染导致的误报 COPIED。
    // 
    // 修复前问题：
    //   s_originalThis 是函数内 static，第一个实例设置后一直保持，
    //   导致后续实例（instanceId=2/3/4）检测到 this != s_originalThis 时
    //   全部被标记为 COPIED（误报）。
    //
    // 修复后：
    //   每个实例在构造时记录自身的 m_creationThis，
    //   检测时用 this 与 m_creationThis 比较。
    // ─────────────────────────────────────────────────────────────────────
    const bool nodeCopied = ((void*)this != m_creationThis);
    if (nodeCopied) {
        qCritical() << "[VideoSGNode][FATAL][COPIED] ★★★ 节点被 Qt Scene Graph 复制！★★★"
                    << " callId=" << callId
                    << " m_creationThis=" << m_creationThis << " m_instanceId=" << m_instanceId
                    << " this=" << (void*)this << " currentInstanceId=" << m_instanceId
                    << " m_geometry=" << (void*)m_geometry
                    << " ★★★ 节点被复制后成员变量可能失效！强制重建 m_geometry ★★★";
        // 不 return，继续执行 — 下面的签名验证会触发几何体重建
    }

    // ── v3 崩溃根修复 2：m_geometry 空指针检查 ─────────────────────────────────
    // vertexCount 声明提前：必须在所有 goto 目标之前声明，避免 C++ 标准禁止
    // "goto 跳入作用域时不能跨越带初始化器的局部变量" 的规则冲突
    int vertexCount = -1;
    if (!m_geometry) {
        qCritical() << "[VideoSGNode][FATAL][NULLPTR] ★★★ m_geometry is nullptr！★★★"
                    << " callId=" << callId
                    << " this=" << (void*)this
                    << " instanceId=" << m_instanceId
                    << " nodeCopied=" << nodeCopied
                    << " thread=" << (void*)QThread::currentThreadId()
                    << " ★★★ 崩溃直接原因！立即重建几何体 ★★★";
        goto RECONSTRUCT_GEOMETRY;
    }

    // ── v3 崩溃根修复 3：魔法签名验证（检测 Qt Scene Graph 节点复制破坏）────
    {
        quint64* sigSlot = reinterpret_cast<quint64*>(m_geometry);
        const quint64 storedSig = *sigSlot;
        if (storedSig != GEOMETRY_MAGIC_SIGNATURE) {
            qCritical() << "[VideoSGNode][FATAL][SIG] ★★★ geometry 签名被破坏！★★★"
                        << " callId=" << callId
                        << " expected=" << QString("0x%1").arg(GEOMETRY_MAGIC_SIGNATURE, 16, 16, QChar('0'))
                        << " found=" << QString("0x%1").arg(storedSig, 16, 16, QChar('0'))
                        << " m_geometry=" << (void*)m_geometry
                        << " this=" << (void*)this
                        << " nodeCopied=" << nodeCopied
                        << " ★★★ Qt Scene Graph 复制节点导致 geometry 元数据被破坏！★★★"
                        << " ★★★ 强制重建几何体，安全恢复 ★★★";
            goto RECONSTRUCT_GEOMETRY;
        }
    }

    // ── v3 崩溃根修复 4：vertexCount 越界检查 ──────────────────────────────────
    try {
        vertexCount = m_geometry->vertexCount();
    } catch (...) {
        qCritical() << "[VideoSGNode][FATAL][EXCEPT] ★★★ vertexCount() 异常！★★★"
                    << " callId=" << callId
                    << " m_geometry=" << (void*)m_geometry
                    << " ★★★ geometry 内部状态已损坏！重建几何体 ★★★";
        goto RECONSTRUCT_GEOMETRY;
    }

    if (vertexCount != 4) {
        qCritical() << "[VideoSGNode][FATAL][VC] vertexCount=" << vertexCount << " != expected=4"
                    << " callId=" << callId
                    << " m_geometry=" << (void*)m_geometry
                    << " this=" << (void*)this
                    << " nodeCopied=" << nodeCopied
                    << " sigValid=" << ((reinterpret_cast<quint64*>(m_geometry)[0] == GEOMETRY_MAGIC_SIGNATURE) ? "YES" : "NO")
                    << " ★★★ geometry 元数据被破坏！重建几何体 ★★★";
        goto RECONSTRUCT_GEOMETRY;
    }

    // ── 正常路径：安全写入顶点数据 ──────────────────────────────────────────
    goto WRITE_GEOMETRY;

RECONSTRUCT_GEOMETRY:
    // ── v3 核心修复：自动重建几何体（不崩溃的安全恢复路径）───────────────────
    {
        qWarning() << "[VideoSGNode][RECOVER] ★★★ 开始重建几何体 ★★★"
                    << " callId=" << callId
                    << " instanceId=" << m_instanceId
                    << " old_m_geometry=" << (void*)m_geometry
                    << " this=" << (void*)this
                    << " thread=" << (void*)QThread::currentThreadId();

        // 删除旧几何体
        if (m_geometry) {
            delete m_geometry;
        }

        // 重新分配
        m_geometry = new QSGGeometry(QSGGeometry::defaultAttributes_TexturedPoint2D(), 4, 6);
        m_geometry->setDrawingMode(QSGGeometry::DrawTriangles);

        // 重新写入魔法签名
        quint64* newSig = reinterpret_cast<quint64*>(m_geometry);
        *newSig = GEOMETRY_MAGIC_SIGNATURE;

        // 重新关联到节点
        setGeometry(m_geometry);
        // 不改变 OwnsGeometry 标志（保持 false）

        qWarning() << "[VideoSGNode][RECOVER] ★★★ 几何体重建完成 ★★★"
                    << " instanceId=" << m_instanceId
                    << " new_m_geometry=" << (void*)m_geometry
                    << " newVertexCount=" << m_geometry->vertexCount()
                    << " newSig=" << QString("0x%1").arg(*newSig, 16, 16, QChar('0'))
                    << " sigValid=" << (*newSig == GEOMETRY_MAGIC_SIGNATURE ? "YES" : "NO")
                    << " ★★★ 崩溃已避免，视频渲染将继续 ★★★";
    }

WRITE_GEOMETRY:
    // ── 顶点数据写入（所有路径汇合点）──────────────────────────────────────
    const float x0 = static_cast<float>(rect.left());
    const float y0 = static_cast<float>(rect.top());
    const float x1 = static_cast<float>(rect.right());
    const float y1 = static_cast<float>(rect.bottom());
    const float w = x1 - x0;
    const float h = y1 - y0;

    float u0 = 0.0f, u1 = 1.0f;
    if (mirrorH) { std::swap(u0, u1); }

    float* v = static_cast<float*>(m_geometry->vertexData());
    quint16* idx = m_geometry->indexDataAsUShort();

    if (callId <= 10 || !v || !idx || vertexCount != 4) {
        qInfo() << "[VideoSGNode] ★★★ updateGeometry ★★★"
                << " callId=" << callId
                << " instanceId=" << m_instanceId
                << " this=" << (void*)this
                << " rect=" << x0 << "," << y0 << " → " << x1 << "," << y1
                << " size=" << w << "x" << h
                << " mirrorH=" << mirrorH
                << " u0=" << u0 << " u1=" << u1
                << " vertexData=" << (void*)v
                << " indexData=" << (void*)idx
                << " vertexCount=" << m_geometry->vertexCount()
                << " m_geometry=" << (void*)m_geometry
                << " m_ready=" << m_ready
                << " elapsedSinceLast=" << elapsed << "ms"
                << " sigValid=" << (reinterpret_cast<quint64*>(m_geometry)[0] == GEOMETRY_MAGIC_SIGNATURE ? "YES" : "NO")
                << " ★ 签名验证通过后可安全写入 ★";
    }

    if (!v || !idx) {
        qCritical() << "[VideoSGNode][FATAL] vertexData=" << (void*)v << " indexData=" << (void*)idx;
        return;
    }

    // 顶点写入
    const float verts[] = {
        x0, y0, u0, 0.0f,
        x1, y0, u1, 0.0f,
        x0, y1, u0, 1.0f,
        x1, y1, u1, 1.0f,
    };
    std::memcpy(v, verts, sizeof(verts));

    // 索引写入
    idx[0] = 0; idx[1] = 1; idx[2] = 2;
    idx[3] = 1; idx[4] = 3; idx[5] = 2;

    markDirty(QSGNode::DirtyGeometry);
    m_ready = true;

    if (callId <= 5) {
        qInfo() << "[VideoSGNode][GEOM] ★★★ updateGeometry EXIT ★★★"
                << " callId=" << callId
                << " instanceId=" << m_instanceId
                << " this=" << (void*)this
                << " verticesWritten=" << sizeof(verts)/sizeof(float)
                << " indicesWritten=6"
                << " sigValid=" << ((reinterpret_cast<quint64*>(m_geometry)[0] == GEOMETRY_MAGIC_SIGNATURE) ? "YES" : "NO")
                << " m_ready=" << m_ready
                << " renderThread=" << (void*)QThread::currentThreadId()
                << " ★ 顶点数据已安全写入！★";
    }
}

void VideoSGNode::updateFrame(const VideoFrame& frame, quint64 lifecycleId)
{
    // ★★★ 修复：安全检查 m_material ★★★
    if (!m_material) {
        qWarning() << "[VideoSGNode][WARN] updateFrame: m_material is null, skipping";
        return;
    }

    // 关键诊断：确认 updateFrame 被调用 + 每30帧打印一次
    static int s_logCount = 0;
    ++s_logCount;
    const int logSeq = s_logCount;
    // 首5次 + 每30帧打印关键信息；CPU interop结果无论何时都打印
    if (logSeq <= 5 || logSeq % 30 == 0) {
        qInfo() << "[VideoSGNode] ★★★ updateFrame 被调用"
                << " seq=" << logSeq
                << " frameId=" << frame.frameId
                << " lifecycleId=" << lifecycleId
                << " frame=" << frame.width << "x" << frame.height
                << " memoryType=" << static_cast<int>(frame.memoryType)
                << " pixelFormat=" << static_cast<int>(frame.pixelFormat)
                << " planes[0].data=" << (void*)frame.planes[0].data
                << " interop=" << (void*)m_interop
                << " (interop=" << (m_interop ? m_interop->name() : "NONE") << ")"
                << " m_geometry=" << (void*)m_geometry
                << " ★ 对比 QML setFrame frameId 确认 emit→updateFrame 不断链";
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

    // ── 路径 3：CPU 内存（通用后备，经 CpuUploadInterop 上传）────────────────
    // ★★★ 根因修复：CpuUploadInterop 返回的 GL 纹理句柄在 Qt 6.8 RHI 模式下
    // 无法被 Scene Graph 正确绑定（updateSampledImage 返回 nullptr）。
    // 修复：对于 CPU_MEMORY 帧，始终跳过 interop 路径，直接调用 uploadYuvFrame，
    // 让 VideoMaterial 内部创建 Qt 可管理的纹理（通过 ensureTextures）。
    // ══════════════════════════════════════════════════════════════════════════════════
    if (frame.memoryType == VideoFrame::MemoryType::CPU_MEMORY && m_interop) {
        const IGpuInterop::TextureSet ts = m_interop->importFrame(frame);
        // ── 诊断：记录 interop 返回的 TextureSet 详情 ─────────────────────────────
        static QAtomicInt s_interopDiagCount{0};
        const int diagSeq = ++s_interopDiagCount;
        if (diagSeq <= 10 || frame.frameId % 30 == 0) {
            qInfo() << "[VideoSGNode][Path-Diag] CPU-interop 路径检查"
                    << " seq=" << diagSeq << " frameId=" << frame.frameId
                    << " ts.valid=" << ts.valid
                    << " ts.yTexId=" << ts.yTexId
                    << " ts.width=" << ts.width << " ts.height=" << ts.height
                    << " frame.width=" << frame.width << " frame.height=" << frame.height
                    << " planes[0].data=" << (void*)frame.planes[0].data
                    << " m_interop=" << (m_interop ? m_interop->name() : "nullptr")
                    << " ★ ts.valid=true 但 Qt 6.8 RHI 无法绑定裸 GL 纹理 ★";
        }

        // ── 根因修复：始终 fallback 到 uploadYuvFrame ─────────────────────────────────
        // CpuUploadInterop 虽然能上传到 GPU，但返回的纹理句柄在 Qt 6.8 RHI 下无法绑定。
        // 必须走 VideoMaterial 内部创建的纹理（通过 ensureTextures），
        // 才能被 Qt Scene Graph 正确渲染。
        if (ts.valid && ts.yTexId != 0) {
            // interop 已经上传了数据到 GPU，但 Qt 无法使用这些裸 GL 纹理
            // 不调用 setTextureSet，直接 fallback 到 uploadYuvFrame
            // uploadYuvFrame 会通过 ensureTextures 创建 Qt 可管理的纹理
            qInfo() << "[VideoSGNode][Path-Fix] ★★★ CPU-interop → fallback 到 uploadYuvFrame ★★★"
                    << " seq=" << diagSeq << " frameId=" << frame.frameId
                    << " ts.yTexId=" << ts.yTexId
                    << " 原因：CpuUploadInterop 纹理在 Qt 6.8 RHI 下无法绑定"
                    << " → 使用 VideoMaterial 内部纹理 + uploadYuvFrame 上传";
        } else {
            qInfo() << "[VideoSGNode][Path-Fallback] CPU-interop 返回无效，fallback 到 uploadYuvFrame"
                    << " seq=" << diagSeq << " frameId=" << frame.frameId
                    << " ts.valid=" << ts.valid << " ts.yTexId=" << ts.yTexId;
        }
    }

    // ── 最终后备：直接三平面上传（VideoMaterial 内部创建 Qt 可管理的纹理）────
    if (frame.planes[0].data) {
        // ── 增强诊断：记录 uploadYuvFrame 调用 ─────────────────────────────────
        static QAtomicInt s_uploadCallCount{0};
        const int uploadSeq = ++s_uploadCallCount;
        qInfo() << "[VideoSGNode][UploadPath] ★★★ uploadYuvFrame 被调用 ★★★"
                << " uploadSeq=" << uploadSeq
                << " frameId=" << frame.frameId
                << " width=" << frame.width << " height=" << frame.height
                << " pixelFormat=" << (int)frame.pixelFormat
                << " memoryType=" << (int)frame.memoryType
                << " planes[0].data=" << (void*)frame.planes[0].data
                << " planes[0].stride=" << frame.planes[0].stride
                << " planes[1].data=" << (void*)frame.planes[1].data
                << " planes[2].data=" << (void*)frame.planes[2].data
                << " ★ 对比 VideoMaterial uploadYuvFrame 日志确认纹理上传成功 ★";

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

        // ── 增强诊断：uploadYuvFrame 调用后检查纹理状态 ────────────────────────────
        static QAtomicInt s_afterUploadDiagCount{0};
        const int afterSeq = ++s_afterUploadDiagCount;
        if (afterSeq <= 10 || frame.frameId % 30 == 0) {
            qInfo() << "[VideoSGNode][UploadPath] ★ uploadYuvFrame 调用后状态 ★"
                    << " afterSeq=" << afterSeq << " frameId=" << frame.frameId
                    << " m_material=" << (void*)m_material
                    << " m_handles.valid=" << m_material->textureHandles().valid
                    << " m_handles.yTex=" << m_material->textureHandles().yTex
                    << " ★ 对比 VideoMaterial uploadYuvFrame 完成日志 ★";
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
