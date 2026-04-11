#include "Nv12DmaBufQsg.h"

#if defined(CLIENT_HAVE_NV12_DMABUF_SG) && defined(ENABLE_EGL_DMABUF)

#include <QFile>
#include <QOpenGLContext>
#include <QQuickWindow>
#include <QSGGeometry>
#include <QSGGeometryNode>
#include <QSGMaterial>
#include <QSGMaterialShader>
#include <QSGTexture>
#include <QtGui/qopengl.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <drm/drm_fourcc.h>

#include "media/VideoFrameEvidenceDiag.h"

#include <cstring>

#ifndef EGL_LINUX_DMA_BUF_EXT
#define EGL_LINUX_DMA_BUF_EXT 0x3270
#endif

namespace {

static std::atomic<int> s_capLogged{0};

bool qsbResourceExists() {
  static const char* const kPaths[] = {
      ":/client_dmabuf/shaders/nv12_dmabuf.vert.qsb",
      ":/client_dmabuf/shaders/nv12_dmabuf.frag.qsb",
  };
  for (const char* p : kPaths) {
    if (!QFile::exists(QLatin1String(p)))
      return false;
  }
  return true;
}

PFNEGLCREATEIMAGEKHRPROC pfn_eglCreateImageKHR = nullptr;
PFNEGLDESTROYIMAGEKHRPROC pfn_eglDestroyImageKHR = nullptr;
typedef void(GL_APIENTRYP PFN_glEGLImageTargetTexture2DOES)(GLenum target, void* image);
static PFN_glEGLImageTargetTexture2DOES pfn_glEGLImageTargetTexture2DOES = nullptr;

static bool loadEglGlProcs() {
  static bool tried = false;
  static bool ok = false;
  if (tried)
    return ok;
  tried = true;
  pfn_eglCreateImageKHR =
      reinterpret_cast<PFNEGLCREATEIMAGEKHRPROC>(eglGetProcAddress("eglCreateImageKHR"));
  pfn_eglDestroyImageKHR =
      reinterpret_cast<PFNEGLDESTROYIMAGEKHRPROC>(eglGetProcAddress("eglDestroyImageKHR"));
  QOpenGLContext* ctx = QOpenGLContext::currentContext();
  if (ctx) {
    pfn_glEGLImageTargetTexture2DOES = reinterpret_cast<PFN_glEGLImageTargetTexture2DOES>(
        ctx->getProcAddress("glEGLImageTargetTexture2DOES"));
  }
  ok = pfn_eglCreateImageKHR && pfn_eglDestroyImageKHR && pfn_glEGLImageTargetTexture2DOES;
  return ok;
}

struct EglDmaPlane {
  EGLImageKHR image = EGL_NO_IMAGE_KHR;
  GLuint tex = 0;
};

static void destroyPlane(EGLDisplay dpy, EglDmaPlane& p) {
  if (p.tex != 0) {
    QOpenGLContext* ctx = QOpenGLContext::currentContext();
    if (ctx)
      ctx->functions()->glDeleteTextures(1, &p.tex);
    p.tex = 0;
  }
  if (p.image != EGL_NO_IMAGE_KHR && pfn_eglDestroyImageKHR && dpy != EGL_NO_DISPLAY) {
    pfn_eglDestroyImageKHR(dpy, p.image);
    p.image = EGL_NO_IMAGE_KHR;
  }
}

static EGLImageKHR createPlaneImage(EGLDisplay dpy, int w, int h, uint32_t drmFourcc, int fd,
                                    uint32_t offset, uint32_t pitch, uint64_t modifier) {
  if (!pfn_eglCreateImageKHR || fd < 0 || w <= 0 || h <= 0)
    return EGL_NO_IMAGE_KHR;

  EGLint attrs[32];
  int i = 0;
  attrs[i++] = EGL_WIDTH;
  attrs[i++] = w;
  attrs[i++] = EGL_HEIGHT;
  attrs[i++] = h;
  attrs[i++] = EGL_LINUX_DRM_FOURCC_EXT;
  attrs[i++] = static_cast<EGLint>(drmFourcc);
  attrs[i++] = EGL_DMA_BUF_PLANE0_FD_EXT;
  attrs[i++] = fd;
  attrs[i++] = EGL_DMA_BUF_PLANE0_OFFSET_EXT;
  attrs[i++] = static_cast<EGLint>(offset);
  attrs[i++] = EGL_DMA_BUF_PLANE0_STRIDE_EXT;
  attrs[i++] = static_cast<EGLint>(pitch);
  if (modifier != 0) {
    attrs[i++] = EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT;
    attrs[i++] = static_cast<EGLint>(modifier & 0xffffffffu);
    attrs[i++] = EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT;
    attrs[i++] = static_cast<EGLint>((modifier >> 32) & 0xffffffffu);
  }
  attrs[i++] = EGL_NONE;

  return pfn_eglCreateImageKHR(dpy, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, nullptr, attrs);
}

static bool planeToTexture(EGLDisplay dpy, const EglDmaPlane& plane, QString* err) {
  if (plane.image == EGL_NO_IMAGE_KHR || plane.tex == 0) {
    if (err)
      *err = QStringLiteral("bad_plane");
    return false;
  }
  QOpenGLContext* ctx = QOpenGLContext::currentContext();
  if (!ctx || !pfn_glEGLImageTargetTexture2DOES) {
    if (err)
      *err = QStringLiteral("no_gl_ctx");
    return false;
  }
  auto& gl = *ctx->functions();
  gl.glBindTexture(GL_TEXTURE_2D, plane.tex);
  gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  pfn_glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, reinterpret_cast<void*>(plane.image));
  gl.glBindTexture(GL_TEXTURE_2D, 0);
  if (gl.glGetError() != GL_NO_ERROR) {
    if (err)
      *err = QStringLiteral("glEGLImageTargetTexture2DOES_gl_err");
    return false;
  }
  return true;
}

