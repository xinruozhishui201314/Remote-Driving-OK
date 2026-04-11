#pragma once

#include <QHash>
#include <QMutex>
#include <QString>

#include <QtGlobal>

/**
 * 解码线程写入、Scene Graph 上传线程读取：按 (streamTag, frameId) 记录 DECODE_OUT 指纹，
 * 用于与 SG 路径上的 QImage 再采样对比（V2：区分解码损坏 vs GPU/纹理异常）。
 */
class VideoFrameFingerprintCache {
 public:
  static VideoFrameFingerprintCache &instance();

  struct Fingerprint {
    quint32 rowHash = 0;
    quint32 fullCrc = 0; /**< 0 = 未计算（未开 CLIENT_VIDEO_EVIDENCE_FULL_CRC） */
    int width = 0;
    int height = 0;
  };

  void record(const QString &streamTag, quint64 frameId, const Fingerprint &fp);

  /** 若存在则写入 *out 并返回 true */
  bool peek(const QString &streamTag, quint64 frameId, Fingerprint *out) const;

  void clearStream(const QString &streamTag);

 private:
  VideoFrameFingerprintCache() = default;

  QString makeKey(const QString &streamTag, quint64 frameId) const;
  void trimUnlocked();

  mutable QMutex m_mutex;
  QHash<QString, Fingerprint> m_map;
  static constexpr int kMaxEntries = 512;
};
