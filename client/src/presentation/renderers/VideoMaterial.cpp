#include "VideoMaterial.h"
#include <QDebug>
#include <QFile>
#include <QDateTime>
#include <QOpenGLFunctions>
#include <QOpenGLContext>
#include <QThread>
#include <rhi/qshader.h>
#include <QByteArray>
#include <QList>
#include <rhi/qshaderbaker.h>

// ── 全局纹理上传统计（用于诊断 GPU 渲染路径）───────────────────────────────
static QAtomicInt s_uploadTotal{0};
static QAtomicInt s_uploadGLFailure{0};
static int64_t s_uploadStatStart = 0;

// ── 着色器降级统计 ───────────────────────────────────────────────────────────
static QAtomicInt s_shaderFallbackActive{0};   // 1 = .qsb 缺失，使用内嵌 GLSL
static QAtomicInt s_shaderQsbActive{0};         // 1 = .qsb 加载成功（正常路径）

// ── v4 新增：VideoMaterial 实例追踪 ─────────────────────────────────────────
static QAtomicInt s_totalVideoMaterialInstances{0};

VideoMaterial::VideoMaterial()
    : m_instanceId(++s_totalVideoMaterialInstances)
{
    // ── ★★★ v4 增强：实例创建日志 ★★★
    qInfo() << "[Client][VideoMaterial] ★★★ 实例创建 ★★★"
             << " instanceId=" << m_instanceId
             << " totalInstances=" << static_cast<int>(s_totalVideoMaterialInstances)
             << " thread=" << (void*)QThread::currentThreadId()
             << " ★ 每个 VideoRenderer 应有独立 instanceId ★";

    setFlag(Blending, false);
    // ── NoBatching 与 type()==nullptr 双保险机制 ────────────────────────────────
    // ★★★ P0 修复（短期）：禁用 Scene Graph 批处理 ★★★
    //
    // 当前防护层次（由强到弱）：
    //   1. type() == nullptr  ← 最强：Qt 直接认为每个实例类型不同，绝不尝试批处理
    //   2. NoBatching == true ← 次强：即使 type() 相同，也明确禁止批处理
    //   3. compare() != 0     ← 兜底：不同纹理材质绝不返回 0
    //
    // NoBatching = kEnableNoBatching（当前 true）：
    //   true  = 禁用批处理（短期安全防护，draw call 略增但稳定）
    //   false = 启用 compare() 驱动的精确批处理（未来性能优化）
    //   当前 kEnableNoBatching=true 与 type()=nullptr 共存，双重保险。
    //
    // 官方文档参考：
    // https://doc.qt.io/qt-6.5/qsgmaterial.html#Flag-enum
    // ─────────────────────────────────────────────────────────────────────────
    setFlag(NoBatching, kEnableNoBatching);

    qInfo() << "[Client][VideoMaterial] ★★★ 构造函数完成 ★★★"
             << " instanceId=" << m_instanceId
             << " NoBatching=" << flags().testFlag(NoBatching)
             << " Blending=" << flags().testFlag(Blending);
}

VideoMaterial::~VideoMaterial()
{
    qInfo() << "[Client][VideoMaterial] ★★★ 实例销毁 ★★★"
             << " instanceId=" << m_instanceId
             << " hadTextures=" << m_ownTextures
             << " yTex=" << m_handles.yTex << " uvTex=" << m_handles.uvTex << " vTex=" << m_handles.vTex
             << " thread=" << (void*)QThread::currentThreadId()
             << " ★ 对比创建日志确认生命周期正确 ★";

    // 清理 QSGTexture 包装对象（m_textures 中的 unique_ptr 会自动释放）
    for (int i = 0; i < kMaxTextures; ++i) {
        if (m_textures[i]) {
            qDebug() << "[Client][VideoMaterial] ~VideoMaterial: 清理 QSGTexture binding=" << i
                     << " glTexId=" << m_textures[i]->glTextureId()
                     << " instanceId=" << m_instanceId;
            m_textures[i].reset();
        }
    }

    if (m_ownTextures) {
        qDebug() << "[Client][VideoMaterial] ~VideoMaterial: 销毁自主纹理 instanceId=" << m_instanceId;
        destroyOwnedTextures();
    }
}

QSGMaterialType* VideoMaterial::type() const
{
    // ── v4 关键：返回 nullptr 让 Scene Graph 不进行批处理 ★★★
    // 每个 VideoMaterial 实例有唯一的 instanceId，
    // 但共享同一个 type() 仍可能导致批处理尝试。
    // 返回 nullptr 确保 Scene Graph 为每个实例独立渲染。
    //
    // 注意：QSGMaterial::type() 返回 nullptr 是合法行为，
    // Qt Scene Graph 会将此类材质标记为"不可批处理"。
    // ─────────────────────────────────────────────────────────
    static int s_typeCallCount = 0;
    if (++s_typeCallCount <= 5) {
        qInfo() << "[Client][VideoMaterial] type() instanceId=" << m_instanceId
                 << " ★ 返回 nullptr 禁用批处理，安全 ★";
    }
    return nullptr;  // v4 关键修复：禁用批处理
}

QSGMaterialShader* VideoMaterial::createShader(QSGRendererInterface::RenderMode) const
{
    qInfo() << "[Client][VideoMaterial] createShader instanceId=" << m_instanceId
             << " ★ 为每个实例创建独立 Shader，避免共享状态冲突 ★";
    return new VideoShader();
}

// ─── compare ───────────────────────────────────────────────────────────────────