class Nv12Material;

class Nv12Shader : public QSGMaterialShader {
 public:
  Nv12Shader() {
    setShaderFileName(VertexStage, QLatin1String(":/client_dmabuf/shaders/nv12_dmabuf.vert.qsb"));
    setShaderFileName(FragmentStage, QLatin1String(":/client_dmabuf/shaders/nv12_dmabuf.frag.qsb"));
  }

  bool updateUniformData(RenderState& state, QSGMaterial*, QSGMaterial* oldMaterial) override {
    QByteArray* buf = state.uniformData();
    bool changed = false;
    if (state.isMatrixDirty() || !oldMaterial) {
      const QMatrix4x4 m = state.combinedMatrix();
      memcpy(buf->data(), m.constData(), 64);
      changed = true;
    }
    if (state.isOpacityDirty() || !oldMaterial) {
      float* f = reinterpret_cast<float*>(buf->data() + 64);
      f[0] = state.opacity();
      f[1] = f[2] = f[3] = 0.f;
      changed = true;
    }
    return changed;
  }

  void updateSampledImage(RenderState&, int binding, QSGTexture** texture, QSGMaterial* newMaterial,
                          QSGMaterial*) override {
    auto* mat = static_cast<Nv12Material*>(newMaterial);
    if (binding == 1)
      *texture = mat->texY();
    else if (binding == 2)
      *texture = mat->texUV();
  }
};

class Nv12Material : public QSGMaterial {
 public:
  Nv12Material() : m_texY(nullptr), m_texUV(nullptr) {
    setFlag(Blending | RequiresFullMatrixExceptTranslate);
  }

  ~Nv12Material() override { teardownInterop(); }

  /** 释放 QSGTexture + EGLImage（下一帧导入前或析构时调用；须在正确 GL 上下文） */
  void teardownInterop() {
    delete m_texY;
    delete m_texUV;
    m_texY = m_texUV = nullptr;
    if (m_dpy != EGL_NO_DISPLAY) {
      destroyPlane(m_dpy, m_planeY);
      destroyPlane(m_dpy, m_planeUv);
      m_dpy = EGL_NO_DISPLAY;
    }
    m_planeY = {};
    m_planeUv = {};
  }

  QSGMaterialType* type() const override {
    static QSGMaterialType t;
    return &t;
  }

  QSGMaterialShader* createShader(QSGRendererInterface::RenderMode) const override {
    return new Nv12Shader();
  }

  int compare(const QSGMaterial* o) const override {
    const auto* other = static_cast<const Nv12Material*>(o);
    if (m_texY == other->m_texY && m_texUV == other->m_texUV)
      return 0;
    return (quintptr(m_texY) < quintptr(other->m_texY)) ? -1 : 1;
  }

  void setTextures(QSGTexture* y, QSGTexture* uv) {
    delete m_texY;
    delete m_texUV;
    m_texY = y;
    m_texUV = uv;
  }

  void setEglState(EGLDisplay dpy, EglDmaPlane py, EglDmaPlane puv) {
    m_dpy = dpy;
    m_planeY = py;
    m_planeUv = puv;
  }

  QSGTexture* texY() const { return m_texY; }
  QSGTexture* texUV() const { return m_texUV; }

