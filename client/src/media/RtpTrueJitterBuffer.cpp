/**
 * 环境变量（节选）：
 * - CLIENT_RTP_PLAYOUT_MODE: off | fixed | adaptive | ntp | hybrid（默认：未设置时若
 * CLIENT_RTP_PLAYOUT_DELAY_MS>0 则 fixed，否则 off）
 * - CLIENT_RTP_PLAYOUT_DELAY_MS: fixed / adaptive 基线（adaptive 仍受 MIN/MAX 夹紧）
 * - CLIENT_RTP_PLAYOUT_MAX_PACKETS: 缓冲包数上限，64..8192，默认 1024；溢出 → 清空 + PLI
 * 建议（由上层处理）
 * - CLIENT_RTP_PLAYOUT_SEQ_ORDER: 1=按 seq 有序出队（默认 1）；0=FIFO+deadline（乱序网不推荐）
 * - CLIENT_RTP_PLAYOUT_HOLE_MS: seq 洞超时（ms），默认 80 → 清空缓冲并请求关键帧
 * - CLIENT_RTP_CLOCK_HZ: RTP 时钟，默认 90000
 * - CLIENT_RTP_NTP_MARGIN_MS: SR 映射附加偏移，默认 0
 * - CLIENT_RTP_SR_STALE_MS: SR 超过该时间未更新则 hybrid 退回 adaptive，默认 800
 * - CLIENT_RTP_JITTER_MIN_MS / CLIENT_RTP_JITTER_MAX_MS: 自适应 target 夹紧，默认 20 / 180
 * - CLIENT_RTP_JITTER_GAIN: EWMA 抖动 → target 的系数，默认 1.5
 * - CLIENT_RTP_JITTER_METRICS_MS: >0 时每路每隔该 ms 打一条指标日志，0=关闭
 */
#include "RtpTrueJitterBuffer.h"

#include "RtpStreamClockContext.h"

#include <QDateTime>
#include <QDebug>
#include <QString>

#include <cmath>

namespace {

static quint16 readBe16(const uint8_t *p) {
  return static_cast<quint16>((static_cast<unsigned>(p[0]) << 8) | static_cast<unsigned>(p[1]));
}

static quint32 readBe32(const uint8_t *p) {
  return (static_cast<quint32>(p[0]) << 24) | (static_cast<quint32>(p[1]) << 16) |
         (static_cast<quint32>(p[2]) << 8) | static_cast<quint32>(p[3]);
}

/** a 在 seq 环上早于 b */
static bool seqBefore(quint16 a, quint16 b) {
  return static_cast<quint16>(b - a) < static_cast<quint16>(0x8000u);
}

static RtpPlayoutMode parseModeEnv(int *outFixedDelay) {
  int fixed = qEnvironmentVariableIntValue("CLIENT_RTP_PLAYOUT_DELAY_MS");
  if (fixed < 0)
    fixed = 0;
  *outFixedDelay = fixed;

  const QByteArray raw = qgetenv("CLIENT_RTP_PLAYOUT_MODE");
  const QString m = QString::fromUtf8(raw).trimmed().toLower();
  if (m.isEmpty()) {
    if (fixed > 0)
      return RtpPlayoutMode::Fixed;
    return RtpPlayoutMode::Off;
  }
  if (m == QLatin1String("off"))
    return RtpPlayoutMode::Off;
  if (m == QLatin1String("fixed"))
    return RtpPlayoutMode::Fixed;
  if (m == QLatin1String("adaptive"))
    return RtpPlayoutMode::Adaptive;
  if (m == QLatin1String("ntp"))
    return RtpPlayoutMode::Ntp;
  if (m == QLatin1String("hybrid"))
    return RtpPlayoutMode::Hybrid;
  qWarning() << "[Client][Media][Jitter] 未知 CLIENT_RTP_PLAYOUT_MODE=" << raw << " 按 off";
  return RtpPlayoutMode::Off;
}

}  // namespace

