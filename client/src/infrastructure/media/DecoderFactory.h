#pragma once
#include "IHardwareDecoder.h"

#include <memory>

/**
 * 解码器工厂（《客户端架构设计》§3.1.2）。
 * 优先级：VAAPI(Linux) > V4L2M2M(嵌入式) > NVDEC > FFmpeg软解。
 */
enum class DecoderPreference { HardwareFirst, SoftwareOnly };

class DecoderFactory {
 public:
  static std::unique_ptr<IHardwareDecoder> create(
      const QString& codec = "H264", DecoderPreference pref = DecoderPreference::HardwareFirst);

  /** 带完整 DecoderConfig（含 extradata / preferDmaBufExport），供 WebRTC 硬解等场景 */
  static std::unique_ptr<IHardwareDecoder> create(
      const DecoderConfig& config, DecoderPreference pref = DecoderPreference::HardwareFirst);

  // 查询可用解码器（用于日志/诊断）
  static QStringList availableDecoders();

  // 运行时硬件检测
  static bool isVaapiAvailable();
  static bool isNvdecAvailable();
  static bool isHardwareDecodeAvailable(const QString& type);
};