 private:
  QSGTexture* m_texY;
  QSGTexture* m_texUV;
  EGLDisplay m_dpy = EGL_NO_DISPLAY;
  EglDmaPlane m_planeY{};
  EglDmaPlane m_planeUv{};
};

/**
 * 导入成功后再 teardown 旧资源，避免「新帧导入失败却先拆掉上一帧」导致黑屏/闪烁。
 */
static bool importNv12ToTextures(QQuickWindow* win, const VideoFrame& vf, Nv12Material* mat,
                                 QString* err) {
  if (vf.memoryType != VideoFrame::MemoryType::DMA_BUF || vf.dmaBuf.nbPlanes < 2) {
    if (err)
      *err = QStringLiteral("not_dma_or_planes");
    return false;
  }

  EGLDisplay dpy = eglGetCurrentDisplay();
  if (dpy == EGL_NO_DISPLAY) {
    if (err)
      *err = QStringLiteral("egl_no_current_display");
    return false;
  }

  if (!loadEglGlProcs()) {
    if (err)
      *err = QStringLiteral("egl_gl_proc_missing");
    return false;
  }

  const int yfd = vf.dmaBuf.fds[0];
  const int uvfd = vf.dmaBuf.fds[1] >= 0 ? vf.dmaBuf.fds[1] : yfd;
  if (yfd < 0) {
    if (err)
      *err = QStringLiteral("bad_fd");
    return false;
  }

  const int W = static_cast<int>(vf.width);
  const int H = static_cast<int>(vf.height);
  if (W <= 0 || H <= 0) {
    if (err)
      *err = QStringLiteral("bad_wh");
    return false;
  }

  const uint32_t offY = vf.dmaBuf.offsets[0];
  const uint32_t offUv = vf.dmaBuf.offsets[1];
  const uint32_t pitchY = vf.dmaBuf.pitches[0];
  const uint32_t pitchUv = vf.dmaBuf.pitches[1];
  const uint64_t modY = vf.dmaBuf.modifiers[0];
  const uint64_t modUv = vf.dmaBuf.nbPlanes > 1 ? vf.dmaBuf.modifiers[1] : modY;

  EglDmaPlane planeY{};
  EglDmaPlane planeUv{};

  QOpenGLContext* ctx = QOpenGLContext::currentContext();
  if (!ctx) {
    if (err)
      *err = QStringLiteral("no_qt_gl_ctx");
    return false;
  }
  ctx->functions()->glGenTextures(1, &planeY.tex);
  ctx->functions()->glGenTextures(1, &planeUv.tex);

  planeY.image =
      createPlaneImage(dpy, W, H, DRM_FORMAT_R8, yfd, offY, pitchY, modY);
  if (planeY.image == EGL_NO_IMAGE_KHR) {
    destroyPlane(dpy, planeY);
    destroyPlane(dpy, planeUv);
    if (err)
      *err = QStringLiteral("eglCreateImage_Y_failed");
    return false;
  }

  const int uvW = qMax(1, W / 2);
  const int uvH = qMax(1, H / 2);
  planeUv.image = createPlaneImage(dpy, uvW, uvH, DRM_FORMAT_GR88, uvfd, offUv, pitchUv, modUv);
  if (planeUv.image == EGL_NO_IMAGE_KHR) {
    destroyPlane(dpy, planeY);
    destroyPlane(dpy, planeUv);
    if (err)
      *err = QStringLiteral("eglCreateImage_UV_failed");
    return false;
  }

  QString e2;
  if (!planeToTexture(dpy, planeY, &e2)) {
    destroyPlane(dpy, planeY);
    destroyPlane(dpy, planeUv);
    if (err)
      *err = e2;
    return false;
  }
  if (!planeToTexture(dpy, planeUv, &e2)) {
    destroyPlane(dpy, planeY);
    destroyPlane(dpy, planeUv);
    if (err)
      *err = e2;
    return false;
  }

  quintptr ny = planeY.tex;
  quintptr nuv = planeUv.tex;
  QSGTexture* texY =
      win->createTextureFromNativeObject(QQuickWindow::NativeObjectOpenGLTexture, &ny, 0,
                                         QSize(W, H), QQuickWindow::TextureIsOpaque);
  QSGTexture* texUV =
      win->createTextureFromNativeObject(QQuickWindow::NativeObjectOpenGLTexture, &nuv, 0,
                                         QSize(uvW, uvH), QQuickWindow::TextureIsOpaque);
  if (!texY || !texUV) {
    delete texY;
    delete texUV;
    destroyPlane(dpy, planeY);
    destroyPlane(dpy, planeUv);
    if (err)
      *err = QStringLiteral("createTextureFromNativeObject_null");
    return false;
  }

  mat->teardownInterop();
  mat->setTextures(texY, texUV);
  planeY.tex = 0;
  planeUv.tex = 0;
  mat->setEglState(dpy, planeY, planeUv);
  return true;
}

