#pragma once

#include "infrastructure/media/IHardwareDecoder.h"

#include <QMetaType>

#include <memory>

/** 解码线程 → 主线程/SceneGraph：携带 VideoFrame（DMA-BUF）与 poolRef 生命周期 */
struct DmaBufFrameHandle {
  VideoFrame frame;
};

using SharedDmaBufFrame = std::shared_ptr<DmaBufFrameHandle>;

Q_DECLARE_METATYPE(SharedDmaBufFrame)
