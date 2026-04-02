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
        static int s_importCount = 0;
        static int s_successCount = 0;
        static int s_failNoDataCount = 0;
        static int s_failNoGLCount = 0;
        static int s_failNoTexCount = 0;
        static int s_failCreateTexCount = 0;
        static int s_failGLCount = 0;
        const int seq = ++s_importCount;

        // ── 1. 输入检查 ──────────────────────────────────────────────
        if (frame.memoryType != VideoFrame::MemoryType::CPU_MEMORY) {
            if (seq <= 3) {
                qDebug() << "[CpuUploadInterop] 非 CPU_MEMORY，跳过 frame.memType="
                         << static_cast<int>(frame.memoryType);
            }
            return {};
        }
        if (!frame.planes[0].data) {
            const int cnt = ++s_failNoDataCount;
            if (cnt <= 3) {
                qWarning() << "[CpuUploadInterop][importFrame] ★ planes[0].data 为空，跳过"
                           << " seq=" << seq << " fail#=" << cnt
                           << " memType=" << static_cast<int>(frame.memoryType);
            }
            return {};
        }

        // ── 2. GL 上下文检查 ─────────────────────────────────────────
        auto* gl = QOpenGLContext::currentContext();
        if (!gl) {
            const int cnt = ++s_failNoGLCount;
            if (cnt <= 3) {
                qCritical() << "[CpuUploadInterop][importFrame] ★ CRITICAL: 无当前 GL 上下文!"
                            << " seq=" << seq << " fail#=" << cnt;
            }
            return {};
        }

        auto* f = gl->functions();
        if (!f) {
            const int cnt = ++s_failNoGLCount;
            if (cnt <= 3) {
                qCritical() << "[CpuUploadInterop][importFrame] ★ CRITICAL: GL functions 不可用!"
                            << " seq=" << seq << " fail#=" << cnt;
            }
            return {};
        }

        const int w = static_cast<int>(frame.width);
        const int h = static_cast<int>(frame.height);
        const bool nv12 = (frame.pixelFormat == VideoFrame::PixelFormat::NV12 ||
                           frame.pixelFormat == VideoFrame::PixelFormat::NV21);

        // ── 3. 尺寸/格式变化时重建纹理 ─────────────────────────────────
        if (w != m_width || h != m_height || nv12 != m_isNv12) {
            if (seq <= 5 || m_width == 0) {
                qInfo() << "[CpuUploadInterop] 纹理重建"
                         << " seq=" << seq
                         << " old=" << m_width << "x" << m_height << " nv12=" << m_isNv12
                         << " new=" << w << "x" << h << " nv12=" << nv12;
            }
            destroyTextures();
            m_width  = w;
            m_height = h;
            m_isNv12 = nv12;
            createTextures(f, seq);
        }

        // ── 4. 纹理有效性检查 ────────────────────────────────────────
        if (!m_texSet.valid) {
            const int cnt = ++s_failNoTexCount;
            if (cnt <= 3) {
                qCritical() << "[CpuUploadInterop][importFrame] ★ CRITICAL: 纹理无效，无法上传"
                            << " seq=" << seq << " fail#=" << cnt
                            << " yTex=" << m_texSet.yTexId;
            }
            return {};
        }

        // ── 5. 上传纹理数据 ────────────────────────────────────────────
        try {
            // 上传 Y 平面
            f->glPixelStorei(GL_UNPACK_ROW_LENGTH,
                             static_cast<GLint>(frame.planes[0].stride));
            f->glBindTexture(GL_TEXTURE_2D, m_texSet.yTexId);
            f->glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h,
                               GL_RED, GL_UNSIGNED_BYTE,
                               static_cast<const uint8_t*>(frame.planes[0].data));
            GLuint yErr = f->glGetError();
            if (yErr != GL_NO_ERROR) {
                qCritical() << "[CpuUploadInterop][importFrame] glTexSubImage2D(Y) 失败"
                             << " seq=" << seq << " err=" << yErr << " yTex=" << m_texSet.yTexId;
            }

            if (nv12) {
                // 上传 UV 交织平面（GL_RG8）
                if (frame.planes[1].data) {
                    f->glPixelStorei(GL_UNPACK_ROW_LENGTH,
                                     static_cast<GLint>(frame.planes[1].stride));
                    f->glBindTexture(GL_TEXTURE_2D, m_texSet.uvTexId);
                    f->glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w / 2, h / 2,
                                       GL_RG, GL_UNSIGNED_BYTE,
                                       static_cast<const uint8_t*>(frame.planes[1].data));
                    GLuint uvErr = f->glGetError();
                    if (uvErr != GL_NO_ERROR) {
                        qWarning() << "[CpuUploadInterop][importFrame] glTexSubImage2D(UV) 失败"
                                    << " seq=" << seq << " err=" << uvErr;
                    }
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

            ++s_successCount;
            if (seq <= 5) {
                qInfo() << "[CpuUploadInterop][importFrame] ★ 上传成功"
                         << " seq=" << seq << " success=" << s_successCount
                         << " yTex=" << m_texSet.yTexId
                         << " uvTex=" << m_texSet.uvTexId << " vTex=" << m_texSet.vTexId;
            }
            return m_texSet;

        } catch (const std::exception& e) {
            qCritical() << "[CpuUploadInterop][importFrame] ★ 上传异常"
                         << " seq=" << seq << " error=" << e.what();
            return {};
        } catch (...) {
            qCritical() << "[CpuUploadInterop][importFrame] ★ 上传未知异常"
                         << " seq=" << seq;
            return {};
        }
    }

    void releaseTextures(const TextureSet& /*textures*/) override
    {
        // 持久化纹理不在此释放，析构时统一销毁
    }

    bool    isAvailable() const override { return true; }
    QString name() const override { return "CpuUploadInterop"; }

