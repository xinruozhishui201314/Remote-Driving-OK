#pragma once

/**
 * @file RtpTrueJitterBuffer.h
 *
 * 「真」抖动缓冲：可选
 * - **fixed**：墙钟 recv + 固定 ms（等同原 RtpFixedPlayoutQueue）
 * - **adaptive**：RFC3550 风格 inter-arrival 抖动 EWMA → 动态 target delay
 * - **ntp**：RTCP SR 将 RTP TS 锚到「收到 SR 时」的墙钟 + Δrtp（需 RtpStreamClockContext）
 * - **hybrid**：SR 有效且未过期用 ntp，否则退回 adaptive
 *
 * 默认按 **RTP seq** 有序出队（与 H264 分片顺序一致）；可 env 关闭为仅按 deadline（不推荐）。
 *
 * 环境变量见 RtpTrueJitterBuffer.cpp 顶部注释。
 */
#include "RtpIngressTypes.h"

#include <QtGlobal>

#include <deque>
#include <unordered_map>

class QString;

class RtpStreamClockContext;

enum class RtpPlayoutMode {
  Off = 0,
  Fixed,
  Adaptive,
  Ntp,
  Hybrid,
};

class RtpTrueJitterBuffer {
 public:
  RtpTrueJitterBuffer();
  // 禁用拷贝以满足 -Weffc++
  RtpTrueJitterBuffer(const RtpTrueJitterBuffer&) = delete;
  RtpTrueJitterBuffer& operator=(const RtpTrueJitterBuffer&) = delete;

  void reloadEnv();
  void setClockContext(RtpStreamClockContext *ctx) { m_clock = ctx; }

  RtpPlayoutMode mode() const { return m_mode; }
  bool isActive() const { return m_mode != RtpPlayoutMode::Off; }

  void clear();

  bool empty() const;
  std::size_t bufferedPackets() const { return m_bySeq.size(); }

  /**
   * @return 丢弃数（尾延迟名包 / 溢出清空）
   */
  int enqueue(RtpIngressPacket &&pkt, qint64 recvWallMs, const uint8_t *rtpHdr, std::size_t rtpLen);

  bool tryPopDue(qint64 nowWallMs, RtpIngressPacket &out);

  int millisUntilNextDue(qint64 nowWallMs) const;

  /** 洞超时清空后应由解码器打 PLI（单次边沿） */
  bool consumeHoleKeyframeRequest();

  void logMetricsIfDue(qint64 nowWallMs, const QString &streamTag);

 private:
  void checkHoleTimeout(qint64 nowWallMs);
  qint64 computeDeadlineWallMs(RtpPlayoutMode mode, qint64 recvWallMs, quint16 seq, quint32 rtp_ts,
                               quint32 ssrc, qint64 nowWallMs);
  void updateAdaptiveJitter(qint64 recvWallMs, quint32 rtp_ts);

  RtpStreamClockContext *m_clock = nullptr;

  RtpPlayoutMode m_mode = RtpPlayoutMode::Off;
  int m_fixedDelayMs = 0;
  int m_maxPackets = 1024;
  bool m_seqOrder = true;
  int m_holeTimeoutMs = 80;
  int m_srStaleMs = 800;
  double m_clockHz = 90000.0;
  int m_ntpMarginMs = 0;
  int m_adaptMinMs = 20;
  int m_adaptMaxMs = 180;
  double m_adaptGain = 1.5;
  int m_metricsIntervalMs = 0;

  struct Slot {
    RtpIngressPacket pkt = {};
    qint64 deadline_wall_ms = 0;

    Slot() = default;
    Slot(RtpIngressPacket &&p, qint64 d) : pkt(std::move(p)), deadline_wall_ms(d) {}
  };

  std::unordered_map<quint16, Slot> m_bySeq;
  std::deque<Slot> m_fifo;
  quint16 m_nextSeq = 0;
  bool m_nextInited = false;
  qint64 m_holeStartWallMs = 0;
  bool m_holeKeyframePending = false;

  bool m_loggedMode = false;
  qint64 m_lastMetricsLogMs = 0;

  // 自适应
  bool m_jitterPrev = false;
  qint64 m_prevRecvWallMs = 0;
  quint32 m_prevRtpTs = 0;
  double m_jitterEwmaMs = 0;
  int m_targetDelayMs = 40;

  std::uint64_t m_metrics_overflow_clears = 0;
  std::uint64_t m_metrics_late_drops = 0;
};
