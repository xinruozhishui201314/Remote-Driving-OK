#pragma once
#include <QSGMaterial>
#include <QSGMaterialShader>
#include <QSGTexture>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QQuickWindow>
#include <QSize>
#include <memory>
#include <array>
#include "../../infrastructure/media/gpu/IGpuInterop.h"

/**
 * ★★★ Qt 6 RHI 兼容纹理包装器 ★★★
 *
 * 问题：qsgplaintexture_p.h 是私有头文件，系统中不可用。
 * Qt 6 公开 API QSGPlainTexture::setTextureFromNativeTexture() 需要 QRhi 实例，
 * 但 updateSampledImage() 回调中没有直接的 QRhi 访问。
 *
 * 解决方案：自定义 QSGTexture 子类，直接包装外部 GL 纹理 ID。
 * 职责：
 * 1. 存储 GL 纹理 ID 和元数据
 * 2. 提供 comparisonKey() 用于 Qt Scene Graph 缓存失效判断
 * 3. 提供 textureSize() 和 hasAlphaChannel() 给 Shader
 *
 * 参考：
 * https://doc.qt.io/qt-6.8/qsgtexture.html（"users can implement custom texture classes"）
 * https://doc.qt.io/qt-6.8/qsgmaterialshader.html#updateSampledImage
 */
class GLExternalTexture : public QSGTexture {
public:
    GLExternalTexture()
        : QSGTexture()
        , m_glTextureId(0)
        , m_size(0, 0)
        , m_hasAlpha(false)
        , m_comparisonKey(0)
    {}

    GLExternalTexture(uint32_t glId, const QSize& size, bool hasAlpha = false)
        : QSGTexture()
        , m_glTextureId(glId)
        , m_size(size)
        , m_hasAlpha(hasAlpha)
        , m_comparisonKey(glId ? static_cast<qint64>(glId) : 0)
    {}

    void setGlTextureId(uint32_t id) {
        m_glTextureId = id;
        m_comparisonKey = id ? static_cast<qint64>(id) : 0;
    }

    qint64 comparisonKey() const override { return m_comparisonKey; }
    QSize textureSize() const override { return m_size; }
    bool hasAlphaChannel() const override { return m_hasAlpha; }
    bool hasMipmaps() const override { return false; }

    void setTextureSize(const QSize& size) { m_size = size; }
    void setHasAlphaChannel(bool alpha) { m_hasAlpha = alpha; }

    uint32_t glTextureId() const { return m_glTextureId; }

private:
    uint32_t m_glTextureId;
    QSize m_size;
    bool m_hasAlpha;
    qint64 m_comparisonKey;
};

/**
 * 视频帧 GLSL 材质（《客户端架构设计》§3.4.1 零拷贝路径）。
 *
 * 支持双像素格式：
 *   YUV420P（isNv12=false）: 3 个 GL_R8 纹理（Y, U, V）
 *   NV12    （isNv12=true） : 2 个纹理（Y: GL_R8, UV: GL_RG8）
 *
 * 三种帧来源：
 *   1. setTextureSet()   — GPU interop 已创建好的 GL 纹理（DMA-BUF 零拷贝路径）
 *   2. uploadYuvFrame()  — CPU 内存平面数据直接上传
 *   3. setGpuTexture()   — 外部单一 GL 纹理（RGBA 格式，bypass YUV 转换）
 */

// ★★★ v4 新增：实例计数器，用于追踪每个 VideoMaterial 实例 ★★★
static int& globalVideoMaterialInstanceCount() {
    static int counter = 0;
    return counter;
}

class VideoMaterial : public QSGMaterial {
public:
    // ★★★ v4 新增：实例唯一标识，用于纹理操作安全追踪 ★★★
    int instanceId() const { return m_instanceId; }

    VideoMaterial();
    ~VideoMaterial() override;

    QSGMaterialType* type() const override;
    QSGMaterialShader* createShader(QSGRendererInterface::RenderMode) const override;
    int compare(const QSGMaterial* other) const override;

    /**
     * ★★★ 长期优化配置：启用批处理优化 ★★★
     *
     * 问题根因（Qt 6.8 qsgbatchrenderer.cpp:6111-6113）：
     * 批处理合并需要三个条件全部满足：
     *   1. type() 相同（g_videoMaterialType 全局共享）✓
     *   2. viewCount() 相同 ✓
     *   3. compare() == 0（未重写 → 默认返回 0，恒等）✗
     * compare() 恒返回 0 导致持有不同纹理的对象被错误合并 → SIGSEGV。
     *
     * compare() 已正确实现（见 VideoMaterial.cpp）：
     * - 比较纹理句柄差异，按 (yTex, uvTex, vTex, isNv12) 排序。
     * - 不同纹理的材质 compare() != 0 → 不被合并 → 不崩溃。
     *
     * kEnableNoBatching 控制开关：
     *   true  （当前）：禁用批处理（短期安全防护，draw call 略增但稳定）
     *   false（未来） ：启用 compare() 驱动的精确批处理（恢复性能优化）
     *
     * 验证流程：运行时将 kEnableNoBatching 改为 false，
     * 若连续运行 30 分钟无 SIGSEGV，则 compare() 实现正确，可永久启用。
     */
    static constexpr bool kEnableNoBatching = true; // ★★★ 验证完成后改为 false 恢复批处理 ★★★