void RtpTrueJitterBuffer::reloadEnv() {
  int fixedDelay = 0;
  m_mode = parseModeEnv(&fixedDelay);
  m_fixedDelayMs = fixedDelay;
  if (m_mode == RtpPlayoutMode::Fixed && m_fixedDelayMs <= 0)
    m_fixedDelayMs = 40;

  const int cap = qEnvironmentVariableIntValue("CLIENT_RTP_PLAYOUT_MAX_PACKETS");
  if (cap >= 64 && cap <= 8192)
    m_maxPackets = cap;

  {
    const QByteArray seqo = qgetenv("CLIENT_RTP_PLAYOUT_SEQ_ORDER");
    if (seqo.isEmpty())
      m_seqOrder = true;
    else
      m_seqOrder = qEnvironmentVariableIntValue("CLIENT_RTP_PLAYOUT_SEQ_ORDER") != 0;
  }

  const int hole = qEnvironmentVariableIntValue("CLIENT_RTP_PLAYOUT_HOLE_MS");
  if (hole >= 10 && hole <= 2000)
    m_holeTimeoutMs = hole;

  const int stale = qEnvironmentVariableIntValue("CLIENT_RTP_SR_STALE_MS");
  if (stale >= 100 && stale <= 10000)
    m_srStaleMs = stale;

  const int hz = qEnvironmentVariableIntValue("CLIENT_RTP_CLOCK_HZ");
  if (hz >= 8000 && hz <= 96000)
    m_clockHz = static_cast<double>(hz);

  m_ntpMarginMs = qEnvironmentVariableIntValue("CLIENT_RTP_NTP_MARGIN_MS");

  const int jmin = qEnvironmentVariableIntValue("CLIENT_RTP_JITTER_MIN_MS");
  if (jmin >= 0 && jmin <= 500)
    m_adaptMinMs = jmin;
  const int jmax = qEnvironmentVariableIntValue("CLIENT_RTP_JITTER_MAX_MS");
  if (jmax >= m_adaptMinMs && jmax <= 800)
    m_adaptMaxMs = jmax;

  const QString gainEnv = QString::fromUtf8(qgetenv("CLIENT_RTP_JITTER_GAIN"));
  if (!gainEnv.isEmpty()) {
    bool ok = false;
    const double g = gainEnv.toDouble(&ok);
    if (ok && g >= 0.1 && g <= 10.0)
      m_adaptGain = g;
  }

  m_metricsIntervalMs = qEnvironmentVariableIntValue("CLIENT_RTP_JITTER_METRICS_MS");
}

void RtpTrueJitterBuffer::clear() {
  m_bySeq.clear();
  m_fifo.clear();
  m_nextInited = false;
  m_holeStartWallMs = 0;
  m_holeKeyframePending = false;
  m_jitterPrev = false;
}

bool RtpTrueJitterBuffer::empty() const { return m_seqOrder ? m_bySeq.empty() : m_fifo.empty(); }

bool RtpTrueJitterBuffer::consumeHoleKeyframeRequest() {
  const bool r = m_holeKeyframePending;
  m_holeKeyframePending = false;
  return r;
}

void RtpTrueJitterBuffer::checkHoleTimeout(qint64 nowWallMs) {
  if (!m_seqOrder || !m_nextInited || m_bySeq.empty())
    return;
  if (m_bySeq.find(m_nextSeq) != m_bySeq.end()) {
    m_holeStartWallMs = 0;
    return;
  }
  if (m_holeStartWallMs == 0)
    m_holeStartWallMs = nowWallMs;
  else if (nowWallMs - m_holeStartWallMs > static_cast<qint64>(m_holeTimeoutMs)) {
    m_bySeq.clear();
    m_fifo.clear();
    m_nextInited = false;
    m_holeStartWallMs = 0;
    m_holeKeyframePending = true;
    ++m_metrics_overflow_clears;
    qWarning() << "[Client][Media][Jitter] seq 洞超时 → 清空缓冲并建议 IDR holeMs="
               << m_holeTimeoutMs;
  }
}