// Qt 官方文档（https://doc.qt.io/qt-6.5/qsgmaterial.html#compare）：
// "Compares this material to other and returns 0 if they are equal;
//  -1 if this material should sort before other and 1 if other should sort before."
//
// 注意：由于 type() 返回 nullptr（最强批处理防护），Qt Scene Graph 根本不会尝试
// 合并不同 VideoMaterial 实例（不同 type 不会被批处理），因此 compare() 实际上
// 不会被 Qt BatchRenderer 调用。
//
// compare() 的作用：
// 1. 当 type() != nullptr 时（未来移除 nullptr 返回值后），compare() 负责
//    按纹理句柄差异排序，防止不同纹理的实例被错误合并。
// 2. 当 type() == nullptr 时（当前），compare() 永远不会到达（Qt 在类型比较
//    阶段就跳过了批处理），但仍需实现以满足编译（.h 声明了 override）。
//
// 排序规则：按 (yTex, uvTex, vTex, isNv12) 字典序。
// 不同纹理的材质 compare() != 0 → 不被合并 → 不崩溃。
//
// 验证流程：运行时将 type() 改回返回有效指针，
// 若连续运行 30 分钟无 SIGSEGV，则 compare() 实现正确，可启用批处理优化。
//
// 参考：https://doc.qt.io/qt-6.5/qtquick-scenegraph-custommaterial-example.html
int VideoMaterial::compare(const QSGMaterial* other) const
{
    const VideoMaterial* o = static_cast<const VideoMaterial*>(other);

    // ── 纹理句柄排序键（稳定性：位运算避免 bool/float 不一致）────────────────
    // 核心规则：yTex ID 差异决定排序顺序
    // - yTex 更小的材质排在前面（-1）
    // - yTex 相同时，比较 uvTex
    // - 以此类推
    //
    // Qt 批处理器行为：
    // compare(a,b)==0 → a 和 b 可合并（当前 type()=nullptr 时不会到达此处）
    // compare(a,b)!=0 → a 和 b 不会被合并（当前 type()=nullptr 时不会到达此处）
    // ─────────────────────────────────────────────────────────────────────────
    const uint64_t thisKey = (static_cast<uint64_t>(m_handles.yTex) << 48)
                           | (static_cast<uint64_t>(m_handles.uvTex) << 32)
                           | (static_cast<uint64_t>(m_handles.vTex) << 16)
                           | (m_handles.isNv12 ? 1ULL : 0ULL);

    const uint64_t otherKey = (static_cast<uint64_t>(o->m_handles.yTex) << 48)
                            | (static_cast<uint64_t>(o->m_handles.uvTex) << 32)
                            | (static_cast<uint64_t>(o->m_handles.vTex) << 16)
                            | (o->m_handles.isNv12 ? 1ULL : 0ULL);

    if (thisKey < otherKey) return -1;
    if (thisKey > otherKey) return  1;
    return 0;
}

// ─── getSampledTexture ★★★ Qt 6.8 RHI 兼容核心实现 ★★★ ───────────────────────

// binding 0=Y, 1=UV/U, 2=V
// ⚠️ 此方法会修改 m_textures（创建/更新 QSGTexture 对象），不能是 const
QSGTexture* VideoMaterial::getSampledTexture(int binding)
{
    // ── 边界检查 ──────────────────────────────────────────────────────────────
    if (binding < 0 || binding >= kMaxTextures) {
        qWarning() << "[VideoMaterial][getSampledTexture] ★ 无效 binding ★"
                   << " binding=" << binding << " max=" << kMaxTextures
                   << " instanceId=" << m_instanceId;
        return nullptr;
    }

    // ── 获取纹理句柄 ────────────────────────────────────────────────────────
    uint32_t glTexId = 0;
    QSize texSize(m_handles.width, m_handles.height);

    switch (binding) {
    case 0: // Y 平面
        glTexId = m_handles.yTex;
        break;
    case 1: // UV 平面（NV12）或 U 平面（YUV420P）
        glTexId = m_handles.uvTex;
        texSize = QSize(m_handles.width / 2, m_handles.height / 2);
        break;
    case 2: // V 平面（仅 YUV420P）
        glTexId = m_handles.vTex;
        texSize = QSize(m_handles.width / 2, m_handles.height / 2);
        break;
    }

    // ── 纹理有效性检查 ──────────────────────────────────────────────────────
    if (glTexId == 0 || !m_handles.valid) {
        // ⚠️ 无有效 GL 纹理：返回 nullptr 让 Qt 使用默认纹理（全黑）
        static int s_nullWarnCount = 0;
        if (s_nullWarnCount++ < 10) {
            qWarning() << "[VideoMaterial][getSampledTexture] ★ GL 纹理无效 ★"
                       << " binding=" << binding << " glTexId=" << glTexId
                       << " valid=" << m_handles.valid
                       << " instanceId=" << m_instanceId;
        }
        return nullptr;
    }

    // ── 获取或创建 QSGTexture 对象 ───────────────────────────────────────────
    // m_textures[binding] 可能为空（首次调用）或已存在（后续帧复用）
    auto& texPtr = m_textures[binding];

    if (!texPtr) {
        // 首次：创建 GLExternalTexture 对象
        texPtr = std::make_unique<GLExternalTexture>(glTexId, texSize, false);
        qInfo() << "[VideoMaterial][getSampledTexture] ★ 首次创建纹理对象 ★"
                << " binding=" << binding << " glTexId=" << glTexId
                << " size=" << texSize << " instanceId=" << m_instanceId;
    } else {
        // 后续帧：更新 GL 纹理 ID（帧尺寸变化时）
        texPtr->setGlTextureId(glTexId);
        texPtr->setTextureSize(texSize);
    }

    return texPtr.get();
}

