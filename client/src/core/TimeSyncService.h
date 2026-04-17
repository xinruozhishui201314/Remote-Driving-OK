#pragma once
#include <QObject>
#include <atomic>
#include <cstdint>

/**
 * 高精度时间同步服务（《客户端架构设计》§4.1 核心链路优化）。
 * 
 * 功能：
 *   1. 通过心跳包测量 RTT（往返延迟）。
 *   2. 计算 Client-Vehicle 时钟偏移（Clock Offset）。
 *   3. 为视频帧和控制指令提供统一的时间基准。
 */
class TimeSyncService : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY(TimeSyncService)

 public:
  static TimeSyncService& instance() {
    static TimeSyncService inst;
    return inst;
  }

  explicit TimeSyncService(QObject* parent = nullptr);

  /**
   * 更新同步信息。
   * @param t1 客户端发送心跳的本地时间 (us)
   * @param t2 车辆接收到心跳的车辆时间 (us)
   * @param t3 车辆发送响应的车辆时间 (us)
   * @param t4 客户端接收到响应的本地时间 (us)
   */
  void update(int64_t t1, int64_t t2, int64_t t3, int64_t t4);

  /**
   * 将车辆时间转换为客户端本地时间。
   */
  int64_t vehicleToLocal(int64_t vehicleTimeUs) const;

  /**
   * 将客户端本地时间转换为车辆时间。
   */
  int64_t localToVehicle(int64_t localTimeUs) const;

  /**
   * 获取当前估计的 RTT (us)。
   */
  int64_t currentRttUs() const { return m_rttUs.load(std::memory_order_relaxed); }

  /**
   * 获取当前估计的时钟偏移 (us)。
   * offset = vehicle_time - client_time
   */
  int64_t clockOffsetUs() const { return m_offsetUs.load(std::memory_order_relaxed); }

 signals:
  void syncUpdated(int64_t rttUs, int64_t offsetUs);

 private:
  std::atomic<int64_t> m_rttUs{0};
  std::atomic<int64_t> m_offsetUs{0};
  
  // 指数移动平均系数
  static constexpr double kAlpha = 0.1; 
};
