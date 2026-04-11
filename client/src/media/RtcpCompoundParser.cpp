#include "RtcpCompoundParser.h"

#include <QDebug>
#include <QString>

bool rtcpCompoundTryConsumeAndUpdateClock(const uint8_t *data, std::size_t len,
                                          RtpStreamClockContext *ctx, const qint64 recv_wall_ms,
                                          const quint32 expected_ssrc, QString *outLog) {
  if (!ctx || len < 4u)
    return false;
  const unsigned v0 = static_cast<unsigned>(data[0]) >> 6;
  const unsigned pt0 = static_cast<unsigned>(data[1]);
  if (v0 != 2u || pt0 < 200u || pt0 > 204u)
    return false;

  std::size_t off = 0;
  while (off + 4u <= len) {
    const uint8_t *blk = data + off;
    const unsigned v = static_cast<unsigned>(blk[0]) >> 6;
    const unsigned pt = static_cast<unsigned>(blk[1]);
    const uint16_t len_words =
        static_cast<uint16_t>((static_cast<unsigned>(blk[2]) << 8) | static_cast<unsigned>(blk[3]));
    const std::size_t pkt_octets = (static_cast<std::size_t>(len_words) + 1u) * 4u;
    if (pkt_octets < 4u || off + pkt_octets > len) {
      ctx->metrics_sr_parse_fail.fetch_add(1, std::memory_order_relaxed);
      if (outLog)
        *outLog = QStringLiteral("rtcp_len_bad");
      return true;
    }
    ctx->metrics_rtcp_blocks_seen.fetch_add(1, std::memory_order_relaxed);

    if (v == 2u && pt == 200u && pkt_octets >= 4u + 24u) {
      const uint8_t *body = blk + 4u;
      const quint32 ssrc = (static_cast<quint32>(body[0]) << 24) |
                           (static_cast<quint32>(body[1]) << 16) |
                           (static_cast<quint32>(body[2]) << 8) | static_cast<quint32>(body[3]);
      const quint32 rtp_ts = (static_cast<quint32>(body[12]) << 24) |
                             (static_cast<quint32>(body[13]) << 16) |
                             (static_cast<quint32>(body[14]) << 8) | static_cast<quint32>(body[15]);
      if (expected_ssrc != 0u && ssrc != expected_ssrc) {
        ctx->metrics_sr_rejected_ssrc.fetch_add(1, std::memory_order_relaxed);
        static std::atomic<int> s_log{0};
        if (++s_log <= 8)
          qInfo() << "[Client][Media][RTCP][SR] SSRC 不匹配 期望=" << expected_ssrc
                  << " sr=" << ssrc;
      } else {
        ctx->updateFromSenderReport(ssrc, rtp_ts, recv_wall_ms);
        if (outLog)
          *outLog = QStringLiteral("sr_ok");
      }
    }
    off += pkt_octets;
  }
  return true;
}