// ─── textureStateDiag ─────────────────────────────────────────────────────────
QString VideoMaterial::textureStateDiag() const
{
    QString info;
    QTextStream ts(&info);
    ts << "VideoMaterial[" << m_instanceId << "] state:\n";
    ts << "  valid=" << m_handles.valid << "\n";
    ts << "  yTex=" << m_handles.yTex << "\n";
    ts << "  uvTex=" << m_handles.uvTex << "\n";
    ts << "  vTex=" << m_handles.vTex << "\n";
    ts << "  size=" << m_handles.width << "x" << m_handles.height << "\n";
    ts << "  isNv12=" << m_handles.isNv12 << "\n";
    ts << "  ownTextures=" << m_ownTextures << "\n";
    ts << "  gpuNative=" << m_gpuNative << "\n";
    ts << "  lastUpload=" << m_lastUploadTimeMs << "ms ago";
    return info;
}

// ─── setTextureSet ────────────────────────────────────────────────────────────

void VideoMaterial::setTextureSet(const IGpuInterop::TextureSet& ts)
{
    if (!ts.valid) {
        qWarning() << "[Client][VideoMaterial] setTextureSet: 无效的 TextureSet instanceId=" << m_instanceId;
        return;
    }

    auto* gl = QOpenGLContext::currentContext();
    if (!gl) {
        qCritical() << "[Client][VideoMaterial] setTextureSet: 无当前 GL 上下文! instanceId=" << m_instanceId;
        return;
    }

    // GPU interop 管理纹理，本类不拥有
    if (m_ownTextures) {
        qDebug() << "[Client][VideoMaterial] setTextureSet: 销毁自主纹理后接收 GPU interop instanceId=" << m_instanceId;
        destroyOwnedTextures();
        m_ownTextures = false;
    }

    m_handles.yTex   = ts.yTexId;
    m_handles.uvTex  = ts.uvTexId;
    m_handles.vTex   = ts.vTexId;
    m_handles.width  = ts.width;
    m_handles.height = ts.height;
    m_handles.isNv12 = ts.isNv12;
    m_handles.valid  = true;
    m_gpuNative      = true;

    m_lastUploadTimeMs = QDateTime::currentMSecsSinceEpoch();

    qDebug() << "[Client][VideoMaterial] setTextureSet: GPU 纹理 y=" << ts.yTexId
             << " uv=" << ts.uvTexId << " v=" << ts.vTexId
             << " size=" << ts.width << "x" << ts.height << " isNv12=" << ts.isNv12
             << " instanceId=" << m_instanceId
             << " ★ GPU-interop 路径成功（DMA-BUF/VAAPI 零拷贝） ★";
}

// ─── uploadYuvFrame ───────────────────────────────────────────────────────────

