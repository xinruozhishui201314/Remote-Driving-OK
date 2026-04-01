#include "EGLDmaBufInterop.h"
#include <QDebug>

#ifdef ENABLE_EGL_DMABUF

#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <drm/drm_fourcc.h>

EGLDmaBufInterop::EGLDmaBufInterop()
{
    m_display = eglGetCurrentDisplay();
    if (m_display == EGL_NO_DISPLAY) {
        qWarning() << "[Client][EGLDmaBufInterop] no current EGL display";
        return;
    }
    m_ready = initEGLFunctions();
    if (m_ready) {
        qInfo() << "[Client][EGLDmaBufInterop] initialized on display" << m_display;
    }
}

EGLDmaBufInterop::~EGLDmaBufInterop()
{
    // 释放所有缓存的 EGL Image 和 GL 纹理
    auto* gl = QOpenGLContext::currentContext();
    auto* f  = gl ? gl->functions() : nullptr;

    for (auto& pair : m_cache) {
        if (f) {
            if (pair.yTex)  f->glDeleteTextures(1, &pair.yTex);
            if (pair.uvTex) f->glDeleteTextures(1, &pair.uvTex);
        }
        if (m_eglDestroyImage && m_display != EGL_NO_DISPLAY) {
            if (pair.yImage  != EGL_NO_IMAGE_KHR) m_eglDestroyImage(m_display, pair.yImage);
            if (pair.uvImage != EGL_NO_IMAGE_KHR) m_eglDestroyImage(m_display, pair.uvImage);
        }
    }
    m_cache.clear();
}

bool EGLDmaBufInterop::isAvailable() const
{
    return m_ready;
}

// ─── importFrame ─────────────────────────────────────────────────────────────

IGpuInterop::TextureSet EGLDmaBufInterop::importFrame(const VideoFrame& frame)
{
    if (!m_ready) return {};
    if (frame.memoryType != VideoFrame::MemoryType::DMA_BUF) return {};

    const VideoFrame::DmaBufInfo& dma = frame.dmaBuf;
    if (dma.nbPlanes < 2 || dma.fds[0] < 0) {
        qWarning() << "[Client][EGLDmaBufInterop] invalid DMA-BUF descriptor";
        return {};
    }

    const int primaryFd = dma.fds[0];

    // 检查缓存（同一 fd 直接复用已有 GL 纹理）
    auto it = m_cache.find(primaryFd);
    if (it != m_cache.end()) {
        TextureSet result;
        result.yTexId  = it->yTex;
        result.uvTexId = it->uvTex;
        result.width   = static_cast<int>(frame.width);
        result.height  = static_cast<int>(frame.height);
        result.isNv12  = (frame.pixelFormat == VideoFrame::PixelFormat::NV12 ||
                          frame.pixelFormat == VideoFrame::PixelFormat::NV21);
        result.valid   = (result.yTexId != 0 && result.uvTexId != 0);
        return result;
    }

    // NV12: plane 0 = Y (R8), plane 1 = UV (GR88/RG88)
    const uint64_t modifier = (dma.modifiers[0] != 0) ? dma.modifiers[0]
                                                        : DRM_FORMAT_MOD_NONE;

    // Y 平面：DRM_FORMAT_R8（单通道 8-bit luminance）
    EGLImageKHR yImage = createEGLImage(
        primaryFd,
        frame.width, frame.height,
        DRM_FORMAT_R8,
        dma.offsets[0], dma.pitches[0], modifier);

    if (yImage == EGL_NO_IMAGE_KHR) {
        qWarning() << "[Client][EGLDmaBufInterop] failed to create Y EGL image";
        return {};
    }

    // UV 平面：DRM_FORMAT_GR88（双通道，U=G, V=R 对应 NV12 布局）
    const int uvFd = (dma.fds[1] >= 0) ? dma.fds[1] : primaryFd;
    const uint64_t uvMod = (dma.modifiers[1] != 0) ? dma.modifiers[1] : modifier;

    EGLImageKHR uvImage = createEGLImage(
        uvFd,
        frame.width / 2, frame.height / 2,
        DRM_FORMAT_GR88,
        dma.offsets[1], dma.pitches[1], uvMod);

    if (uvImage == EGL_NO_IMAGE_KHR) {
        m_eglDestroyImage(m_display, yImage);
        qWarning() << "[Client][EGLDmaBufInterop] failed to create UV EGL image";
        return {};
    }

    // EGL Image → GL Texture
    const uint32_t yTex  = createGLTexFromEGLImage(yImage);
    const uint32_t uvTex = createGLTexFromEGLImage(uvImage);

    if (yTex == 0 || uvTex == 0) {
        m_eglDestroyImage(m_display, yImage);
        m_eglDestroyImage(m_display, uvImage);
        qWarning() << "[Client][EGLDmaBufInterop] failed to create GL textures from EGL images";
        return {};
    }

    // 缓存
    EGLImagePair pair;
    pair.yImage  = yImage;
    pair.uvImage = uvImage;
    pair.yTex    = yTex;
    pair.uvTex   = uvTex;
    pair.fd      = primaryFd;
    m_cache.insert(primaryFd, pair);

    TextureSet result;
    result.yTexId  = yTex;
    result.uvTexId = uvTex;
    result.width   = static_cast<int>(frame.width);
    result.height  = static_cast<int>(frame.height);
    result.isNv12  = true;
    result.valid   = true;
    return result;
}

