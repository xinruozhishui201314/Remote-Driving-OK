#pragma once

/**
 * 每路视频流共享的时钟上下文：在 libdatachannel 媒体线程由 RTCP SR 更新，
 * 在解码线程由 RtpTrueJitterBuffer 只读（原子），用于 RTP TS → 接收端墙钟 playout 估计。
 */
#include <QtGlobal>

#include <atomic>

struct RtpStreamClockContext {
  std::atomic<qint64> sr_recv_wall_ms{0};
  std::atomic<quint32> sr_rtp_ts{0};
  std::atomic<quint32> sr_ssrc{0};
  /** 1 = 曾成功解析过与本路 SSRC 匹配的 SR，且未过期 */
  std::atomic<int> sr_valid{0};

  std::atomic<quint64> metrics_sr_accepted{0};
  std::atomic<quint64> metrics_sr_rejected_ssrc{0};
  std::atomic<quint64> metrics_sr_parse_fail{0};
  std::atomic<quint64> metrics_rtcp_blocks_seen{0};

  void resetMapping();

  /** @param wall_ms 解析 SR 时接收端墙钟 ms */
  void updateFromSenderReport(quint32 ssrc, quint32 rtp_ts, qint64 wall_ms);

  bool isSrStale(qint64 now_wall_ms, int max_age_ms) const;
};
