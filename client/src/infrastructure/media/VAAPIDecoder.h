#pragma once
#include "IHardwareDecoder.h"

/**
 * Linux VA-API 硬件解码器（《客户端架构设计》§3.1.2）。
 * 当 ENABLE_VAAPI 编译宏存在时提供真实实现，否则为存根。
 */
#ifdef ENABLE_VAAPI

class VAAPIDecoder : public IHardwareDecoder {
 public:
  VAAPIDecoder();
  ~VAAPIDecoder() override;

  bool initialize(const DecoderConfig& config) override;
  void shutdown() override;
  DecodeResult submitPacket(const uint8_t* data, size_t size, int64_t pts, int64_t dts) override;
  DecodeResult receiveFrame(VideoFrame& frame) override;
  void flush() override;
  DecoderCapabilities queryCapabilities() const override;
  bool isHardwareAccelerated() const override { return true; }
  void setDmaBufExportPreferred(bool enable) override;

 private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};

#else

class VAAPIDecoder : public IHardwareDecoder {
 public:
  bool initialize(const DecoderConfig&) override { return false; }
  void shutdown() override {}
  DecodeResult submitPacket(const uint8_t*, size_t, int64_t, int64_t) override {
    return DecodeResult::Error;
  }
  DecodeResult receiveFrame(VideoFrame&) override { return DecodeResult::Error; }
  void flush() override {}
  DecoderCapabilities queryCapabilities() const override {
    return {false, false, false, "VAAPIDecoder(unavailable)"};
  }
  bool isHardwareAccelerated() const override { return false; }
};

#endif  // ENABLE_VAAPI