void VideoMaterial::uploadYuvFrame(
    const uint8_t* yPlane, int yStride,
    const uint8_t* uPlane, int uStride,
    const uint8_t* vPlane, int vStride,
    int width, int height, bool isNv12)
{
    // ── ★★★ 增强诊断：记录 uploadYuvFrame 每次调用 ★★★
    static QAtomicInt s_uploadYuvFrameCount{0};
    const int callSeq = ++s_uploadYuvFrameCount;
    const int64_t callTime = QDateTime::currentMSecsSinceEpoch();

    qInfo() << "[VideoMaterial][uploadYuvFrame] ★★★ 函数被调用 ★★★"
            << " callSeq=" << callSeq
            << " width=" << width << " height=" << height
            << " isNv12=" << isNv12
            << " yPlane=" << (void*)yPlane << " yStride=" << yStride
            << " uPlane=" << (void*)uPlane << " uStride=" << uStride
            << " vPlane=" << (void*)vPlane << " vStride=" << vStride
            << " m_instanceId=" << m_instanceId
            << " callTime=" << callTime
            << " ★ 对比 VideoSGNode UploadPath 日志确认调用链正确 ★";

    // ── v4 增强：GL 上下文完整性验证 ─────────────────────────────────────
    auto* gl = QOpenGLContext::currentContext();
    auto* f = gl ? gl->functions() : nullptr;

    if (!gl || !f) {
        ++s_uploadGLFailure;
        static int s_ctxWarnCount = 0;
        if (s_ctxWarnCount++ < 5) {
            qCritical() << "[VideoMaterial][GL-FAIL] ★★★ uploadYuvFrame: GL 上下文无效！★★★"
                        << " instanceId=" << m_instanceId
                        << " gl=" << (void*)gl << " func=" << (void*)f
                        << " failureCount=" << static_cast<int>(s_uploadGLFailure)
                        << " callSeq=" << callSeq
                        << " ★ 视频无法上传到 GPU！检查 Qt OpenGL 初始化 ★";
        }
        return;
    }

    // ── 初始化统计周期 ─────────────────────────────────────────────────────
    const int64_t nowMs = QDateTime::currentMSecsSinceEpoch();
    if (s_uploadStatStart == 0) { s_uploadStatStart = nowMs; }

    ++s_uploadTotal;

    // ── 首次帧/每30帧统计报告 ─────────────────────────────────────────────
    if (callSeq <= 5 || callSeq % 30 == 0) {
        qInfo() << "[Client][VideoMaterial] ★★★ uploadYuvFrame"
                << " instanceId=" << m_instanceId
                << " seq=" << callSeq
                << " yPlane=" << (void*)yPlane
                << " w=" << width << " h=" << height
                << " yStride=" << yStride << " isNv12=" << isNv12
                << " total=" << static_cast<int>(s_uploadTotal)
                << " glFailure=" << static_cast<int>(s_uploadGLFailure)
                << " gl=" << (void*)gl
                << " ★ 对比 VideoSGNode UploadPath 日志确认此函数被调用 ★";
    }

    // ── 帧率统计（每60帧）──────────────────────────────────────────────────
    if (callSeq % 60 == 0 && callSeq > 0) {
        const int64_t elapsed = nowMs - s_uploadStatStart;
        const double fps = (elapsed > 0) ? (static_cast<int>(s_uploadTotal) * 1000.0 / elapsed) : 0.0;
        qInfo() << "[Client][VideoMaterial][FPS-STATS] ★ 每60帧统计 ★"
                << " instanceId=" << m_instanceId
                << " total=" << static_cast<int>(s_uploadTotal)
                << " elapsed=" << elapsed << "ms"
                << " fps=" << fps
                << " glFailure=" << static_cast<int>(s_uploadGLFailure)
                << " glFailureRate=" << (static_cast<int>(s_uploadTotal) > 0
                    ? (100.0 * static_cast<int>(s_uploadGLFailure) / static_cast<int>(s_uploadTotal)) : 0.0) << "%";
    }

    if (!yPlane) {
        qWarning() << "[VideoMaterial] uploadYuvFrame: yPlane 为空 instanceId=" << m_instanceId;
        return;
    }

    // ── ★★★ 增强诊断：ensureTextures 调用前状态 ★★★
    static int s_preEnsureCount = 0;
    if (++s_preEnsureCount <= 5 || callSeq % 30 == 0) {
        qInfo() << "[VideoMaterial][Pre-Upload] ★ ensureTextures 调用前状态 ★"
                << " callSeq=" << callSeq
                << " m_handles.valid=" << m_handles.valid
                << " m_handles.yTex=" << m_handles.yTex
                << " m_ownTextures=" << m_ownTextures
                << " width=" << width << " height=" << height
                << " isNv12=" << isNv12
                << " ★ ensureTextures 将创建/复用 Qt 可管理的纹理 ★";
    }

    ensureTextures(width, height, isNv12);

    // ── ★★★ 增强诊断：ensureTextures 调用后状态 ★★★
    static int s_postEnsureCount = 0;
    if (++s_postEnsureCount <= 5 || callSeq % 30 == 0) {
        qInfo() << "[VideoMaterial][Post-Upload] ★ ensureTextures 调用后状态 ★"
                << " callSeq=" << callSeq
                << " m_handles.valid=" << m_handles.valid
                << " m_handles.yTex=" << m_handles.yTex
                << " m_handles.uvTex=" << m_handles.uvTex
                << " m_handles.vTex=" << m_handles.vTex
                << " m_handles.width=" << m_handles.width
                << " m_handles.height=" << m_handles.height
                << " m_handles.isNv12=" << m_handles.isNv12
                << " ★ 如果 m_handles.yTex=0 → ensureTextures 失败，检查 GL 错误 ★";
    }

    if (!m_handles.valid) {
        qCritical() << "[VideoMaterial][FATAL] uploadYuvFrame: ensureTextures 后纹理仍无效！"
                       " 视频将不显示（黑屏）！\n"
                       << " instanceId=" << m_instanceId
                       << " w=" << width << " h=" << height << " isNv12=" << isNv12
                       << " callSeq=" << callSeq;
        return;
    }

    // ── v4 增强：清除之前的 GL 错误状态 ─────────────────────────────────
    while (f->glGetError() != GL_NO_ERROR) { /* clear pending errors */ }
    GLuint preErr = f->glGetError();
    if (preErr != GL_NO_ERROR) {
        qCritical() << "[VideoMaterial][GL-STATE] ★★★ 上传前 GL 错误状态！★★★"
                    << " instanceId=" << m_instanceId
                    << " preErr=" << preErr
                    << " ★ 之前的 uploadYuvFrame 可能有问题！★★★";
    }

    // ── v4 纹理有效性二次确认 ────────────────────────────────────────────
    if (callSeq <= 5) {
        qInfo() << "[VideoMaterial] ★★★ 纹理有效，开始上传数据 ★★★"
                << " instanceId=" << m_instanceId
                << " seq=" << callSeq
                << " yTex=" << m_handles.yTex << " uvTex=" << m_handles.uvTex << " vTex=" << m_handles.vTex
                << " preErr=" << preErr;
    }

    // ── 上传 Y 平面（GL_R8，全分辨率）────────────────────────────────
    f->glPixelStorei(GL_UNPACK_ROW_LENGTH, yStride);
    f->glBindTexture(GL_TEXTURE_2D, m_handles.yTex);
    f->glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height,
                       GL_RED, GL_UNSIGNED_BYTE, yPlane);
    GLuint yErr = f->glGetError();
    if (yErr != GL_NO_ERROR) {
        qCritical() << "[VideoMaterial] uploadYuvFrame: glTexSubImage2D(Y) 失败"
                    << " instanceId=" << m_instanceId
                    << " err=" << yErr << " yTex=" << m_handles.yTex;
    } else if (callSeq <= 5) {
        qInfo() << "[VideoMaterial] glTexSubImage2D(Y) 成功 yTex=" << m_handles.yTex;
    }

    if (isNv12) {
        // NV12：UV 交织平面（GL_RG8，半分辨率）
        if (uPlane) {
            f->glPixelStorei(GL_UNPACK_ROW_LENGTH, uStride);
            f->glBindTexture(GL_TEXTURE_2D, m_handles.uvTex);
            f->glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width / 2, height / 2,
                               GL_RG, GL_UNSIGNED_BYTE, uPlane);
            GLuint uvErr = f->glGetError();
            if (uvErr != GL_NO_ERROR) {
                qCritical() << "[VideoMaterial] uploadYuvFrame: glTexSubImage2D(UV/NV12) 失败"
                             << " instanceId=" << m_instanceId
                             << " err=" << uvErr << " uvTex=" << m_handles.uvTex;
            } else if (callSeq <= 5) {
                qInfo() << "[VideoMaterial] glTexSubImage2D(UV/NV12) 成功 uvTex=" << m_handles.uvTex;
            }
        }
    } else {
        // YUV420P：独立 U 和 V 平面（各 GL_R8，半分辨率）
        if (uPlane) {
            f->glPixelStorei(GL_UNPACK_ROW_LENGTH, uStride);
            f->glBindTexture(GL_TEXTURE_2D, m_handles.uvTex);
            f->glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width / 2, height / 2,
                               GL_RED, GL_UNSIGNED_BYTE, uPlane);
            GLuint uErr = f->glGetError();
            if (uErr != GL_NO_ERROR) {
                qCritical() << "[VideoMaterial] uploadYuvFrame: glTexSubImage2D(U) 失败"
                             << " instanceId=" << m_instanceId
                             << " err=" << uErr << " uvTex=" << m_handles.uvTex;
            }
        }
        if (vPlane && m_handles.vTex) {
            f->glPixelStorei(GL_UNPACK_ROW_LENGTH, vStride);
            f->glBindTexture(GL_TEXTURE_2D, m_handles.vTex);
            f->glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width / 2, height / 2,
                               GL_RED, GL_UNSIGNED_BYTE, vPlane);
            GLuint vErr = f->glGetError();
            if (vErr != GL_NO_ERROR) {
                qCritical() << "[VideoMaterial] uploadYuvFrame: glTexSubImage2D(V) 失败"
                             << " instanceId=" << m_instanceId
                             << " err=" << vErr << " vTex=" << m_handles.vTex;
            }
        }
    }

    f->glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    f->glBindTexture(GL_TEXTURE_2D, 0);

    // ── v4 增强：上传后 GL 错误检查 + 更新时间戳 ─────────────────────────
    GLuint finalErr = f->glGetError();
    const int64_t uploadDoneTime = QDateTime::currentMSecsSinceEpoch();
    const int64_t uploadCost = uploadDoneTime - callTime;

    if (finalErr != GL_NO_ERROR) {
        // ★★★ 致命：GL 操作失败！视频将不显示（黑屏）★★★
        qCritical() << "[VideoMaterial][FATAL] uploadYuvFrame: GL 错误"
                    << " instanceId=" << m_instanceId
                    << " err=" << finalErr
                    << " yTex=" << m_handles.yTex
                    << " uvTex=" << m_handles.uvTex << " vTex=" << m_handles.vTex
                    << " seq=" << callSeq
                    << " uploadCost=" << uploadCost << "ms";
        ++s_uploadGLFailure;
    } else {
        m_lastUploadTimeMs = nowMs;  // v4：成功上传时更新时间戳

        if (callSeq <= 5 || callSeq % 30 == 0) {
            qInfo() << "[VideoMaterial] ★★★ uploadYuvFrame 完成（GL 成功）★★★"
                    << " instanceId=" << m_instanceId
                    << " seq=" << callSeq
                    << " yTex=" << m_handles.yTex << " uvTex=" << m_handles.uvTex << " vTex=" << m_handles.vTex
                    << " size=" << width << "x" << height << " isNv12=" << isNv12
                    << " uploadCost=" << uploadCost << "ms"
                    << " ★ 若纹理有效但屏幕全黑 → 检查 VideoShader .qsb 是否编译成功 ★"
                    << " ★ 对比 VideoSGNode UploadPath 后日志确认渲染链路 ★";
        }
    }
}

