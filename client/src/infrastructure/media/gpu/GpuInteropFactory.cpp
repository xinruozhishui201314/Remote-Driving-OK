#include "GpuInteropFactory.h"
#include "EGLDmaBufInterop.h"
#include "CpuUploadInterop.h"
#include <QDebug>

std::unique_ptr<IGpuInterop> GpuInteropFactory::create(const QString& preferredBackend)
{
    // 注意：此函数在 Qt 渲染线程调用，可安全探测 GL/EGL 扩展

    qInfo() << "[Client][GpuInteropFactory] available backends:" << availableBackends();

    // ── 按优先级尝试 ──────────────────────────────────────────────────────────

#ifdef ENABLE_EGL_DMABUF
    if (preferredBackend.isEmpty() || preferredBackend == "EGLDmaBuf") {
        auto interop = std::make_unique<EGLDmaBufInterop>();
        if (interop->isAvailable()) {
            qInfo() << "[Client][GpuInteropFactory] selected: EGLDmaBufInterop (zero-copy)";
            return interop;
        }
        qInfo() << "[Client][GpuInteropFactory] EGLDmaBufInterop unavailable, falling back";
    }
#else
    Q_UNUSED(preferredBackend)
#endif

    // 通用后备：CPU 上传（所有平台）
    qInfo() << "[Client][GpuInteropFactory] selected: CpuUploadInterop (CPU path)";
    return std::make_unique<CpuUploadInterop>();
}

QStringList GpuInteropFactory::availableBackends()
{
    QStringList result;
#ifdef ENABLE_EGL_DMABUF
    result << "EGLDmaBuf";
#endif
    result << "CpuUpload";
    return result;
}