void RtpTrueJitterBuffer::updateAdaptiveJitter(qint64 recvWallMs, quint32 rtp_ts) {
  if (!m_jitterPrev) {
    m_jitterPrev = true;
    m_prevRecvWallMs = recvWallMs;
    m_prevRtpTs = rtp_ts;
    m_targetDelayMs =
        qBound(m_adaptMinMs, m_fixedDelayMs > 0 ? m_fixedDelayMs : m_adaptMinMs, m_adaptMaxMs);
    return;
  }
  const double ia_ms = static_cast<double>(recvWallMs - m_prevRecvWallMs);
  const int32_t dts = static_cast<int32_t>(rtp_ts - m_prevRtpTs);
  const double ts_ms = static_cast<double>(dts) * 1000.0 / m_clockHz;
  const double d = std::fabs(ia_ms - ts_ms);
  m_jitterEwmaMs += (d - m_jitterEwmaMs) / 16.0;
  m_prevRecvWallMs = recvWallMs;
  m_prevRtpTs = rtp_ts;
  const int t = static_cast<int>(
      std::lround(static_cast<double>(m_adaptMinMs) + m_adaptGain * m_jitterEwmaMs));
  m_targetDelayMs = qBound(m_adaptMinMs, t, m_adaptMaxMs);
}

qint64 RtpTrueJitterBuffer::computeDeadlineWallMs(RtpPlayoutMode mode, qint64 recvWallMs,
                                                  quint16 seq, quint32 rtp_ts, quint32 ssrc,
                                                  qint64 nowWallMs) {
  (void)seq;
  switch (mode) {
    case RtpPlayoutMode::Off:
      return recvWallMs;
    case RtpPlayoutMode::Fixed:
      return recvWallMs + static_cast<qint64>(m_fixedDelayMs);
    case RtpPlayoutMode::Adaptive:
      updateAdaptiveJitter(recvWallMs, rtp_ts);
      return recvWallMs + static_cast<qint64>(m_targetDelayMs);
    case RtpPlayoutMode::Ntp:
    case RtpPlayoutMode::Hybrid: {
      const bool haveClock = m_clock && m_clock->sr_valid.load(std::memory_order_acquire) &&
                             !m_clock->isSrStale(nowWallMs, m_srStaleMs);
      const quint32 sr_ssrc = haveClock ? m_clock->sr_ssrc.load(std::memory_order_acquire) : 0u;
      const bool ssrcOk = haveClock && (sr_ssrc == ssrc);
      if (ssrcOk) {
        const qint64 sr_wall = m_clock->sr_recv_wall_ms.load(std::memory_order_acquire);
        const quint32 sr_rtp = m_clock->sr_rtp_ts.load(std::memory_order_acquire);
        const int32_t dt = static_cast<int32_t>(rtp_ts - sr_rtp);
        const double dms = static_cast<double>(dt) * 1000.0 / m_clockHz;
        qint64 play = sr_wall + static_cast<qint64>(dms + 0.5) + static_cast<qint64>(m_ntpMarginMs);
        if (play < recvWallMs)
          play = recvWallMs;
        const qint64 cap = recvWallMs + static_cast<qint64>(m_adaptMaxMs * 2);
        if (play > cap)
          play = cap;
        return play;
      }
      updateAdaptiveJitter(recvWallMs, rtp_ts);
      return recvWallMs + static_cast<qint64>(m_targetDelayMs);
    }
    default:
      updateAdaptiveJitter(recvWallMs, rtp_ts);
      return recvWallMs + static_cast<qint64>(m_targetDelayMs);
  }
}

int RtpTrueJitterBuffer::enqueue(RtpIngressPacket &&pkt, qint64 recvWallMs, const uint8_t *rtpHdr,
                                 std::size_t rtpLen) {
  if (m_mode == RtpPlayoutMode::Off)
    return 0;
  if (!rtpHdr || rtpLen < 12)
    return 0;

  const quint16 seq = readBe16(rtpHdr + 2);
  const quint32 rtp_ts = readBe32(rtpHdr + 4);
  const quint32 ssrc = readBe32(rtpHdr + 8);
  const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
  const qint64 deadline = computeDeadlineWallMs(m_mode, recvWallMs, seq, rtp_ts, ssrc, nowMs);

  const std::size_t total = m_seqOrder ? m_bySeq.size() : m_fifo.size();
  if (total >= static_cast<std::size_t>(m_maxPackets)) {
    clear();
    m_nextInited = false;
    ++m_metrics_overflow_clears;
    return 1;
  }

  if (m_seqOrder) {
    if (m_nextInited) {
      if (seq != m_nextSeq && seqBefore(seq, m_nextSeq)) {
        ++m_metrics_late_drops;
        return 1;
      }
    } else {
      m_nextSeq = seq;
      m_nextInited = true;
    }
    m_bySeq[seq] = Slot{std::move(pkt), deadline};
  } else {
    m_fifo.push_back(Slot{std::move(pkt), deadline});
  }
  return 0;
}