    /**
     * 绑定 GPU interop 提供的纹理集（零拷贝路径）。
     * 纹理生命周期由 IGpuInterop 管理，此处仅记录 ID。
     */
    void setTextureSet(const IGpuInterop::TextureSet& ts);

    /**
     * 直接上传 CPU 内存平面（通用后备路径）。
     * 支持 YUV420P（3 平面）和 NV12（2 平面，isNv12=true）。
     */
    void uploadYuvFrame(const uint8_t* yPlane, int yStride,
                         const uint8_t* uPlane, int uStride,
                         const uint8_t* vPlane, int vStride,
                         int width, int height, bool isNv12 = false);

    /**
     * 向后兼容：旧接口（YUV420P 3 平面 CPU 上传）。
     */
    void uploadFrame(const uint8_t* yPlane, int yStride,
                      const uint8_t* uPlane, int uStride,
                      const uint8_t* vPlane, int vStride,
                      int width, int height)
    {
        uploadYuvFrame(yPlane, yStride, uPlane, uStride, vPlane, vStride,
                       width, height, false);
    }

    /**
     * 外部单 GL 纹理（RGBA，bypass YUV 转换）。
     */
    void setGpuTexture(uint32_t glTextureId, int width, int height);

    struct TextureHandles {
        uint32_t yTex  = 0; // Y plane  或 RGBA single tex
        uint32_t uvTex = 0; // UV plane（NV12）或 U plane（YUV420P）
        uint32_t vTex  = 0; // V plane（YUV420P），0 for NV12
        int width      = 0;
        int height     = 0;
        bool isNv12    = false;
        bool valid     = false;
    };

    static constexpr int kMaxTextures = 3; // Y, UV, V planes

    const TextureHandles& textureHandles() const { return m_handles; }

    /**
     * ★★★ Qt 6.8 RHI 兼容：提供 QSGTexture* 给 updateSampledImage() ★★★
     *
     * 返回绑定点对应的 QSGTexture 实例。
     * 纹理对象使用 std::unique_ptr 管理生命周期，避免手动 delete。
     * ⚠️ 此方法会修改 m_textures（创建/更新 QSGTexture 对象），不能是 const。
     */
    QSGTexture* getSampledTexture(int binding);

    // ★★★ v4 新增：纹理状态诊断接口 ★★★
    QString textureStateDiag() const;
    int64_t lastUploadTimeMs() const { return m_lastUploadTimeMs; }

private:
    void ensureTextures(int width, int height, bool isNv12);
    void destroyOwnedTextures();

    // ★★★ Qt 6.8 RHI 兼容：包装 GL 纹理的 QSGTexture 对象 ★★★
    // 使用 std::unique_ptr 管理生命周期，避免手动 delete
    // binding 0=Y, 1=UV/U, 2=V
    std::array<std::unique_ptr<GLExternalTexture>, kMaxTextures> m_textures;

    // ★★★ v4 新增：实例标识和纹理状态追踪 ★★★
    int m_instanceId = 0;
    int64_t m_lastUploadTimeMs = 0;
    TextureHandles m_handles;
    bool m_gpuNative  = false; // true = 纹理由外部管理，不在析构中释放
    bool m_ownTextures = false; // true = 本类创建了纹理（CPU 上传路径）
};

/**
 * GLSL YUV → RGB 着色器。
 * 通过 uniform 支持 YUV420P 和 NV12 双格式。
 */
class VideoShader : public QSGMaterialShader {
public:
    VideoShader();
    bool updateUniformData(RenderState& state, QSGMaterial* mat,
                            QSGMaterial* old) override;
    void updateSampledImage(RenderState& state, int binding, QSGTexture** texture,
                             QSGMaterial* mat, QSGMaterial* old) override;

    /**
     * 运行时验证着色器是否真正编译成功。
     * 由 VideoMaterial 在首次 uploadYuvFrame / setTextureSet 时调用，
     * 若验证失败（.qsb 缺失或编译错误），输出 ERROR 级别日志。
     */
    void checkCompilationStatus();

private:
    bool m_shaderChecked = false;
};
