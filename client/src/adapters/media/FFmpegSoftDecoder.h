#pragma once
#include <infrastructure/media/IHardwareDecoder.h>

#ifdef ENABLE_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
}

/**
 * FFmpeg 软件解码器（IHardwareDecoder 实现）。
 * 封装现有 H264Decoder 逻辑，符合 IHardwareDecoder 接口。
 */
class FFmpegSoftDecoder : public IHardwareDecoder {
 public:
  FFmpegSoftDecoder();
  ~FFmpegSoftDecoder() override;

  bool initialize(const DecoderConfig& config) override;
  void shutdown() override;
  bool reconfigure(const DecoderConfig& config) override;
  DecodeResult submitPacket(const uint8_t* data, size_t size, int64_t pts, int64_t dts) override;
  DecodeResult receiveFrame(VideoFrame& frame) override;
  void flush() override;
  DecoderCapabilities queryCapabilities() const override;
  bool isHardwareAccelerated() const override { return false; }

 private:
  const AVCodec* m_codec = nullptr;
  AVCodecContext* m_ctx = nullptr;
  AVPacket* m_packet = nullptr;
  AVFrame* m_frame = nullptr;
  bool m_initialized = false;
};

#else  // !ENABLE_FFMPEG

// Stub when FFmpeg is not available
class FFmpegSoftDecoder : public IHardwareDecoder {
 public:
  bool initialize(const DecoderConfig&) override { return false; }
  void shutdown() override {}
  bool reconfigure(const DecoderConfig&) override { return false; }
  DecodeResult submitPacket(const uint8_t*, size_t, int64_t, int64_t) override {
    return DecodeResult::Error;
  }
  DecodeResult receiveFrame(VideoFrame&) override { return DecodeResult::Error; }
  void flush() override {}
  DecoderCapabilities queryCapabilities() const override { return {}; }
  bool isHardwareAccelerated() const override { return false; }
};

#endif  // ENABLE_FFMPEG
