#pragma once
#include <array>
#include <atomic>

/**
 * 三缓冲（Triple Buffering）用于视频帧从解码线程到渲染线程的零拷贝传递。
 * 生产者写 write buffer，消费者读 read buffer，通过 middle 原子交换。
 * 无锁，延迟 < 1us。
 */
template <typename T>
class TripleBuffer {
public:
    TripleBuffer() : m_writeIndex(0), m_readIndex(2), m_middleIndex(1), m_newData(false) {}

    // 生产者：获取写缓冲区引用
    T& getWriteBuffer() {
        return m_buffers[m_writeIndex];
    }

    // 生产者：发布写缓冲区（原子交换 write 和 middle）
    void publishWrite() {
        int old = m_middleIndex.exchange(m_writeIndex, std::memory_order_acq_rel);
        m_writeIndex = old;
        m_newData.store(true, std::memory_order_release);
    }

    // 消费者：是否有新数据
    bool hasNewData() const {
        return m_newData.load(std::memory_order_acquire);
    }

    // 消费者：获取最新读缓冲区（如有新数据则交换 middle 和 read）
    const T& getReadBuffer() {
        if (m_newData.exchange(false, std::memory_order_acq_rel)) {
            int old = m_middleIndex.exchange(m_readIndex, std::memory_order_acq_rel);
            m_readIndex = old;
        }
        return m_buffers[m_readIndex];
    }

private:
    std::array<T, 3> m_buffers{};
    int m_writeIndex;
    int m_readIndex;
    std::atomic<int> m_middleIndex;
    std::atomic<bool> m_newData;
};
