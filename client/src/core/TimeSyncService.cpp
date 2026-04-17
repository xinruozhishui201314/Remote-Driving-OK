#include "TimeSyncService.h"
#include <QDebug>
#include <algorithm>

TimeSyncService::TimeSyncService(QObject* parent) : QObject(parent) {}

void TimeSyncService::update(int64_t t1, int64_t t2, int64_t t3, int64_t t4) {
  // RTT = (t4 - t1) - (t3 - t2)
  int64_t tripTimeUs = (t4 - t1) - (t3 - t2);
  tripTimeUs = std::max<int64_t>(0, tripTimeUs);

  // Offset = ((t2 - t1) + (t3 - t4)) / 2
  int64_t offsetUs = ((t2 - t1) + (t3 - t4)) / 2;

  // 指数移动平均 (EMA) 平滑数据，减少网络抖动影响
  int64_t oldRtt = m_rttUs.load(std::memory_order_relaxed);
  int64_t newRtt = static_cast<int64_t>(oldRtt * (1.0 - kAlpha) + tripTimeUs * kAlpha);
  m_rttUs.store(newRtt, std::memory_order_relaxed);

  int64_t oldOffset = m_offsetUs.load(std::memory_order_relaxed);
  int64_t newOffset = static_cast<int64_t>(oldOffset * (1.0 - kAlpha) + offsetUs * kAlpha);
  m_offsetUs.store(newOffset, std::memory_order_relaxed);

  static int s_logCount = 0;
  if (++s_logCount % 100 == 0) {
    qInfo() << "[Client][TimeSync] RTT=" << newRtt / 1000.0 << "ms"
            << "Offset=" << newOffset / 1000.0 << "ms";
  }

  emit syncUpdated(newRtt, newOffset);
}

int64_t TimeSyncService::vehicleToLocal(int64_t vehicleTimeUs) const {
  return vehicleTimeUs - m_offsetUs.load(std::memory_order_relaxed);
}

int64_t TimeSyncService::localToVehicle(int64_t localTimeUs) const {
  return localTimeUs + m_offsetUs.load(std::memory_order_relaxed);
}
