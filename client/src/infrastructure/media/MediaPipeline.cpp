#include "MediaPipeline.h"

#include "../../utils/TimeUtils.h"
#include "DecoderFactory.h"

#include <QDebug>
#include <QThread>

MediaPipeline::MediaPipeline(QObject* parent) : QObject(parent) {}

MediaPipeline::~MediaPipeline() { shutdown(); }

bool MediaPipeline::initialize(const PipelineConfig& config) {
  m_config = config;

  // 创建帧池（预分配）
  m_framePool = std::make_shared<FramePool>(config.framePoolSize, config.maxWidth, config.maxHeight,
                                            config.gpuMemoryType);

  // 创建解码器
  m_decoder = DecoderFactory::create(config.codec);
  if (!m_decoder) {
    emit pipelineError(QString("Failed to create decoder for %1").arg(config.codec));
    return false;
  }

  DecoderConfig dcfg;
  dcfg.codec = config.codec;
  dcfg.width = static_cast<int>(config.maxWidth);
  dcfg.height = static_cast<int>(config.maxHeight);
  if (!m_decoder->initialize(dcfg)) {
    emit pipelineError("Decoder initialization failed");
    return false;
  }

  // 启动解码线程（高优先级）
  m_running = true;
  m_decodeThread = QThread::create([this]() { decodeLoop(); });
  m_decodeThread->setObjectName(QString("DecodeThread-cam%1").arg(config.cameraId));
  m_decodeThread->start(QThread::HighPriority);

  qInfo() << "[Client][MediaPipeline] initialized"
          << "cam=" << config.cameraId << "codec=" << config.codec
          << "hw=" << (m_decoder->isHardwareAccelerated() ? "yes" : "no")
          << "pool=" << config.framePoolSize;
  return true;
}

void MediaPipeline::shutdown() {
  m_running = false;
  if (m_decodeThread) {
    m_decodeThread->wait(3000);
    delete m_decodeThread;
    m_decodeThread = nullptr;
  }
  if (m_decoder) {
    m_decoder->shutdown();
    m_decoder.reset();
  }
  qInfo() << "[Client][MediaPipeline] shutdown cam=" << m_config.cameraId;
}

void MediaPipeline::onVideoPacketReceived(const uint8_t* data, size_t size, int64_t pts) {
  NALUnit nalu;
  nalu.data = QByteArray(reinterpret_cast<const char*>(data), static_cast<int>(size));
  nalu.pts = pts;
  if (!m_decodeQueue.push(std::move(nalu))) {
    {
      QMutexLocker lock(&m_statsMutex);
      m_stats.framesDropped++;
    }
    qWarning() << "[Client][MediaPipeline] decode queue full, dropping frame cam="
               << m_config.cameraId;
  }
}

void MediaPipeline::onVideoPacketReceived(const QByteArray& data, int64_t pts) {
  NALUnit nalu;
  nalu.data = data;
  nalu.pts = pts;
  if (!m_decodeQueue.push(std::move(nalu))) {
    {
      QMutexLocker lock(&m_statsMutex);
      m_stats.framesDropped++;
    }
    qWarning() << "[Client][MediaPipeline] decode queue full, dropping frame cam="
               << m_config.cameraId;
  }
}

void MediaPipeline::reconfigure(const PipelineConfig& config) {
  shutdown();
  initialize(config);
}

void MediaPipeline::setFramePoolCapacity(int capacity) {
  if (capacity <= 0) {
    qWarning() << "[Client][MediaPipeline] Invalid frame pool capacity:" << capacity;
    return;
  }

  if (m_framePool) {
    const int currentSize = static_cast<int>(m_framePool->size());
    if (currentSize == capacity) {
      return;
    }
    qInfo() << "[Client][MediaPipeline] Resizing frame pool from" << currentSize << "to" << capacity
            << "cam=" << m_config.cameraId;
  }

  // 重建帧池以新容量
  m_framePool = std::make_shared<FramePool>(static_cast<size_t>(capacity), m_config.maxWidth,
                                            m_config.maxHeight, m_config.gpuMemoryType);

  {
    QMutexLocker lock(&m_statsMutex);
    m_config.framePoolSize = static_cast<size_t>(capacity);
  }
}

