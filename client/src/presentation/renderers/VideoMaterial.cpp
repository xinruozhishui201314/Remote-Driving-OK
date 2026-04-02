#include "VideoMaterial.h"
#include <QDebug>
#include <QFile>
#include <QOpenGLFunctions>
#include <QOpenGLContext>
#include <rhi/qshader.h>

static QSGMaterialType g_videoMaterialType;

VideoMaterial::VideoMaterial()
{
    setFlag(Blending, false);
}

VideoMaterial::~VideoMaterial()
{
    if (m_ownTextures) {
        qDebug() << "[Client][VideoMaterial] ~VideoMaterial: 销毁自主纹理";
        destroyOwnedTextures();
    }
}

QSGMaterialType* VideoMaterial::type() const
{
    return &g_videoMaterialType;
}

QSGMaterialShader* VideoMaterial::createShader(QSGRendererInterface::RenderMode) const
{
    return new VideoShader();
}

// ─── setTextureSet ────────────────────────────────────────────────────────────

void VideoMaterial::setTextureSet(const IGpuInterop::TextureSet& ts)
{
    if (!ts.valid) {
        qWarning() << "[Client][VideoMaterial] setTextureSet: 无效的 TextureSet";
        return;
    }

    auto* gl = QOpenGLContext::currentContext();
    if (!gl) {
        qCritical() << "[Client][VideoMaterial] setTextureSet: 无当前 GL 上下文!";
        return;
    }

    // GPU interop 管理纹理，本类不拥有
    if (m_ownTextures) {
        qDebug() << "[Client][VideoMaterial] setTextureSet: 销毁自主纹理后接收 GPU interop";
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

    qDebug() << "[Client][VideoMaterial] setTextureSet: GPU 纹理 y=" << ts.yTexId
             << " uv=" << ts.uvTexId << " v=" << ts.vTexId
             << " size=" << ts.width << "x" << ts.height << " isNv12=" << ts.isNv12;
}

// ─── uploadYuvFrame ───────────────────────────────────────────────────────────

void VideoMaterial::uploadYuvFrame(
    const uint8_t* yPlane, int yStride,
    const uint8_t* uPlane, int uStride,
    const uint8_t* vPlane, int vStride,
    int width, int height, bool isNv12)
{
    // ★★★ 关键诊断：确认 uploadYuvFrame 被调用（CPU-upload 路径的入口） ★★★
    // ★★★ 如果此日志出现但屏幕全黑 → 纹理上传失败（检查 GL 上下文）或着色器编译失败 ★★★
    static int s_logCount = 0;
    ++s_logCount;
    if (s_logCount <= 10 || s_logCount % 30 == 0) {
        qInfo() << "[Client][VideoMaterial] ★★★ uploadYuvFrame 被调用"
                << " seq=" << s_logCount
                << " yPlane=" << (void*)yPlane
                << " w=" << width << " h=" << height
                << " yStride=" << yStride << " (stride==width=无padding，正常)"
                << " isNv12=" << isNv12;
    }

    if (!yPlane) {
        qWarning() << "[Client][VideoMaterial] uploadYuvFrame: yPlane 为空";
        return;
    }

    auto* gl = QOpenGLContext::currentContext();
    if (!gl) {
        // ★ 关键警告：GL 上下文不存在！这将导致视频无法上传到 GPU！
        qCritical() << "[Client][VideoMaterial][CRITICAL] uploadYuvFrame: 无当前 GL 上下文!"
                        " 视频将不显示！（可能原因：QQuickItem 未加入场景图、OpenGL 初始化失败）";
        return;
    }
    if (s_logCount <= 5) {
        qInfo() << "[Client][VideoMaterial] ★ GL 上下文有效 gl=" << (void*)gl;
    }

    auto* f = gl->functions();

    ensureTextures(width, height, isNv12);
    if (!m_handles.valid) {
        // ★★★ 致命：纹理无效！视频将不显示（黑屏）★★★
        // 可能原因：GL 上下文无效、GPU 驱动崩溃
        qCritical() << "[Client][VideoMaterial][FATAL] uploadYuvFrame: ensureTextures 后纹理仍无效！"
                       " 视频将不显示（黑屏）！"
                       " w=" << width << " h=" << height << " isNv12=" << isNv12 <<
                       " glContext=" << (void*)QOpenGLContext::currentContext();
        return;
    }
    // ★★★ 纹理有效 + 上传数据 ★★★
    if (s_logCount <= 10) {
        qInfo() << "[Client][VideoMaterial] ★★★ 纹理有效，开始上传数据 ★★★"
                << " seq=" << s_logCount
                << " yTex=" << m_handles.yTex
                << " uvTex=" << m_handles.uvTex << " vTex=" << m_handles.vTex;
    }

    // 首次调用时验证着色器编译状态（静态变量：整个类只检查一次）
    static bool s_shaderStatusLogged = false;
    if (!s_shaderStatusLogged) {
        s_shaderStatusLogged = true;
        // 检查 .qsb 文件存在性（已在 VideoShader 构造函数中输出；这里补充渲染路径）
        QFile vf(QStringLiteral(":/shaders/video.vert.qsb"));
        QFile ff(QStringLiteral(":/shaders/video.frag.qsb"));
        if (!vf.exists() || !ff.exists()) {
            qCritical() << "[Client][VideoMaterial] ✗ CRITICAL: .qsb files missing! Video will NOT render (black screen).";
            qCritical() << "[Client][VideoMaterial]    Build fix: bash scripts/build-client-dev-full-image.sh";
        } else {
            qInfo() << "[Client][VideoMaterial] ✓ .qsb files present ("
                     << vf.size() << "/" << ff.size() << " bytes), shader pipeline should work.";
        }
    }

    static int s_warnedStrideMismatch = 0;
    if (yStride != width && s_warnedStrideMismatch < 3) {
        qDebug() << "[Client][VideoMaterial] uploadYuvFrame: yStride=" << yStride
                 << " != width=" << width << "（正常，可能有 padding）";
        s_warnedStrideMismatch++;
    }

    // 上传 Y 平面（GL_R8，全分辨率）
    f->glPixelStorei(GL_UNPACK_ROW_LENGTH, yStride);
    f->glBindTexture(GL_TEXTURE_2D, m_handles.yTex);
    f->glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height,
                       GL_RED, GL_UNSIGNED_BYTE, yPlane);
    GLuint yErr = f->glGetError();
    if (yErr != GL_NO_ERROR) {
        qCritical() << "[Client][VideoMaterial] uploadYuvFrame: glTexSubImage2D(Y) 失败"
                    << " err=" << yErr << " yTex=" << m_handles.yTex;
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
                qCritical() << "[Client][VideoMaterial] uploadYuvFrame: glTexSubImage2D(UV/NV12) 失败"
                             << " err=" << uvErr << " uvTex=" << m_handles.uvTex;
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
                qCritical() << "[Client][VideoMaterial] uploadYuvFrame: glTexSubImage2D(U) 失败"
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
                qCritical() << "[Client][VideoMaterial] uploadYuvFrame: glTexSubImage2D(V) 失败"
                             << " err=" << vErr << " vTex=" << m_handles.vTex;
            }
        }
    }

    f->glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    f->glBindTexture(GL_TEXTURE_2D, 0);
    GLuint finalErr = f->glGetError();
    if (finalErr != GL_NO_ERROR) {
        // ★★★ 致命：GL 操作失败！视频将不显示（黑屏）★★★
        qCritical() << "[Client][VideoMaterial][FATAL] uploadYuvFrame: GL 错误"
                    << " err=" << finalErr << " yTex=" << m_handles.yTex
                    << " uvTex=" << m_handles.uvTex << " vTex=" << m_handles.vTex;
    } else {
        // ★★★ 关键诊断：GL 上传成功确认（首10帧 + 每30帧打印）★★★
        // ★★★ 此日志出现 → CPU→GPU 数据传输成功，屏幕是否显示取决于着色器渲染 ★★★
        if (s_logCount <= 10 || s_logCount % 30 == 0) {
            qInfo() << "[Client][VideoMaterial] ★★★ uploadYuvFrame 完成（GL 成功）★★★"
                    << " seq=" << s_logCount
                    << " yTex=" << m_handles.yTex << " uvTex=" << m_handles.uvTex << " vTex=" << m_handles.vTex
                    << " size=" << width << "x" << height << " isNv12=" << isNv12
                    << " ★ 若纹理有效但屏幕全黑 → 检查 VideoShader .qsb 是否编译成功";
        }
    }
}

