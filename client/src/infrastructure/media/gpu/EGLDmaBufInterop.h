#pragma once
#include "IGpuInterop.h"
#include <QHash>

/**
 * EGL DMA-BUF → GL 纹理零拷贝实现（Linux，Intel/AMD Mesa）。
 *
 * 工作原理（无 CPU 参与）：
 *   1) VAAPI 解码器将帧导出为 DRM Prime fd（AVDRMFrameDescriptor）
 *   2) 本类在渲染线程调用 eglCreateImageKHR(EGL_LINUX_DMA_BUF_EXT)
 *      为 Y 和 UV 平面各创建一个 EGLImage
 *   3) glEGLImageTargetTexture2DOES 将 EGLImage 绑定到 GL_TEXTURE_2D
 *   4) shader 直接从纹理采样，帧数据全程在 GPU 内存中
 *
 * 要求：
 *   - EGL 扩展：EGL_EXT_image_dma_buf_import
 *   - GL 扩展：GL_OES_EGL_image
 *   - Mesa 20+（Intel i965/iris/radeonsi 均支持）
 *
 * *** 必须在 Qt 渲染线程调用 ***
 */

#ifdef ENABLE_EGL_DMABUF

#include <EGL/egl.h>
#include <EGL/eglext.h>

class EGLDmaBufInterop : public IGpuInterop {
public:
    EGLDmaBufInterop();
    ~EGLDmaBufInterop() override;

    TextureSet importFrame(const VideoFrame& frame) override;
    void       releaseTextures(const TextureSet& textures) override;
    bool       isAvailable() const override;
    QString    name() const override { return "EGLDmaBufInterop"; }

private:
    struct EGLImagePair {
        EGLImageKHR yImage  = EGL_NO_IMAGE_KHR;
        EGLImageKHR uvImage = EGL_NO_IMAGE_KHR;
        uint32_t    yTex    = 0;
        uint32_t    uvTex   = 0;
        int         fd      = -1;   // 对应的 DMA-BUF fd（用于缓存 key）
    };

    bool initEGLFunctions();
    EGLImageKHR createEGLImage(int fd, uint32_t width, uint32_t height,
                                uint32_t drmFourcc, uint32_t offset,
                                uint32_t pitch, uint64_t modifier);
    uint32_t    createGLTexFromEGLImage(EGLImageKHR image);

    EGLDisplay  m_display  = EGL_NO_DISPLAY;
    bool        m_ready    = false;

    // EGL 函数指针
    PFNEGLCREATEIMAGEKHRPROC         m_eglCreateImage    = nullptr;
    PFNEGLDESTROYIMAGEKHRPROC        m_eglDestroyImage   = nullptr;
    PFNGLEGLIMAGETARGETTEXTURE2DOESPROC m_glEGLImageTargetTex = nullptr;

    // 缓存：per-DMA-BUF fd 的 EGLImage + GL texture
    // key = DMA-BUF fd（同一 fd 不重建）
    QHash<int, EGLImagePair> m_cache;
};

#else // !ENABLE_EGL_DMABUF

/**
 * 不支持 EGL DMA-BUF 时的存根（总是 isAvailable()=false）。
 */
class EGLDmaBufInterop : public IGpuInterop {
public:
    TextureSet importFrame(const VideoFrame&) override { return {}; }
    void releaseTextures(const TextureSet&) override {}
    bool isAvailable() const override { return false; }
    QString name() const override { return "EGLDmaBufInterop(unavailable)"; }
};

#endif // ENABLE_EGL_DMABUF
