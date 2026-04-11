#ifndef CPUVIDEORGBA8888FRAME_H
#define CPUVIDEORGBA8888FRAME_H

#include <QImage>

#include <optional>

/**
 * CPU 纹理呈现强类型：全链路约定 QImage::Format_RGBA8888 且 bytesPerLine >= width*4。
 * 与 DMA-BUF/NV12（applyDmaBufFrame）路径隔离，禁止与「任意 QImage」混用同一弱类型 API。
 */
struct CpuVideoRgba8888Frame {
  QImage image;

  /** 成功时消耗 img；失败时 img 仍为有效 QImage（未移动）。 */
  static std::optional<CpuVideoRgba8888Frame> tryAdopt(QImage &&img) {
    if (img.isNull())
      return std::nullopt;
    const int w = img.width();
    const int h = img.height();
    if (w <= 0 || h <= 0)
      return std::nullopt;
    if (img.format() != QImage::Format_RGBA8888)
      return std::nullopt;
    if (img.bytesPerLine() < w * 4)
      return std::nullopt;
    return CpuVideoRgba8888Frame{std::move(img)};
  }
};

#endif