bool RtpTrueJitterBuffer::tryPopDue(qint64 nowWallMs, RtpIngressPacket &out) {
  if (m_mode == RtpPlayoutMode::Off)
    return false;

  checkHoleTimeout(nowWallMs);

  if (!m_seqOrder) {
    if (m_fifo.empty())
      return false;
    if (m_fifo.front().deadline_wall_ms > nowWallMs)
      return false;
    out = std::move(m_fifo.front().pkt);
    m_fifo.pop_front();
    return true;
  }

  if (!m_nextInited)
    return false;
  const auto it = m_bySeq.find(m_nextSeq);
  if (it == m_bySeq.end())
    return false;
  if (it->second.deadline_wall_ms > nowWallMs)
    return false;
  out = std::move(it->second.pkt);
  m_bySeq.erase(it);
  ++m_nextSeq;
  m_holeStartWallMs = 0;
  return true;
}

int RtpTrueJitterBuffer::millisUntilNextDue(qint64 nowWallMs) const {
  if (m_mode == RtpPlayoutMode::Off)
    return -1;
  if (!m_seqOrder) {
    if (m_fifo.empty())
      return -1;
    const qint64 d = m_fifo.front().deadline_wall_ms - nowWallMs;
    if (d <= 0)
      return 0;
    return static_cast<int>(qMin<qint64>(d, 500));
  }
  if (!m_nextInited)
    return -1;
  const auto it = m_bySeq.find(m_nextSeq);
  if (it == m_bySeq.end())
    return 1;
  const qint64 d = it->second.deadline_wall_ms - nowWallMs;
  if (d <= 0)
    return 0;
  return static_cast<int>(qMin<qint64>(d, 500));
}

void RtpTrueJitterBuffer::logMetricsIfDue(qint64 nowWallMs, const QString &streamTag) {
  if (m_metricsIntervalMs <= 0 || m_mode == RtpPlayoutMode::Off)
    return;
  if (m_lastMetricsLogMs == 0)
    m_lastMetricsLogMs = nowWallMs;
  if (nowWallMs - m_lastMetricsLogMs < m_metricsIntervalMs)
    return;
  m_lastMetricsLogMs = nowWallMs;

  const char *modeStr = "off";
  switch (m_mode) {
    case RtpPlayoutMode::Fixed:
      modeStr = "fixed";
      break;
    case RtpPlayoutMode::Adaptive:
      modeStr = "adaptive";
      break;
    case RtpPlayoutMode::Ntp:
      modeStr = "ntp";
      break;
    case RtpPlayoutMode::Hybrid:
      modeStr = "hybrid";
      break;
    default:
      break;
  }

  quint64 sr_ok = 0, sr_bad = 0, sr_fail = 0, rtcp_blk = 0;
  if (m_clock) {
    sr_ok = m_clock->metrics_sr_accepted.load(std::memory_order_relaxed);
    sr_bad = m_clock->metrics_sr_rejected_ssrc.load(std::memory_order_relaxed);
    sr_fail = m_clock->metrics_sr_parse_fail.load(std::memory_order_relaxed);
    rtcp_blk = m_clock->metrics_rtcp_blocks_seen.load(std::memory_order_relaxed);
  }

  qInfo() << "[Client][Media][Jitter][Metrics] stream=" << streamTag << " mode=" << modeStr
          << " buf=" << static_cast<qint64>(m_seqOrder ? m_bySeq.size() : m_fifo.size())
          << " jitterEwmaMs=" << m_jitterEwmaMs << " targetDelayMs=" << m_targetDelayMs
          << " fixedMs=" << m_fixedDelayMs
          << " sr_valid=" << (m_clock && m_clock->sr_valid.load(std::memory_order_relaxed) ? 1 : 0)
          << " sr_accept=" << sr_ok << " sr_ssrc_rej=" << sr_bad << " sr_parse_fail=" << sr_fail
          << " rtcp_blk=" << rtcp_blk << " overflow_clears=" << m_metrics_overflow_clears
          << " late_drops=" << m_metrics_late_drops;
}