// ─── setGpuTexture ────────────────────────────────────────────────────────────

void VideoMaterial::setGpuTexture(uint32_t glTextureId, int width, int height)
{
    auto* gl = QOpenGLContext::currentContext();
    if (!gl) {
        qCritical() << "[Client][VideoMaterial] setGpuTexture: 无当前 GL 上下文! instanceId=" << m_instanceId;
        return;
    }

    if (m_ownTextures) {
        qDebug() << "[Client][VideoMaterial] setGpuTexture: 销毁自主纹理 instanceId=" << m_instanceId;
        destroyOwnedTextures();
        m_ownTextures = false;
    }
    m_handles.yTex   = glTextureId;
    m_handles.uvTex  = 0;
    m_handles.vTex   = 0;
    m_handles.width  = width;
    m_handles.height = height;
    m_handles.isNv12 = false;
    m_handles.valid  = (glTextureId != 0);
    m_gpuNative      = true;
    m_lastUploadTimeMs = QDateTime::currentMSecsSinceEpoch();

    qDebug() << "[Client][VideoMaterial] setGpuTexture: id=" << glTextureId
             << " size=" << width << "x" << height
             << " instanceId=" << m_instanceId;
}

// ─── private helpers ─────────────────────────────────────────────────────────

void VideoMaterial::ensureTextures(int width, int height, bool isNv12)
{
    static int s_ensureLogCount = 0;
    const int seq = ++s_ensureLogCount;

    // ── 1. 尺寸有效性检查 ──────────────────────────────────────────────────
    if (width <= 0 || height <= 0) {
        qWarning() << "[Client][VideoMaterial][ensureTextures] ★ 无效尺寸，跳过"
                   << " instanceId=" << m_instanceId
                   << " seq=" << seq << " w=" << width << " h=" << height;
        return;
    }

    // ── 2. 尺寸未变化检查 ──────────────────────────────────────────────────
    if (m_handles.valid && m_handles.width == width &&
        m_handles.height == height && m_handles.isNv12 == isNv12) {
        return;
    }

    // ── 3. 销毁旧纹理 ──────────────────────────────────────────────────────
    if (m_ownTextures) {
        qDebug() << "[Client][VideoMaterial][ensureTextures] 重建纹理"
                 << " instanceId=" << m_instanceId
                 << " seq=" << seq
                 << " old=" << m_handles.width << "x" << m_handles.height
                 << " new=" << width << "x" << height << " isNv12=" << isNv12;
        try {
            destroyOwnedTextures();
        } catch (const std::exception& e) {
            qWarning() << "[Client][VideoMaterial][ensureTextures] destroyOwnedTextures 异常:"
                       << e.what();
        }
    }

    // ── 4. GL 上下文检查 ─────────────────────────────────────────────────
    auto* gl = QOpenGLContext::currentContext();
    if (!gl) {
        qCritical() << "[Client][VideoMaterial][ensureTextures] ★ CRITICAL: 无当前 GL 上下文!"
                        " 纹理创建将失败，视频将不显示！"
                        << " instanceId=" << m_instanceId
                        << " seq=" << seq;
        return;
    }

    auto* f = gl->functions();
    if (!f) {
        qCritical() << "[Client][VideoMaterial][ensureTextures] ★ CRITICAL: GL functions 不可用!"
                        << " instanceId=" << m_instanceId
                        << " seq=" << seq;
        return;
    }

    // ── 5. 创建纹理 ───────────────────────────────────────────────────────
    auto makeTexture = [&](uint32_t& id, int w, int h, GLenum internalFmt, GLenum fmt, const char* name) -> bool {
        try {
            f->glGenTextures(1, &id);
            GLuint genErr = f->glGetError();
            if (genErr != GL_NO_ERROR) {
                qCritical() << "[Client][VideoMaterial][ensureTextures] glGenTextures 失败"
                           << " instanceId=" << m_instanceId
                           << " name=" << name << " id=" << id << " err=" << genErr;
                id = 0;
                return false;
            }
            if (id == 0) {
                qCritical() << "[Client][VideoMaterial][ensureTextures] glGenTextures 返回 0"
                           << " instanceId=" << m_instanceId
                           << " name=" << name;
                return false;
            }

            f->glBindTexture(GL_TEXTURE_2D, id);
            GLuint bindErr = f->glGetError();
            if (bindErr != GL_NO_ERROR) {
                qCritical() << "[Client][VideoMaterial][ensureTextures] glBindTexture 失败"
                           << " instanceId=" << m_instanceId
                           << " name=" << name << " id=" << id << " err=" << bindErr;
                f->glDeleteTextures(1, &id);
                id = 0;
                return false;
            }

            f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

            f->glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(internalFmt),
                            w, h, 0, fmt, GL_UNSIGNED_BYTE, nullptr);
            GLuint texErr = f->glGetError();
            if (texErr != GL_NO_ERROR) {
                qCritical() << "[Client][VideoMaterial][ensureTextures] glTexImage2D 失败"
                           << " instanceId=" << m_instanceId
                           << " name=" << name << " id=" << id << " w=" << w << " h=" << h
                           << " internalFmt=" << internalFmt << " fmt=" << fmt << " err=" << texErr;
                f->glDeleteTextures(1, &id);
                id = 0;
                return false;
            }

            qDebug() << "[Client][VideoMaterial][ensureTextures] 创建纹理成功"
                     << " instanceId=" << m_instanceId
                     << " name=" << name << " id=" << id << " size=" << w << "x" << h
                     << " internalFmt=0x" << QString::number(internalFmt, 16)
                     << " fmt=0x" << QString::number(fmt, 16);
            return true;
        } catch (const std::exception& e) {
            qCritical() << "[Client][VideoMaterial][ensureTextures] 创建纹理异常"
                       << " instanceId=" << m_instanceId
                       << " name=" << name << " error=" << e.what();
            if (id != 0) {
                try { f->glDeleteTextures(1, &id); } catch (...) {}
                id = 0;
            }
            return false;
        } catch (...) {
            qCritical() << "[Client][VideoMaterial][ensureTextures] 创建纹理未知异常"
                       << " instanceId=" << m_instanceId
                       << " name=" << name;
            if (id != 0) {
                try { f->glDeleteTextures(1, &id); } catch (...) {}
                id = 0;
            }
            return false;
        }
    };

    bool yOk = makeTexture(m_handles.yTex, width, height, GL_R8, GL_RED, "Y");
    bool uOk, vOk;
    if (isNv12) {
        uOk = makeTexture(m_handles.uvTex, width / 2, height / 2, GL_RG8, GL_RG, "UV");
        m_handles.vTex = 0;
        vOk = true;
    } else {
        uOk = makeTexture(m_handles.uvTex, width / 2, height / 2, GL_R8, GL_RED, "U");
        vOk = makeTexture(m_handles.vTex, width / 2, height / 2, GL_R8, GL_RED, "V");
    }

    // ── 6. 结果检查 ──────────────────────────────────────────────────────
    if (yOk && uOk && vOk) {
        m_handles.width  = width;
        m_handles.height = height;
        m_handles.isNv12 = isNv12;
        m_handles.valid  = true;
        m_ownTextures    = true;
        m_gpuNative      = false;

        f->glBindTexture(GL_TEXTURE_2D, 0);
        GLuint finalErr = f->glGetError();
        if (seq <= 5) {
            qInfo() << "[Client][VideoMaterial][ensureTextures] ★ 纹理创建完成"
                     << " instanceId=" << m_instanceId
                     << " seq=" << seq
                     << " yTex=" << m_handles.yTex << " uvTex=" << m_handles.uvTex << " vTex=" << m_handles.vTex
                     << " finalGLerr=" << finalErr;
        }
    } else {
        qCritical() << "[Client][VideoMaterial][ensureTextures] ★ CRITICAL: 纹理创建失败"
                     << " instanceId=" << m_instanceId
                     << " seq=" << seq
                     << " yOk=" << yOk << " uOk=" << uOk << " vOk=" << vOk
                     << " yTex=" << m_handles.yTex << " uvTex=" << m_handles.uvTex << " vTex=" << m_handles.vTex;
        m_handles.valid = false;
    }
}

