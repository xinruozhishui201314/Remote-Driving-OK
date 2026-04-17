#pragma once

#include <media/DmaBufFrameHandle.h>

#include <QRectF>
#include <QString>

class QQuickWindow;
class QSGNode;

enum class RemoteSurfaceSgKind : uint8_t { None = 0, SimpleTexture, Nv12DmaBuf };

#if defined(CLIENT_HAVE_NV12_DMABUF_SG)

/**
 * 在 Scene Graph 渲染线程从 DMA-BUF NV12 建 EGLImage→GL 纹理→双采样 QSGMaterial。
 * @param outKind 写入 Nv12DmaBuf；失败时调用方应删 node 并回退 CPU 路径
 */
QSGNode* nv12DmaBufBuildOrUpdateNode(QQuickWindow* win, QSGNode* oldNode, const SharedDmaBufFrame& handle,
                                     const QRectF& itemRect, const QString& streamTag, quintptr surfaceKey,
                                     bool* outOk, QString* outFailReason, quint64 dmaFrameId = 0);

/** 首次探测：OpenGL RHI + EGL 扩展 + qsb 资源 */
void nv12DmaBufLogCapabilityOnce(QQuickWindow* win);

#else

inline QSGNode* nv12DmaBufBuildOrUpdateNode(QQuickWindow*, QSGNode*, const SharedDmaBufFrame&,
                                            const QRectF&, const QString&, quintptr, bool* outOk,
                                            QString* outFailReason, quint64 = 0) {
  if (outOk)
    *outOk = false;
  if (outFailReason)
    *outFailReason = QStringLiteral("CLIENT_HAVE_NV12_DMABUF_SG_off");
  return nullptr;
}

inline void nv12DmaBufLogCapabilityOnce(QQuickWindow*) {}

#endif
