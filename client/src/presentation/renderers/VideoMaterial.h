#pragma once
#include <QSGMaterial>
#include <QSGMaterialShader>
#include <QSGTexture>
#include <memory>
#include "../../infrastructure/media/gpu/IGpuInterop.h"

/**
 * 视频帧 GLSL 材质（《客户端架构设计》§3.4.1 完整零拷贝路径）。
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
class VideoMaterial : public QSGMaterial {
public:
    VideoMaterial();
    ~VideoMaterial() override;

    QSGMaterialType* type() const override;
    QSGMaterialShader* createShader(QSGRendererInterface::RenderMode) const override;

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

    const TextureHandles& textureHandles() const { return m_handles; }

private:
    void ensureTextures(int width, int height, bool isNv12);
    void destroyOwnedTextures();

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
