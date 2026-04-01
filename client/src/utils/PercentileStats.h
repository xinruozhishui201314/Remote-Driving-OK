#pragma once
#include "CircularBuffer.h"
#include <algorithm>
#include <cstdint>
#include <vector>

/**
 * 滑动窗口百分位统计（用于延迟/帧时间监控）。
 * 线程安全：仅在单线程中调用。
 */
template <std::size_t WindowSize = 1000>
class PercentileStats {
public:
    void addSample(int64_t valueMicroseconds) {
        m_window.push_back(valueMicroseconds);
        m_dirty = true;
    }

    int64_t p50() const { return percentile(50); }
    int64_t p95() const { return percentile(95); }
    int64_t p99() const { return percentile(99); }
    int64_t min() const { compute(); return m_sorted.empty() ? 0 : m_sorted.front(); }
    int64_t max() const { compute(); return m_sorted.empty() ? 0 : m_sorted.back(); }
    double mean() const {
        if (m_window.empty()) return 0.0;
        int64_t sum = 0;
        for (std::size_t i = 0; i < m_window.size(); ++i) {
            sum += m_window[i];
        }
        return static_cast<double>(sum) / m_window.size();
    }

    std::size_t count() const { return m_window.size(); }
    void reset() { m_window.clear(); m_dirty = true; }

private:
    int64_t percentile(int pct) const {
        compute();
        if (m_sorted.empty()) return 0;
        std::size_t idx = static_cast<std::size_t>(pct / 100.0 * (m_sorted.size() - 1));
        return m_sorted[idx];
    }

    void compute() const {
        if (!m_dirty) return;
        m_sorted.resize(m_window.size());
        for (std::size_t i = 0; i < m_window.size(); ++i) {
            m_sorted[i] = m_window[i];
        }
        std::sort(m_sorted.begin(), m_sorted.end());
        m_dirty = false;
    }

    CircularBuffer<int64_t, WindowSize> m_window;
    mutable std::vector<int64_t> m_sorted;
    mutable bool m_dirty = true;
};

/**
 * 简化的帧率计数器。
 */
class FPSCounter {
public:
    void tick(int64_t timestampMs) {
        m_timestamps.push_back(timestampMs);
    }

    double currentFps(int64_t nowMs = 0, int64_t windowMs = 1000) const {
        if (m_timestamps.empty()) return 0.0;
        int64_t cutoff = (nowMs > 0 ? nowMs : m_timestamps.back()) - windowMs;
        int count = 0;
        for (std::size_t i = 0; i < m_timestamps.size(); ++i) {
            if (m_timestamps[i] >= cutoff) ++count;
        }
        return static_cast<double>(count) / (windowMs / 1000.0);
    }

private:
    CircularBuffer<int64_t, 300> m_timestamps;
};