void MediaPipeline::decodeLoop() {
  NALUnit nalu;
  bool continueDecodedFrames = false;
  while (m_running) {
    if (!m_decodeQueue.pop(nalu)) {
      if (continueDecodedFrames) {
        // flush后继续处理已解码的帧
      } else {
        QThread::usleep(100);  // 100µs 空转等待
        continue;
      }
    }

    const int64_t decodeStart = TimeUtils::nowUs();

    // 提交到解码器
    auto res = m_decoder->submitPacket(reinterpret_cast<const uint8_t*>(nalu.data.constData()),
                                       static_cast<size_t>(nalu.data.size()), nalu.pts, nalu.pts);

    if (res == DecodeResult::Error) {
      {
        QMutexLocker lock(&m_statsMutex);
        m_stats.decodeErrors++;
      }
      qWarning() << "[Client][MediaPipeline] decode error cam=" << m_config.cameraId;
      m_decoder->flush();
      // flush 后继续处理解码器内部残留的帧，不丢失
      continueDecodedFrames = true;
    }

    // 取出解码帧
    VideoFrame rawFrame;
    while (m_decoder->receiveFrame(rawFrame) == DecodeResult::Ok) {
      continueDecodedFrames = false;  // 正常解码帧后重置标志
      auto poolFrame = m_framePool->acquire();
      if (!poolFrame) {
        {
          QMutexLocker lock(&m_statsMutex);
          m_stats.framesDropped++;
        }
        continue;
      }
      // 复制帧元数据（CPU路径下数据已在 decoder 内部，通过指针传递）
      poolFrame->width = rawFrame.width;
      poolFrame->height = rawFrame.height;
      poolFrame->pts = rawFrame.pts;
      poolFrame->captureTimestamp =
          rawFrame.captureTimestamp > 0 ? rawFrame.captureTimestamp : TimeUtils::wallClockMs();
      poolFrame->cameraId = m_config.cameraId;
      poolFrame->memoryType = rawFrame.memoryType;
      poolFrame->gpuHandle = rawFrame.gpuHandle;
      poolFrame->pixelFormat = rawFrame.pixelFormat;
      poolFrame->interlacedMetadata = rawFrame.interlacedMetadata;
      poolFrame->topFieldFirst = rawFrame.topFieldFirst;

      // CPU 内存：拷贝平面数据到池帧（GPU路径则零拷贝）
      if (rawFrame.memoryType == VideoFrame::MemoryType::CPU_MEMORY) {
        for (int p = 0; p < 3; ++p) {
          if (rawFrame.planes[p].data && poolFrame->planes[p].data) {
            const size_t sz = std::min(static_cast<size_t>(rawFrame.planes[p].size),
                                       static_cast<size_t>(poolFrame->planes[p].size));
            std::memcpy(poolFrame->planes[p].data, rawFrame.planes[p].data, sz);
            poolFrame->planes[p].stride = rawFrame.planes[p].stride;
          }
        }
      }

      {
        QMutexLocker lock(&m_statsMutex);
        m_stats.framesDecoded++;
      }
      updateStats(TimeUtils::nowUs() - decodeStart);

      // 发送到渲染线程（Qt::QueuedConnection 或 DirectConnection 由接收者决定）
      emit frameReady(poolFrame);
    }
  }
}

void MediaPipeline::updateStats(int64_t decodeUs) {
  QMutexLocker lock(&m_statsMutex);
  const double decodeMs = decodeUs / 1000.0;
  m_stats.avgDecodeTimeMs = m_stats.avgDecodeTimeMs * 0.95 + decodeMs * 0.05;
}
