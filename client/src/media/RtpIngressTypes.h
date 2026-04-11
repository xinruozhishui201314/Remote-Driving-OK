#pragma once

#include <QByteArray>

#include <cstdint>

/** 单条 RTP 入站记录：载荷 + 端到端 lifecycleId（诊断） */
struct RtpIngressPacket {
  QByteArray bytes;
  std::uint64_t lifecycleId = 0;
};
