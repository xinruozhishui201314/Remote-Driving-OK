#include "RtpStreamClockContext.h"

void RtpStreamClockContext::resetMapping() { sr_valid.store(0, std::memory_order_release); }

void RtpStreamClockContext::updateFromSenderReport(quint32 ssrc, quint32 rtp_ts, qint64 wall_ms) {
  sr_ssrc.store(ssrc, std::memory_order_relaxed);
  sr_rtp_ts.store(rtp_ts, std::memory_order_relaxed);
  sr_recv_wall_ms.store(wall_ms, std::memory_order_relaxed);
  sr_valid.store(1, std::memory_order_release);
  metrics_sr_accepted.fetch_add(1, std::memory_order_relaxed);
}

bool RtpStreamClockContext::isSrStale(qint64 now_wall_ms, int max_age_ms) const {
  if (!sr_valid.load(std::memory_order_acquire))
    return true;
  const qint64 t = sr_recv_wall_ms.load(std::memory_order_acquire);
  if (t <= 0)
    return true;
  return (now_wall_ms - t) > static_cast<qint64>(max_age_ms);
}
