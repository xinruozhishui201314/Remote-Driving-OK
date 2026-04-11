#pragma once
#include <chrono>
#include <cstdint>

/**
 * 高精度时间工具，统一在全客户端使用。
 */
namespace TimeUtils {

inline int64_t nowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

inline int64_t nowUs() {
  return std::chrono::duration_cast<std::chrono::microseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

// 墙钟（用于日志时间戳，不用于时间差计算）
inline int64_t wallClockMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

// 高精度计时器（纳秒）
inline int64_t nowNs() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::high_resolution_clock::now().time_since_epoch())
      .count();
}

}  // namespace TimeUtils
