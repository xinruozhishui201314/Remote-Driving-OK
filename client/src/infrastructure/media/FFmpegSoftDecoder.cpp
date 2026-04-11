#include "FFmpegSoftDecoder.h"

#include <QDebug>

#ifdef ENABLE_FFMPEG

FFmpegSoftDecoder::FFmpegSoftDecoder() = default;

FFmpegSoftDecoder::~FFmpegSoftDecoder() { shutdown(); }

bool FFmpegSoftDecoder::initialize(const DecoderConfig& config) {
  const AVCodecID codecId =
      (config.codec == "H265" || config.codec == "HEVC") ? AV_CODEC_ID_HEVC : AV_CODEC_ID_H264;

  m_codec = avcodec_find_decoder(codecId);
  if (!m_codec) {
    qWarning() << "[Client][FFmpegSoftDecoder] codec not found:" << config.codec;
    return false;
  }

  m_ctx = avcodec_alloc_context3(m_codec);
  if (!m_ctx)
    return false;

  m_ctx->width = config.width;
  m_ctx->height = config.height;
  
  // 修复：预加载 extradata (SPS/PPS) 确保解码器初始化完整
  if (!config.codecExtradata.isEmpty()) {
    m_ctx->extradata = static_cast<uint8_t*>(av_malloc(config.codecExtradata.size() + AV_INPUT_BUFFER_PADDING_SIZE));
    if (m_ctx->extradata) {
      memcpy(m_ctx->extradata, config.codecExtradata.constData(), config.codecExtradata.size());
      memset(m_ctx->extradata + config.codecExtradata.size(), 0, AV_INPUT_BUFFER_PADDING_SIZE);
      m_ctx->extradata_size = config.codecExtradata.size();
      qDebug() << "[Client][FFmpegSoftDecoder] preloaded extradata size=" << config.codecExtradata.size();
    }
  }
  
  // Low-latency flags
  m_ctx->flags |= AV_CODEC_FLAG_LOW_DELAY;
  m_ctx->flags2 |= AV_CODEC_FLAG2_FAST;
  
  // 修复：配置线程以平衡 latency 和 throughput
  m_ctx->thread_count = 2;
  m_ctx->thread_type = FF_THREAD_FRAME;

  if (avcodec_open2(m_ctx, m_codec, nullptr) < 0) {
    qWarning() << "[Client][FFmpegSoftDecoder] avcodec_open2 failed";
    if (m_ctx->extradata) {
      av_free(m_ctx->extradata);
      m_ctx->extradata = nullptr;
    }
    avcodec_free_context(&m_ctx);
    return false;
  }

  m_packet = av_packet_alloc();
  m_frame = av_frame_alloc();
  m_initialized = true;

  qInfo() << "[Client][FFmpegSoftDecoder] initialized codec=" << config.codec << config.width << "x"
          << config.height << " extradata_size=" << m_ctx->extradata_size;
  return true;
}

void FFmpegSoftDecoder::shutdown() {
  if (m_frame) {
    av_frame_free(&m_frame);
    m_frame = nullptr;
  }
  if (m_packet) {
    av_packet_free(&m_packet);
    m_packet = nullptr;
  }
  if (m_ctx) {
    // 修复：清理 extradata 防止内存泄漏
    if (m_ctx->extradata) {
      av_free(m_ctx->extradata);
      m_ctx->extradata = nullptr;
    }
    avcodec_free_context(&m_ctx);
    m_ctx = nullptr;
  }
  m_initialized = false;
}

DecodeResult FFmpegSoftDecoder::submitPacket(const uint8_t* data, size_t size, int64_t pts,
                                             int64_t /*dts*/) {
  if (!m_initialized)
    return DecodeResult::Error;

  m_packet->data = const_cast<uint8_t*>(data);
  m_packet->size = static_cast<int>(size);
  m_packet->pts = pts;

  const int ret = avcodec_send_packet(m_ctx, m_packet);
  if (ret == AVERROR(EAGAIN))
    return DecodeResult::NeedMore;
  if (ret < 0) {
    qWarning() << "[Client][FFmpegSoftDecoder] send_packet error:" << ret;
    return DecodeResult::Error;
  }
  return DecodeResult::Ok;
}

DecodeResult FFmpegSoftDecoder::receiveFrame(VideoFrame& frame) {
  if (!m_initialized)
    return DecodeResult::Error;

  const int ret = avcodec_receive_frame(m_ctx, m_frame);
  if (ret == AVERROR(EAGAIN))
    return DecodeResult::NeedMore;
  if (ret == AVERROR_EOF)
    return DecodeResult::EOF_Stream;
  if (ret < 0)
    return DecodeResult::Error;

  frame.memoryType = VideoFrame::MemoryType::CPU_MEMORY;
  frame.width = static_cast<uint32_t>(m_frame->width);
  frame.height = static_cast<uint32_t>(m_frame->height);
  frame.pts = m_frame->pts;
  frame.interlacedMetadata = (m_frame->flags & (1 << 5)) != 0;
  frame.topFieldFirst = (m_frame->flags & (1 << 6)) != 0;

  // Y plane
  frame.planes[0].data = m_frame->data[0];
  frame.planes[0].stride = static_cast<uint32_t>(m_frame->linesize[0]);
  frame.planes[0].size = static_cast<uint32_t>(m_frame->linesize[0] * m_frame->height);

  // U plane
  frame.planes[1].data = m_frame->data[1];
  frame.planes[1].stride = static_cast<uint32_t>(m_frame->linesize[1]);
  frame.planes[1].size = static_cast<uint32_t>(m_frame->linesize[1] * m_frame->height / 2);

  // V plane
  frame.planes[2].data = m_frame->data[2];
  frame.planes[2].stride = static_cast<uint32_t>(m_frame->linesize[2]);
  frame.planes[2].size = static_cast<uint32_t>(m_frame->linesize[2] * m_frame->height / 2);

  return DecodeResult::Ok;
}

void FFmpegSoftDecoder::flush() {
  if (m_ctx)
    avcodec_flush_buffers(m_ctx);
}

DecoderCapabilities FFmpegSoftDecoder::queryCapabilities() const {
  DecoderCapabilities caps;
  caps.supportsH264 = true;
  caps.supportsH265 = true;
  caps.supportsHW = false;
  caps.name = "FFmpegSoftDecoder";
  return caps;
}

#endif  // ENABLE_FFMPEG
