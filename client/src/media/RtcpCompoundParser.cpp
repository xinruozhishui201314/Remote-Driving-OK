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
  // 与 webrtcclient onMessage 一致：200–207（含 RTPFB=205 / PSFB=206），避免首块为 205/206 的复合包整段被拒
  if (v0 != 2u || pt0 < 200u || pt0 > 207u)
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

    // ★ WHY5 诊断：记录所有 RTCP 块类型
    // PT=200 SR（发送报告）  PT=201 RR（接收报告）  PT=202 SDES  PT=203 BYE
    // PT=205 RTPFB（包含 NACK=FMT1）  PT=206 PSFB（包含 PLI=FMT1/FIR=FMT4）
    // 若收到 PT=205/FMT1 → ZLMediaKit 在响应 NACK（重传已生效）
    // 若仅有 PT=200 SR，无 PT=205 → NACK 未生效，发送端不响应重传
    if (pt != 200u) {
        // 非 SR：额外记录，帮助判断 RTCP 通道活跃情况和 NACK 支持
        const uint8_t fmt = static_cast<uint8_t>(blk[0]) & 0x1Fu;  // RC/FMT 字段
        static std::atomic<int> s_nonSrCount{0};
        const int c = ++s_nonSrCount;
        const bool isNack    = (pt == 205u && fmt == 1u);
        const bool isPli     = (pt == 206u && fmt == 1u);
        const bool isFir     = (pt == 206u && fmt == 4u);
        const bool isRr      = (pt == 201u);
        const bool isBye     = (pt == 203u);
        if (c <= 30 || (c % 200) == 0 || isNack) {
            QString ptDesc;
            if (isNack)      ptDesc = QStringLiteral("RTPFB/NACK ★重传请求");
            else if (isPli)  ptDesc = QStringLiteral("PSFB/PLI 关键帧请求");
            else if (isFir)  ptDesc = QStringLiteral("PSFB/FIR 全帧刷新");
            else if (isRr)   ptDesc = QStringLiteral("RR 接收报告");
            else if (isBye)  ptDesc = QStringLiteral("BYE 告别");
            else             ptDesc = QStringLiteral("PT=%1/FMT=%2").arg(pt).arg(fmt);
            qInfo().noquote()
                << QStringLiteral("[Client][RTCP][Block] WHY5诊断 pt=%1 fmt=%2 desc=%3 n=%4"
                                  " | NACK(PT=205/FMT1)出现→ZLM响应重传，FrameHole应减少")
                       .arg(pt).arg(fmt).arg(ptDesc).arg(c);
        }
    }

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
