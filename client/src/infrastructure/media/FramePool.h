#pragma once
#include "IHardwareDecoder.h"
#include "../../utils/LockFreeQueue.h"
#include <QDebug>
#include <memory>
#include <vector>
#include <atomic>

/**
 * GPU 帧池（《客户端架构设计》§3.1.2）。
 * 预分配固定数量的 VideoFrame，避免运行时 GPU 内存分配。
 * 无锁归还，acquire() O(1)。
 */
class FramePool : public std::enable_shared_from_this<FramePool> {
public:
    explicit FramePool(std::size_t poolSize, uint32_t maxWidth, uint32_t maxHeight,
                        VideoFrame::MemoryType memType = VideoFrame::MemoryType::CPU_MEMORY)
    {
        m_frames.resize(poolSize);
        for (std::size_t i = 0; i < poolSize; ++i) {
            auto& f = m_frames[i];
            f.memoryType = memType;
            f.width  = maxWidth;
            f.height = maxHeight;
            if (memType == VideoFrame::MemoryType::CPU_MEMORY) {
                // 预分配 CPU 内存（YUV420: W*H*3/2）
                const std::size_t ySize = maxWidth * maxHeight;
                const std::size_t uvSize = ySize / 4;
                m_storage.emplace_back(ySize + uvSize * 2, 0);
                uint8_t* base = m_storage.back().data();
                f.planes[0] = {base,          maxWidth,     (uint32_t)ySize};
                f.planes[1] = {base + ySize,  maxWidth / 2, (uint32_t)uvSize};
                f.planes[2] = {base + ySize + uvSize, maxWidth / 2, (uint32_t)uvSize};
            }
            m_freeList.push(&f);
        }
        qInfo() << "[Client][FramePool] initialized"
                << "size=" << poolSize << maxWidth << "x" << maxHeight;
    }

    // 获取空闲帧（无锁）；返回 shared_ptr，析构时自动归还
    std::shared_ptr<VideoFrame> acquire() {
        VideoFrame* frame = nullptr;
        if (!m_freeList.pop(frame) || !frame) {
            qWarning() << "[Client][FramePool] pool exhausted!";
            return nullptr;
        }
        frame->reset();
        auto pool = shared_from_this();
        return std::shared_ptr<VideoFrame>(frame, [pool](VideoFrame* f) {
            pool->release(f);
        });
    }

    std::size_t size() const { return m_frames.size(); }

private:
    void release(VideoFrame* frame) {
        m_freeList.push(frame);
    }

    std::vector<VideoFrame> m_frames;
    std::vector<std::vector<uint8_t>> m_storage; // CPU 内存后备
    LockFreeStack<VideoFrame> m_freeList;
};