static void setTexturedQuadGeometry(QSGGeometry* geo, const QRectF& r) {
  if (!geo)
    return;
  QSGGeometry::TexturedPoint2D* v = geo->vertexDataAsTexturedPoint2D();
  v[0].set(r.left(), r.top(), 0.f, 0.f);
  v[1].set(r.left(), r.bottom(), 0.f, 1.f);
  v[2].set(r.right(), r.top(), 1.f, 0.f);
  v[3].set(r.right(), r.bottom(), 1.f, 1.f);
}

/** 配置/上下文类错误：仍尽量保留上一帧画面，由上层累计失败后回退 CPU */
static QSGGeometryNode* retainLastGoodNv12Node(QSGNode* oldNode, bool* outOk, QString* outFailReason,
                                               const QString& reason) {
  if (outOk)
    *outOk = false;
  if (outFailReason)
    *outFailReason = reason;
  auto* node = dynamic_cast<QSGGeometryNode*>(oldNode);
  if (!node || !node->material())
    return nullptr;
  auto* mat = dynamic_cast<Nv12Material*>(node->material());
  if (!mat || !mat->texY() || !mat->texUV())
    return nullptr;
  return node;
}

static void dmaBufSgLogCapabilityOnceImpl(QQuickWindow* win) {
  if (s_capLogged.fetch_add(1, std::memory_order_relaxed) != 0)
    return;
  const bool qsb = qsbResourceExists();
  const bool gl = win && win->openglContext() != nullptr;
  qInfo() << "[Client][UI][DMABUF-SG][CAP] qsbBundle=" << qsb << " quickWindowOpenGlContext=" << gl
          << " ★ qsb=false→检查 qt_add_shaders 与 Qt6 ShaderTools；无 openglContext 时(Vulkan/Metal)本路径不可用";
}

