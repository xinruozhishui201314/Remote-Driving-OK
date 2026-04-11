#pragma once
#include "IHardwareDecoder.h"

/**
 * NVIDIA NVDEC 硬件解码器（通过 FFmpeg CUDA hwaccel）。
 *
 * 输出格式：NV12（VideoFrame::MemoryType::CPU_MEMORY + PixelFormat::NV12）
 * 解码在 GPU 上完成（NVDEC），解码后通过 av_hwframe_transfer_data 拷贝到
 * CPU NV12 内存，再经 CpuUploadInterop 异步上传 GL 纹理。
 *
 * 与 VAAPI 的对比：
 *   VAAPI (Intel/AMD) → DMA-BUF → EGL → GL（零拷贝，无 CPU 参与）
 *   NVDEC (NVIDIA)    → CUDA → CPU NV12 → GL（近零拷贝，硬件解码+PBO上传）
 *
 * 当 ENABLE_NVDEC 未定义时，编译为总是返回 false 的存根。
 */

#ifdef ENABLE_NVDEC

class NvdecDecoder : public IHardwareDecoder {
 public:
  NvdecDecoder();
  ~NvdecDecoder() override;

  bool initialize(const DecoderConfig& config) override;
  void shutdown() override;
  DecodeResult submitPacket(const uint8_t* data, size_t size, int64_t pts, int64_t dts) override;
  DecodeResult receiveFrame(VideoFrame& frame) override;
  void flush() override;
  DecoderCapabilities queryCapabilities() const override;
  bool isHardwareAccelerated() const override { return true; }

 private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};

#else

class NvdecDecoder : public IHardwareDecoder {
 public:
  bool initialize(const DecoderConfig&) override { return false; }
  void shutdown() override {}
  DecodeResult submitPacket(const uint8_t*, size_t, int64_t, int64_t) override {
    return DecodeResult::Error;
  }
  DecodeResult receiveFrame(VideoFrame&) override { return DecodeResult::Error; }
  void flush() override {}
  DecoderCapabilities queryCapabilities() const override {
    return {false, false, false, "NvdecDecoder(unavailable)"};
  }
  bool isHardwareAccelerated() const override { return false; }
};

#endif  // ENABLE_NVDEC
