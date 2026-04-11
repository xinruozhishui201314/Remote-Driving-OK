#pragma once

#include "RtpStreamClockContext.h"

#include <cstddef>
#include <cstdint>

class QString;

/**
 * 解析 Track::onMessage 中的 RTCP compound（RFC 3550），提取 Sender Report (PT=200)。
 * 成功时更新 ctx（线程安全原子）。
 *
 * @return true 若整段按 RTCP 消费（勿再当 RTP 入环）
 */
bool rtcpCompoundTryConsumeAndUpdateClock(const uint8_t *data, std::size_t len,
                                          RtpStreamClockContext *ctx, qint64 recv_wall_ms,
                                          quint32 expected_ssrc, QString *outLog);
