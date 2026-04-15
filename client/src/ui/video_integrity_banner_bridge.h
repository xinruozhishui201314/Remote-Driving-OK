#pragma once

#include <QObject>
#include <QString>

class VideoIntegrityBannerBridge : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY(VideoIntegrityBannerBridge)

 public:
  explicit VideoIntegrityBannerBridge(QObject *parent = nullptr);
  ~VideoIntegrityBannerBridge() override;

 signals:
  /** category=decode：解码/码流完整性（含多 slice 多线程条状风险等） */
  void decodeIntegrityBanner(const QString &stream, const QString &code, const QString &title,
                             const QString &body, bool mitigationApplied);
  /** category=present：纹理尺寸或 DECODE→SG 指纹不一致，疑 GPU/RHI/合成器 */
  void presentIntegrityBanner(const QString &stream, const QString &code, const QString &title,
                              const QString &body, bool suspectGpuCompositor);

 private:
  int m_decodeSub = 0;
  int m_presentSub = 0;
};
