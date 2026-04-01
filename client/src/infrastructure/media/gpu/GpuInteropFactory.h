#pragma once
#include "IGpuInterop.h"
#include <memory>
#include <QString>
#include <QStringList>

/**
 * GPU 互操作后端工厂（《客户端架构设计》§3.1.2 零拷贝扩展）。
 *
 * 优先级（运行时探测，取第一个可用的）：
 *   1. EGLDmaBufInterop  — Linux Intel/AMD，真正零拷贝
 *   2. CpuUploadInterop  — 通用后备，所有平台均可用
 *
 * 调用 create() 时必须已有活跃 OpenGL 上下文（Qt 渲染线程）。
 */
class GpuInteropFactory {
public:
    /**
     * 创建最优可用的 GPU interop 实现。
     * @param preferredBackend 强制指定后端名称（空 = 自动选择）
     * @return 可用的 IGpuInterop 实例（永不返回 nullptr，最坏返回 CpuUploadInterop）
     */
    static std::unique_ptr<IGpuInterop> create(const QString& preferredBackend = {});

    /**
     * 查询所有编译时包含的后端名称（用于诊断）。
     */
    static QStringList availableBackends();
};