void VideoMaterial::destroyOwnedTextures()
{
    auto* gl = QOpenGLContext::currentContext();
    if (!gl || !m_handles.valid) return;
    auto* f = gl->functions();

    if (m_handles.yTex)  f->glDeleteTextures(1, &m_handles.yTex);
    if (m_handles.uvTex) f->glDeleteTextures(1, &m_handles.uvTex);
    if (m_handles.vTex)  f->glDeleteTextures(1, &m_handles.vTex);
    m_handles = {};
}

// ─── VideoShader ──────────────────────────────────────────────────────────────

// ─── 内嵌 GLSL 源码（.qsb 不可用时的安全降级）────────────────────────────────
// 使用 QShaderBaker 将 GLSL 440 编译为 QShader，再通过 setShader() 注入。
// QShaderBaker 是 Qt6::ShaderTools 的一部分，CMake 已将其链接到本项目。
// 参考：https://doc.qt.io/qt-6.8/qshaderbaker.html
//       https://doc.qt.io/qt-6.8/qsgmaterialshader.html#setShader
static QShader bakeShader(QShader::Stage stage, const char* glslSource) {
    QShaderBaker baker;
    baker.setSourceString(QByteArray(glslSource), stage);
    baker.setGeneratedShaderVariants({ QShader::StandardShader });
    QList<QShaderBaker::GeneratedShader> targets;
    // 为 Scene Graph RHI 后端生成 SPIR-V + GLSL ES 100（移动端）+ GLSL 120（桌面 OpenGL）
    targets.append({ QShader::SpirvShader, QShaderVersion(100) });
    targets.append({ QShader::GlslShader, QShaderVersion(100, QShaderVersion::GlslEs) });
    targets.append({ QShader::GlslShader, QShaderVersion(120) });
    targets.append({ QShader::HlslShader, QShaderVersion(50) });
    targets.append({ QShader::MslShader, QShaderVersion(12) });
    baker.setGeneratedShaders(targets);
    const QShader result = baker.bake();
    if (!result.isValid()) {
        qFatal("[VideoShader] ✗ CRITICAL: GLSL bake() 失败！stage=%d error=%s",
               (int)stage, qPrintable(baker.errorMessage()));
    }
    return result;
}