// ─── setGpuTexture ────────────────────────────────────────────────────────────

void VideoMaterial::setGpuTexture(uint32_t glTextureId, int width, int height)
{
    auto* gl = QOpenGLContext::currentContext();
    if (!gl) {
        qCritical() << "[Client][VideoMaterial] setGpuTexture: 无当前 GL 上下文!";
        return;
    }

    if (m_ownTextures) {
        qDebug() << "[Client][VideoMaterial] setGpuTexture: 销毁自主纹理";
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
    qDebug() << "[Client][VideoMaterial] setGpuTexture: id=" << glTextureId
             << " size=" << width << "x" << height;
}

// ─── private helpers ─────────────────────────────────────────────────────────

void VideoMaterial::ensureTextures(int width, int height, bool isNv12)
{
    static int s_ensureLogCount = 0;
    const int seq = ++s_ensureLogCount;

    // ── 1. 尺寸有效性检查 ──────────────────────────────────────────────────
    if (width <= 0 || height <= 0) {
        qWarning() << "[Client][VideoMaterial][ensureTextures] ★ 无效尺寸，跳过"
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
                        " seq=" << seq;
        return;
    }

    auto* f = gl->functions();
    if (!f) {
        qCritical() << "[Client][VideoMaterial][ensureTextures] ★ CRITICAL: GL functions 不可用!"
                        " seq=" << seq;
        return;
    }

    // ── 5. 创建纹理 ───────────────────────────────────────────────────────
    int textureCreateSuccess = 0;
    int textureCreateFail = 0;

    auto makeTexture = [&](uint32_t& id, int w, int h, GLenum internalFmt, GLenum fmt, const char* name) -> bool {
        try {
            f->glGenTextures(1, &id);
            GLuint genErr = f->glGetError();
            if (genErr != GL_NO_ERROR) {
                qCritical() << "[Client][VideoMaterial][ensureTextures] glGenTextures 失败"
                           << " name=" << name << " id=" << id << " err=" << genErr;
                id = 0;
                return false;
            }
            if (id == 0) {
                qCritical() << "[Client][VideoMaterial][ensureTextures] glGenTextures 返回 0"
                           << " name=" << name;
                return false;
            }

            f->glBindTexture(GL_TEXTURE_2D, id);
            GLuint bindErr = f->glGetError();
            if (bindErr != GL_NO_ERROR) {
                qCritical() << "[Client][VideoMaterial][ensureTextures] glBindTexture 失败"
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
                           << " name=" << name << " id=" << id << " w=" << w << " h=" << h
                           << " internalFmt=" << internalFmt << " fmt=" << fmt << " err=" << texErr;
                f->glDeleteTextures(1, &id);
                id = 0;
                return false;
            }

            qDebug() << "[Client][VideoMaterial][ensureTextures] 创建纹理成功"
                     << " name=" << name << " id=" << id << " size=" << w << "x" << h
                     << " internalFmt=0x" << QString::number(internalFmt, 16)
                     << " fmt=0x" << QString::number(fmt, 16);
            return true;
        } catch (const std::exception& e) {
            qCritical() << "[Client][VideoMaterial][ensureTextures] 创建纹理异常"
                       << " name=" << name << " error=" << e.what();
            if (id != 0) {
                try { f->glDeleteTextures(1, &id); } catch (...) {}
                id = 0;
            }
            return false;
        } catch (...) {
            qCritical() << "[Client][VideoMaterial][ensureTextures] 创建纹理未知异常"
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
                     << " seq=" << seq
                     << " yTex=" << m_handles.yTex << " uvTex=" << m_handles.uvTex << " vTex=" << m_handles.vTex
                     << " finalGLerr=" << finalErr;
        }
    } else {
        qCritical() << "[Client][VideoMaterial][ensureTextures] ★ CRITICAL: 纹理创建失败"
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

VideoShader::VideoShader()
{
    // 尝试加载预编译的 .qsb（由 CMake + qsb 工具生成，见 CMakeLists.txt）
    // 路径：qrc:/shaders/video.vert.qsb（通过 CMake 生成的 shaders.qrc 映射）
    const QString vertPath = QStringLiteral(":/shaders/video.vert.qsb");
    const QString fragPath = QStringLiteral(":/shaders/video.frag.qsb");

    QFile vertFile(vertPath);
    QFile fragFile(fragPath);
    const bool vertExists = vertFile.exists();
    const bool fragExists = fragFile.exists();
    const bool bothExist = vertExists && fragExists;

    // 每次构造都记录（不同于旧逻辑的单次警告）；ERROR 级别确保日志可被自动检测
    if (bothExist) {
        qInfo() << "[VideoShader] ✓ .qsb shader files found:"
                 << " vert=" << vertPath << " (" << vertFile.size() << " bytes)"
                 << " frag=" << fragPath << " (" << fragFile.size() << " bytes)";
    } else {
        qCritical() << "[VideoShader] ✗ CRITICAL: .qsb shader files NOT FOUND — video rendering DISABLED (black screen)!";
        qCritical() << "[VideoShader]    vertFile.exists=" << vertExists
                     << " fragFile.exists=" << fragExists;
        qCritical() << "[VideoShader]    BUILD FIX: ensure qsb is installed and in PATH, then:";
        qCritical() << "[VideoShader]    1. Re-run: bash scripts/build-client-dev-full-image.sh";
        qCritical() << "[VideoShader]    2. Or in container: cmake --build . --target shader_compilation";
        qCritical() << "[VideoShader]    3. Expected paths: qrc:/shaders/video.vert.qsb, qrc:/shaders/video.frag.qsb";
    }

    setShaderFileName(VertexStage, vertPath);
    setShaderFileName(FragmentStage, fragPath);
    qDebug() << "[VideoShader] constructed:"
             << " vert=" << vertPath << " frag=" << fragPath;
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

void VideoShader::updateSampledImage(RenderState& /*state*/, int binding,
                                      QSGTexture** /*texture*/,
                                      QSGMaterial* mat, QSGMaterial* /*old*/)
{
    // 纹理绑定通过直接 GL 调用完成（绕过 Qt 纹理封装以复用既有 GL ID）
    auto* vm = static_cast<VideoMaterial*>(mat);
    const auto& h = vm->textureHandles();
    if (!h.valid) return;

    auto* gl = QOpenGLContext::currentContext();
    if (!gl) return;
    auto* f = gl->functions();

    switch (binding) {
    case 1: // Y
        f->glActiveTexture(GL_TEXTURE1);
        f->glBindTexture(GL_TEXTURE_2D, h.yTex);
        break;
    case 2: // UV (NV12) 或 U (YUV420P)
        f->glActiveTexture(GL_TEXTURE2);
        f->glBindTexture(GL_TEXTURE_2D, h.uvTex ? h.uvTex : h.yTex);
        break;
    case 3: // V (YUV420P only)
        f->glActiveTexture(GL_TEXTURE3);
        f->glBindTexture(GL_TEXTURE_2D, h.vTex ? h.vTex : h.yTex);
        break;
    default:
        break;
    }
    f->glActiveTexture(GL_TEXTURE0);
}