private:
    void createTextures(QOpenGLFunctions* f, int seq)
    {
        static int s_createFailCount = 0;

        if (!f) {
            qCritical() << "[CpuUploadInterop][createTextures] GL functions 不可用";
            return;
        }

        auto make = [&](uint32_t& id, int w, int h, GLenum fmt, const char* name) -> bool {
            try {
                f->glGenTextures(1, &id);
                GLuint genErr = f->glGetError();
                if (genErr != GL_NO_ERROR || id == 0) {
                    qCritical() << "[CpuUploadInterop][createTextures] glGenTextures 失败"
                                 << " name=" << name << " id=" << id << " err=" << genErr;
                    id = 0;
                    return false;
                }

                f->glBindTexture(GL_TEXTURE_2D, id);
                f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                f->glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(fmt), w, h, 0,
                                (fmt == GL_RG) ? GL_RG : GL_RED,
                                GL_UNSIGNED_BYTE, nullptr);
                GLuint texErr = f->glGetError();
                if (texErr != GL_NO_ERROR) {
                    qCritical() << "[CpuUploadInterop][createTextures] glTexImage2D 失败"
                                 << " name=" << name << " w=" << w << " h=" << h << " err=" << texErr;
                    f->glDeleteTextures(1, &id);
                    id = 0;
                    return false;
                }

                qDebug() << "[CpuUploadInterop][createTextures] 创建纹理"
                          << " name=" << name << " id=" << id << " size=" << w << "x" << h;
                return true;
            } catch (const std::exception& e) {
                qCritical() << "[CpuUploadInterop][createTextures] 创建纹理异常"
                             << " name=" << name << " error=" << e.what();
                if (id != 0) {
                    try { f->glDeleteTextures(1, &id); } catch (...) {}
                    id = 0;
                }
                return false;
            }
        };

        bool yOk = make(m_texSet.yTexId, m_width, m_height, GL_R8, "Y");
        bool uOk, vOk;
        if (m_isNv12) {
            uOk = make(m_texSet.uvTexId, m_width / 2, m_height / 2, GL_RG8, "UV");
            m_texSet.vTexId = 0;
            vOk = true;
        } else {
            uOk = make(m_texSet.uvTexId, m_width / 2, m_height / 2, GL_R8, "U");
            vOk = make(m_texSet.vTexId, m_width / 2, m_height / 2, GL_R8, "V");
        }

        if (yOk && uOk && vOk) {
            m_texSet.width  = m_width;
            m_texSet.height = m_height;
            m_texSet.isNv12 = m_isNv12;
            m_texSet.valid  = true;
            f->glBindTexture(GL_TEXTURE_2D, 0);
            if (seq <= 5) {
                qInfo() << "[CpuUploadInterop][createTextures] ★ 纹理创建成功"
                         << " seq=" << seq
                         << " yTex=" << m_texSet.yTexId
                         << " uvTex=" << m_texSet.uvTexId
                         << " vTex=" << m_texSet.vTexId;
            }
        } else {
            qCritical() << "[CpuUploadInterop][createTextures] ★ CRITICAL: 纹理创建失败"
                         << " seq=" << seq
                         << " yOk=" << yOk << " uOk=" << uOk << " vOk=" << vOk
                         << " 视频将不显示！";
            m_texSet.valid = false;
        }
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
