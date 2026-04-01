#pragma once
#include "IGpuInterop.h"
#include <QOpenGLContext>
#include <QOpenGLFunctions>

/**
 * CPU → GL 纹理上传（通用后备路径，所有平台均可用）。
 *
 * 支持：
 *   - YUV420P：3 个 GL_R8 纹理（Y / U / V）
 *   - NV12：  2 个纹理（Y: GL_R8, UV: GL_RG8）
 *   - NV21：  2 个纹理（Y: GL_R8, VU: GL_RG8，shader 中 UV 交换）
 *
 * 纹理持久化（仅在尺寸/格式变化时重建），通过 glTexSubImage2D 更新内容。
 * 可选 PBO 异步上传（TODO 性能增强项，当前为直接同步上传）。
 *
 * *** 所有方法必须在 Qt 渲染线程调用 ***
 */
class CpuUploadInterop : public IGpuInterop {
public:
    CpuUploadInterop() = default;
    ~CpuUploadInterop() override { destroyTextures(); }

    TextureSet importFrame(const VideoFrame& frame) override
    {
        if (frame.memoryType != VideoFrame::MemoryType::CPU_MEMORY) return {};
        if (!frame.planes[0].data) return {};

        auto* gl = QOpenGLContext::currentContext();
        if (!gl) return {};
        auto* f = gl->functions();

        const int w = static_cast<int>(frame.width);
        const int h = static_cast<int>(frame.height);
        const bool nv12 = (frame.pixelFormat == VideoFrame::PixelFormat::NV12 ||
                           frame.pixelFormat == VideoFrame::PixelFormat::NV21);

        // 尺寸/格式变化时重建纹理
        if (w != m_width || h != m_height || nv12 != m_isNv12) {
            destroyTextures();
            m_width  = w;
            m_height = h;
            m_isNv12 = nv12;
            createTextures(f);
        }

        if (!m_texSet.valid) return {};

        // 上传 Y 平面
        f->glPixelStorei(GL_UNPACK_ROW_LENGTH,
                         static_cast<GLint>(frame.planes[0].stride));
        f->glBindTexture(GL_TEXTURE_2D, m_texSet.yTexId);
        f->glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h,
                           GL_RED, GL_UNSIGNED_BYTE,
                           static_cast<const uint8_t*>(frame.planes[0].data));

        if (nv12) {
            // 上传 UV 交织平面（GL_RG8）
            if (frame.planes[1].data) {
                f->glPixelStorei(GL_UNPACK_ROW_LENGTH,
                                 static_cast<GLint>(frame.planes[1].stride));
                f->glBindTexture(GL_TEXTURE_2D, m_texSet.uvTexId);
                f->glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w / 2, h / 2,
                                   GL_RG, GL_UNSIGNED_BYTE,
                                   static_cast<const uint8_t*>(frame.planes[1].data));
            }
        } else {
            // YUV420P：独立 U/V 平面（各 w/2 × h/2）
            if (frame.planes[1].data) {
                f->glPixelStorei(GL_UNPACK_ROW_LENGTH,
                                 static_cast<GLint>(frame.planes[1].stride));
                f->glBindTexture(GL_TEXTURE_2D, m_texSet.uvTexId);
                f->glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w / 2, h / 2,
                                   GL_RED, GL_UNSIGNED_BYTE,
                                   static_cast<const uint8_t*>(frame.planes[1].data));
            }
            if (frame.planes[2].data) {
                f->glPixelStorei(GL_UNPACK_ROW_LENGTH,
                                 static_cast<GLint>(frame.planes[2].stride));
                f->glBindTexture(GL_TEXTURE_2D, m_texSet.vTexId);
                f->glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w / 2, h / 2,
                                   GL_RED, GL_UNSIGNED_BYTE,
                                   static_cast<const uint8_t*>(frame.planes[2].data));
            }
        }

        f->glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        f->glBindTexture(GL_TEXTURE_2D, 0);
        return m_texSet;
    }

    void releaseTextures(const TextureSet& /*textures*/) override
    {
        // 持久化纹理不在此释放，析构时统一销毁
    }

    bool    isAvailable() const override { return true; }
    QString name() const override { return "CpuUploadInterop"; }

private:
    void createTextures(QOpenGLFunctions* f)
    {
        auto make = [&](uint32_t& id, int w, int h, GLenum fmt) {
            f->glGenTextures(1, &id);
            f->glBindTexture(GL_TEXTURE_2D, id);
            f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            f->glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(fmt), w, h, 0,
                            (fmt == GL_RG) ? GL_RG : GL_RED,
                            GL_UNSIGNED_BYTE, nullptr);
        };

        make(m_texSet.yTexId, m_width, m_height, GL_R8);
        if (m_isNv12) {
            make(m_texSet.uvTexId, m_width / 2, m_height / 2, GL_RG8);
        } else {
            make(m_texSet.uvTexId, m_width / 2, m_height / 2, GL_R8);
            make(m_texSet.vTexId,  m_width / 2, m_height / 2, GL_R8);
        }

        m_texSet.width  = m_width;
        m_texSet.height = m_height;
        m_texSet.isNv12 = m_isNv12;
        m_texSet.valid  = true;

        f->glBindTexture(GL_TEXTURE_2D, 0);
    }

    void destroyTextures()
    {
        if (!m_texSet.valid) return;
        auto* gl = QOpenGLContext::currentContext();
        if (!gl) return;
        auto* f = gl->functions();
        if (m_texSet.yTexId)  f->glDeleteTextures(1, &m_texSet.yTexId);
        if (m_texSet.uvTexId) f->glDeleteTextures(1, &m_texSet.uvTexId);
        if (m_texSet.vTexId)  f->glDeleteTextures(1, &m_texSet.vTexId);
        m_texSet = {};
    }

    TextureSet m_texSet;
    int  m_width  = 0;
    int  m_height = 0;
    bool m_isNv12 = false;
};
