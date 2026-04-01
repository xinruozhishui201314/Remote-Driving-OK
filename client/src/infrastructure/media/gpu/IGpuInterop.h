#pragma once
#include "../IHardwareDecoder.h"
#include <cstdint>
#include <QString>

/**
 * GPU 帧导入接口（《客户端架构设计》§3.1.2 零拷贝扩展）。
 *
 * 各实现的作用域：
 *   EGLDmaBufInterop  — Linux，Intel/AMD Mesa，DMA-BUF → EGL → GL（真正零拷贝）
 *   CpuUploadInterop  — 所有平台，CPU 内存 → GL 纹理（PBO 异步上传）
 *
 * *** 所有方法必须在 Qt 渲染线程调用 ***
 */
class IGpuInterop {
public:
    /**
     * 导入后的 GL 纹理句柄集合。
     * 对于 NV12：yTexId = Y plane (GL_R8), uvTexId = UV plane (GL_RG8), vTexId = 0。
     * 对于 YUV420P：yTexId/uvTexId/vTexId = Y/U/V (GL_R8)。
     */
    struct TextureSet {
        uint32_t yTexId  = 0;
        uint32_t uvTexId = 0; // NV12 UV 交织或 YUV420P U 平面
        uint32_t vTexId  = 0; // YUV420P V 平面，NV12 时为 0
        int width        = 0;
        int height       = 0;
        bool isNv12      = false; // 影响 shader uniform 和纹理采样方式
        bool valid       = false;
    };

    virtual ~IGpuInterop() = default;

    /**
     * 将 VideoFrame 导入为 GL 纹理。
     * @param frame   要导入的帧（CPU 或 DMA-BUF）
     * @return 导入结果；valid=false 表示导入失败
     */
    virtual TextureSet importFrame(const VideoFrame& frame) = 0;

    /**
     * 释放 importFrame 返回的纹理（若由 interop 管理）。
     * CpuUploadInterop 复用持久纹理，通常空实现。
     * EGLDmaBufInterop 在此销毁 EGL Image。
     */
    virtual void releaseTextures(const TextureSet& textures) = 0;

    /**
     * 运行时探测：当前实现是否可用（检查扩展/驱动）。
     */
    virtual bool isAvailable() const = 0;

    /**
     * 实现名称（用于日志）。
     */
    virtual QString name() const = 0;
};