VideoShader::VideoShader()
{
    const QString vertQsbPath = QStringLiteral(":/shaders/video.vert.qsb");
    const QString fragQsbPath = QStringLiteral(":/shaders/video.frag.qsb");
    QFile vertQsb(vertQsbPath);
    QFile fragQsb(fragQsbPath);
    const bool vertExists = vertQsb.open(QIODevice::ReadOnly);
    const bool fragExists = fragQsb.open(QIODevice::ReadOnly);

    if (vertExists && fragExists) {
        setShaderFileName(VertexStage, vertQsbPath);
        setShaderFileName(FragmentStage, fragQsbPath);
        qInfo() << "[VideoShader] ✓ .qsb loaded (precompiled, fastest path)"
                << " vert=" << vertQsbPath << " frag=" << fragQsbPath;
        s_shaderQsbActive.ref();
    } else {
        qCritical() << "[VideoShader] ✗ .qsb NOT FOUND — using inline GLSL fallback (slower first-run compile)"
                    << " vertExists=" << vertExists << " fragExists=" << fragExists;
        qCritical() << "[VideoShader]    BUILD FIX: ensure qsb in PATH, re-run cmake, then:"
                    << " cmake --build . --target shader_compilation";

        // 内嵌顶点着色器
        static const char* s_vertGLSL = R"(
            #version 440
            layout(location = 0) in vec4 position;
            layout(location = 1) in vec2 texcoord;
            layout(location = 0) out vec2 vTexCoord;
            layout(std140, binding = 0) uniform buf {
                mat4  qt_Matrix;
                int   isNv12;
                float opacity;
            } ubuf;
            void main() {
                gl_Position = ubuf.qt_Matrix * position;
                vTexCoord   = texcoord;
            }
        )";

        // 内嵌片段着色器（YUV420P + NV12，BT.709）
        static const char* s_fragGLSL = R"(
            #version 440
            layout(location = 0) in  vec2 vTexCoord;
            layout(location = 0) out vec4 fragColor;
            layout(binding = 0) uniform sampler2D yTex;
            layout(binding = 1) uniform sampler2D uvTex;
            layout(binding = 2) uniform sampler2D vTex;
            layout(std140, binding = 0) uniform buf {
                mat4  qt_Matrix;
                int   isNv12;
                float opacity;
            } ubuf;
            vec3 yuv2rgb_bt709(float y, float u, float v) {
                y = (y - 16.0/255.0) * (255.0/219.0);
                u =  u - 128.0/255.0;
                v =  v - 128.0/255.0;
                float r = y + 1.5748 * v;
                float g = y - 0.1873 * u - 0.4681 * v;
                float b = y + 1.8556 * u;
                return clamp(vec3(r, g, b), 0.0, 1.0);
            }
            void main() {
                float y, u, v;
                if (ubuf.isNv12 == 1) {
                    y = texture(yTex, vTexCoord).r;
                    vec2 uv = texture(uvTex, vTexCoord).rg;
                    u = uv.r; v = uv.g;
                } else {
                    y = texture(yTex, vTexCoord).r;
                    u = texture(uvTex, vTexCoord).r;
                    v = texture(vTex,  vTexCoord).r;
                }
                fragColor = vec4(yuv2rgb_bt709(y, u, v), 1.0) * ubuf.opacity;
            }
        )";

        const QShader vert = bakeShader(QShader::VertexStage, s_vertGLSL);
        const QShader frag = bakeShader(QShader::FragmentStage, s_fragGLSL);

        setShader(VertexStage, vert);
        setShader(FragmentStage, frag);
        qInfo() << "[VideoShader] ✓ inline GLSL compiled (fallback path active)";
        s_shaderFallbackActive.ref();
    }

    qDebug() << "[VideoShader] constructed";
}