// ─── releaseTextures ─────────────────────────────────────────────────────────

void EGLDmaBufInterop::releaseTextures(const TextureSet& textures)
{
    // 从缓存中查找并移除对应的 EGLImage pair
    for (auto it = m_cache.begin(); it != m_cache.end(); ++it) {
        if (it->yTex == textures.yTexId) {
            auto* gl = QOpenGLContext::currentContext();
            if (gl) {
                auto* f = gl->functions();
                f->glDeleteTextures(1, &it->yTex);
                f->glDeleteTextures(1, &it->uvTex);
            }
            if (m_eglDestroyImage) {
                m_eglDestroyImage(m_display, it->yImage);
                m_eglDestroyImage(m_display, it->uvImage);
            }
            m_cache.erase(it);
            return;
        }
    }
}

// ─── private helpers ─────────────────────────────────────────────────────────

bool EGLDmaBufInterop::initEGLFunctions()
{
    // 检查必需的 EGL 扩展
    const QString extensions = QString(eglQueryString(m_display, EGL_EXTENSIONS));
    if (!extensions.contains("EGL_EXT_image_dma_buf_import")) {
        qInfo() << "[Client][EGLDmaBufInterop] EGL_EXT_image_dma_buf_import not supported";
        return false;
    }

    // 检查 GL 扩展
    auto* ctx = QOpenGLContext::currentContext();
    if (!ctx) return false;
    const QString glExts = QString(reinterpret_cast<const char*>(
        ctx->functions()->glGetString(GL_EXTENSIONS)));
    if (!glExts.contains("GL_OES_EGL_image")) {
        qInfo() << "[Client][EGLDmaBufInterop] GL_OES_EGL_image not supported";
        return false;
    }

    // 获取函数指针
    m_eglCreateImage = reinterpret_cast<PFNEGLCREATEIMAGEKHRPROC>(
        eglGetProcAddress("eglCreateImageKHR"));
    m_eglDestroyImage = reinterpret_cast<PFNEGLDESTROYIMAGEKHRPROC>(
        eglGetProcAddress("eglDestroyImageKHR"));
    m_glEGLImageTargetTex = reinterpret_cast<PFNGLEGLIMAGETARGETTEXTURE2DOESPROC>(
        eglGetProcAddress("glEGLImageTargetTexture2DOES"));

    if (!m_eglCreateImage || !m_eglDestroyImage || !m_glEGLImageTargetTex) {
        qWarning() << "[Client][EGLDmaBufInterop] failed to get EGL/GL extension function pointers";
        return false;
    }

    qInfo() << "[Client][EGLDmaBufInterop] all extensions and functions available";
    return true;
}

EGLImageKHR EGLDmaBufInterop::createEGLImage(int fd, uint32_t width, uint32_t height,
                                               uint32_t drmFourcc,
                                               uint32_t offset, uint32_t pitch,
                                               uint64_t modifier)
{
    // 构建 EGL image 属性列表
    // 使用 EGLAttrib (EGLAttribKHR) 以支持 64-bit modifier
    EGLint attrs[] = {
        EGL_WIDTH,                         static_cast<EGLint>(width),
        EGL_HEIGHT,                        static_cast<EGLint>(height),
        EGL_LINUX_DRM_FOURCC_EXT,          static_cast<EGLint>(drmFourcc),
        EGL_DMA_BUF_PLANE0_FD_EXT,         fd,
        EGL_DMA_BUF_PLANE0_OFFSET_EXT,     static_cast<EGLint>(offset),
        EGL_DMA_BUF_PLANE0_PITCH_EXT,      static_cast<EGLint>(pitch),
        // modifier (hi/lo split)
        EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT, static_cast<EGLint>(modifier & 0xFFFFFFFF),
        EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT, static_cast<EGLint>(modifier >> 32),
        EGL_NONE
    };

    EGLImageKHR image = m_eglCreateImage(
        m_display, EGL_NO_CONTEXT,
        EGL_LINUX_DMA_BUF_EXT,
        nullptr,
        attrs);

    if (image == EGL_NO_IMAGE_KHR) {
        qWarning() << "[Client][EGLDmaBufInterop] eglCreateImageKHR failed"
                   << "fourcc=" << Qt::hex << drmFourcc
                   << "eglError=" << eglGetError();
    }
    return image;
}

uint32_t EGLDmaBufInterop::createGLTexFromEGLImage(EGLImageKHR image)
{
    auto* gl = QOpenGLContext::currentContext();
    if (!gl) return 0;
    auto* f = gl->functions();

    uint32_t tex = 0;
    f->glGenTextures(1, &tex);
    f->glBindTexture(GL_TEXTURE_2D, tex);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // 将 EGL Image 绑定到当前 GL_TEXTURE_2D（零拷贝核心操作）
    m_glEGLImageTargetTex(GL_TEXTURE_2D, image);

    f->glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

#endif // ENABLE_EGL_DMABUF
