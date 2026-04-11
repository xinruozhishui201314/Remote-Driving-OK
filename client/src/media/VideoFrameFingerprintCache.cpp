#include "VideoFrameFingerprintCache.h"

QString VideoFrameFingerprintCache::makeKey(const QString &streamTag, quint64 frameId) const {
  return streamTag + QLatin1Char(':') + QString::number(frameId);
}

VideoFrameFingerprintCache &VideoFrameFingerprintCache::instance() {
  static VideoFrameFingerprintCache s;
  return s;
}

void VideoFrameFingerprintCache::record(const QString &streamTag, quint64 frameId,
                                        const Fingerprint &fp) {
  if (streamTag.isEmpty())
    return;
  QMutexLocker lock(&m_mutex);
  m_map.insert(makeKey(streamTag, frameId), fp);
  trimUnlocked();
}

bool VideoFrameFingerprintCache::peek(const QString &streamTag, quint64 frameId,
                                      Fingerprint *out) const {
  if (!out || streamTag.isEmpty())
    return false;
  QMutexLocker lock(&m_mutex);
  const auto it = m_map.constFind(makeKey(streamTag, frameId));
  if (it == m_map.cend())
    return false;
  *out = it.value();
  return true;
}

void VideoFrameFingerprintCache::clearStream(const QString &streamTag) {
  if (streamTag.isEmpty())
    return;
  QMutexLocker lock(&m_mutex);
  for (auto it = m_map.begin(); it != m_map.end();) {
    if (it.key().startsWith(streamTag + QLatin1Char(':')))
      it = m_map.erase(it);
    else
      ++it;
  }
}

void VideoFrameFingerprintCache::trimUnlocked() {
  while (m_map.size() > kMaxEntries) {
    auto it = m_map.begin();
    m_map.erase(it);
  }
}