static QSGNode* dmaBufSgBuildOrUpdateNodeImpl(QQuickWindow* win, QSGNode* oldNode,
                                              const SharedDmaBufFrame& handle, const QRectF& itemRect,
                                              const QString& streamTag, quintptr surfaceKey,
                                              quint64 dmaFrameId, bool* outOk, QString* outFailReason) {
  if (outOk)
    *outOk = false;
  if (!win || !handle || handle->frame.memoryType != VideoFrame::MemoryType::DMA_BUF) {
    if (outFailReason)
      *outFailReason = QStringLiteral("bad_args");
    if (QSGGeometryNode* kept =
            retainLastGoodNv12Node(oldNode, outOk, outFailReason, QStringLiteral("bad_args")))
      return kept;
    delete oldNode;
    return nullptr;
  }

  if (!win->openglContext() || QOpenGLContext::currentContext() != win->openglContext()) {
    static std::atomic<int> s_ctxLog{0};
    if (s_ctxLog.fetch_add(1) < 8) {
      qWarning() << "[Client][UI][DMABUF-SG][ERR] need current QOpenGLContext==win->openglContext() "
                    "stream="
                 << streamTag << " surf=0x" << Qt::hex << surfaceKey << Qt::dec;
    }
    if (QSGGeometryNode* kept = retainLastGoodNv12Node(oldNode, outOk, outFailReason,
                                                       QStringLiteral("no_matching_gl_context")))
      return kept;
    delete oldNode;
    return nullptr;
  }

  if (!qsbResourceExists()) {
    static std::atomic<int> s_qsbLog{0};
    if (s_qsbLog.fetch_add(1) < 4) {
      qWarning() << "[Client][UI][DMABUF-SG][ERR] missing :/client_dmabuf/shaders/*.qsb stream="
                 << streamTag;
    }
    if (QSGGeometryNode* kept =
            retainLastGoodNv12Node(oldNode, outOk, outFailReason, QStringLiteral("no_qsb")))
      return kept;
    delete oldNode;
    return nullptr;
  }

  auto* node = dynamic_cast<QSGGeometryNode*>(oldNode);
  Nv12Material* mat = nullptr;
  if (node && node->material())
    mat = dynamic_cast<Nv12Material*>(node->material());

  if (!node || !mat) {
    delete oldNode;
    node = new QSGGeometryNode();
    QSGGeometry* geo = new QSGGeometry(QSGGeometry::defaultAttributes_TexturedPoint2D(), 4);
    geo->setDrawingMode(QSGGeometry::TriangleStrip);
    setTexturedQuadGeometry(geo, itemRect);
    node->setGeometry(geo);
    node->setFlag(QSGNode::OwnsGeometry);
    mat = new Nv12Material();
    node->setMaterial(mat);
    node->setFlag(QSGNode::OwnsMaterial);
  } else {
    QSGGeometry* geo = node->geometry();
    if (geo && geo->vertexCount() == 4) {
      setTexturedQuadGeometry(geo, itemRect);
      node->markDirty(QSGNode::DirtyGeometry);
    }
  }

  QString ierr;
  if (!importNv12ToTextures(win, handle->frame, mat, &ierr)) {
    qWarning() << "[Client][UI][DMABUF-SG][IMPORT] fail stream=" << streamTag << " surf=0x" << Qt::hex
               << surfaceKey << Qt::dec << " err=" << ierr << " fourcc=0x" << Qt::hex
               << handle->frame.dmaBuf.drmFourcc << Qt::dec << " planes=" << handle->frame.dmaBuf.nbPlanes;
    if (outFailReason)
      *outFailReason = ierr;
    if (outOk)
      *outOk = false;
    if (mat->texY() && mat->texUV()) {
      node->markDirty(QSGNode::DirtyMaterial);
      return node;
    }
    delete node;
    return nullptr;
  }

  node->markDirty(QSGNode::DirtyMaterial);

  static std::atomic<int> s_okLog{0};
  if (s_okLog.fetch_add(1) < 8) {
    qInfo() << "[Client][UI][DMABUF-SG][OK] node stream=" << streamTag << " surf=0x" << Qt::hex
            << surfaceKey << Qt::dec << " wxh=" << handle->frame.width << "x" << handle->frame.height;
  }

  const auto& f = handle->frame;
  if (dmaFrameId > 0 &&
      (VideoFrameEvidence::shouldLogPipelineTrace(dmaFrameId) || VideoFrameEvidence::videoForensicsPackEnabled())) {
    qInfo().noquote()
        << QStringLiteral(
               "[Client][VideoE2E][DMABUF-SG][Layout] stream=%1 fid=%2 surf=0x%3 wxh=%4x%5 "
               "fourcc=0x%6 nbPlanes=%7 pitchY=%8 pitchUV=%9 offY=%10 offUV=%11 modY=0x%12 modUV=0x%13 "
               "fd0=%14 fd1=%15 mapRect=(%16,%17 %18x%19) ★ pitch/offset 错误易致条状/色条")
            .arg(streamTag)
            .arg(dmaFrameId)
            .arg(surfaceKey, 0, 16)
            .arg(f.width)
            .arg(f.height)
            .arg(f.dmaBuf.drmFourcc, 0, 16)
            .arg(f.dmaBuf.nbPlanes)
            .arg(f.dmaBuf.pitches[0])
            .arg(f.dmaBuf.pitches[1])
            .arg(f.dmaBuf.offsets[0])
            .arg(f.dmaBuf.offsets[1])
            .arg(static_cast<qulonglong>(f.dmaBuf.modifiers[0]), 0, 16)
            .arg(static_cast<qulonglong>(f.dmaBuf.nbPlanes > 1 ? f.dmaBuf.modifiers[1] : f.dmaBuf.modifiers[0]),
                 0, 16)
            .arg(f.dmaBuf.fds[0])
            .arg(f.dmaBuf.fds[1])
            .arg(itemRect.x(), 0, 'f', 1)
            .arg(itemRect.y(), 0, 'f', 1)
            .arg(itemRect.width(), 0, 'f', 1)
            .arg(itemRect.height(), 0, 'f', 1);
  }

  if (outOk)
    *outOk = true;
  if (outFailReason)
    outFailReason->clear();
  return node;
}

}  // namespace

void nv12DmaBufLogCapabilityOnce(QQuickWindow* win) {
  dmaBufSgLogCapabilityOnceImpl(win);
}

QSGNode* nv12DmaBufBuildOrUpdateNode(QQuickWindow* win, QSGNode* oldNode, const SharedDmaBufFrame& handle,
                                     const QRectF& itemRect, const QString& streamTag, quintptr surfaceKey,
                                     bool* outOk, QString* outFailReason, quint64 dmaFrameId) {
  return dmaBufSgBuildOrUpdateNodeImpl(win, oldNode, handle, itemRect, streamTag, surfaceKey,
                                       dmaFrameId, outOk, outFailReason);
}

#else

// Non-Linux / no shader bundle: stubs live in header.

#endif