void VideoShader::checkCompilationStatus()
{
    if (m_shaderChecked) return;
    m_shaderChecked = true;

    // 验证 .qsb 着色器（CMake 已验证文件非空，此处用 QShader::fromSerialized 做运行时检查）
    auto loadShaderFromQrc = [](const QString& path) -> QShader {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            return QShader();
        }
        return QShader::fromSerialized(f.readAll());
    };

    const QShader vert = loadShaderFromQrc(":/shaders/video.vert.qsb");
    const QShader frag = loadShaderFromQrc(":/shaders/video.frag.qsb");

    if (!vert.isValid()) {
        qCritical() << "[VideoShader] ✗ vertex shader FAILED to compile — check qsb build output for errors";
    }
    if (!frag.isValid()) {
        qCritical() << "[VideoShader] ✗ fragment shader FAILED to compile — check qsb build output for errors";
    }
    if (vert.isValid() && frag.isValid()) {
        qInfo() << "[VideoShader] ✓ both shaders validated (compiled successfully)";
    }
}

bool VideoShader::updateUniformData(RenderState& state, QSGMaterial* mat,
                                     QSGMaterial* /*old*/)
{
    bool changed = false;
    QByteArray* buf = state.uniformData();

    if (state.isMatrixDirty()) {
        const QMatrix4x4 m = state.combinedMatrix();
        // offset 0: mat4 MVP (64 bytes)
        memcpy(buf->data(), m.constData(), 64);
        changed = true;
    }

    // offset 64: int isNv12 (4 bytes, padded to 16)
    VideoMaterial* vm = static_cast<VideoMaterial*>(mat);
    const int isNv12 = vm->textureHandles().isNv12 ? 1 : 0;
    int stored;
    memcpy(&stored, buf->data() + 64, sizeof(int));
    if (stored != isNv12) {
        memcpy(buf->data() + 64, &isNv12, sizeof(int));
        changed = true;
    }

    return changed;
}

// ═════════════════════════════════════════════════════════════════════════════════════════════════════
// ★★★ Qt 6.8 RHI 兼容修复：返回有效的 QSGTexture* ★★★
//
// 问题根因：qsgplaintexture_p.h 私有头文件不可用，之前的修复返回 nullptr。
// updateSampledImage() 返回 nullptr 导致 Qt Scene Graph 使用默认透明纹理 → 黑屏。
//
// 解决方案：
// - 自定义 QSGTexture 子类 GLExternalTexture：包装外部 GL 纹理 ID
// - getSampledTexture()：根据 binding 返回对应的 GLExternalTexture 实例
// - updateSampledImage()：调用 getSampledTexture() 返回有效 QSGTexture*
//
// 参考：
// https://doc.qt.io/qt-6.8/qsgmaterialshader.html#updateSampledImage
// https://doc.qt.io/qt-6.8/qsgtexture.html（"users can implement custom texture classes"）
// ═════════════════════════════════════════════════════════════════════════════════════════════════════
void VideoShader::updateSampledImage(RenderState& /*state*/, int binding,
                                     QSGTexture** texture,
                                     QSGMaterial* mat, QSGMaterial* /*old*/)
{
    auto* vm = static_cast<VideoMaterial*>(mat);
    const auto& h = vm->textureHandles();

    // ── 增强诊断：每次调用都记录纹理绑定详情 ─────────────────────────────────
    static QAtomicInt s_bindingCount{0};
    const int seq = ++s_bindingCount;

    // binding 0=Y, 1=UV/U, 2=V
    static const char* s_bindingNames[] = { "Y(tex0)", "UV/U(tex1)", "V(tex2)" };
    const char* name = (binding >= 0 && binding <= 2) ? s_bindingNames[binding] : "unknown";

    qInfo() << "[VideoShader][updateSampledImage] ★★★ Qt 6.8 RHI 兼容 ★★★"
            << " seq=" << seq
            << " binding=" << binding << "(" << name << ")"
            << " h.valid=" << h.valid
            << " h.yTex=" << h.yTex << " h.uvTex=" << h.uvTex << " h.vTex=" << h.vTex
            << " h.width=" << h.width << " h.height=" << h.height << " h.isNv12=" << h.isNv12
            << " instanceId=" << vm->instanceId();

    // ── 获取有效的 QSGTexture*（核心修复）───────────────────────────────────
    QSGTexture* tex = vm->getSampledTexture(binding);

    if (!tex) {
        // ⚠️ 纹理无效：Qt Scene Graph 将使用默认纹理（全黑）
        static int s_nullWarnCount = 0;
        if (s_nullWarnCount++ < 10) {
            qWarning() << "[VideoShader][updateSampledImage] ★ WARNING: QSGTexture 为 nullptr ★"
                       << " seq=" << seq << " binding=" << binding
                       << " h.valid=" << h.valid << " h.yTex=" << h.yTex
                       << " ★ Scene Graph 将绑定无效/默认纹理（全黑）★";
        }
    } else if (seq <= 5) {
        qInfo() << "[VideoShader][updateSampledImage] ★ QSGTexture 有效 ★"
                << " seq=" << seq << " binding=" << binding
                << " tex=" << (void*)tex
                << " texSize=" << tex->textureSize()
                << " ★ Qt Scene Graph RHI 将正确绑定 GL 纹理 ★";
    }

    // ── 返回 QSGTexture*（nullptr 或有效实例）────────────────────────────────
    *texture = tex;
}