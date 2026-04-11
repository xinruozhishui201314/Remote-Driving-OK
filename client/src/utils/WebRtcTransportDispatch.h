#pragma once

#include "../infrastructure/itransportmanager.h"

#include <QByteArray>
#include <QMap>

#include <functional>

/**
 * WebRTCChannel::send 的可测核心：不依赖真实 WebRtcClient，仅依赖回调与统计表。
 */
namespace WebRtcTransportDispatch {

/** tryPost 返回 true 表示已写入 DataChannel；false 时不更新 stats */
SendResult sendPayload(TransportChannel channel, bool hasPrimaryClient,
                       const std::function<bool(const QByteArray&)>& tryPostToDataChannel,
                       const uint8_t* data, size_t len,
                       QMap<TransportChannel, ChannelStats>* stats);

QString synthesizeWhepUrl(const QString& host, const QString& vin, const QString& sessionId);

TransportChannel videoChannelForCameraId(quint32 cameraId);

}  // namespace WebRtcTransportDispatch
