// h264decoder.cpp
#include "h264decoder.h"
#include <QDebug>
#include <QDateTime>
#include <algorithm>
#include <cstring>
#include <QElapsedTimer>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/error.h>
#include <libavutil/imgutils.h>
#include <libavutil/log.h>
#include <libavutil/mem.h>
#include <libavutil/pixdesc.h>
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
}

namespace {
static void quiet_av_log_callback(void *avcl, int level, const char *fmt, va_list vl)
{
    if (fmt && strstr(fmt, "concealing")) return;
    va_list vl2;
    va_copy(vl2, vl);
    av_log_default_callback(avcl, level, fmt, vl2);
    va_end(vl2);
}
}

static quint16 rtpSeqNum(const uint8_t *d) { return (d[2] << 8) | d[3]; }
static quint32 rtpTimestamp(const uint8_t *d) { return (d[4]<<24)|(d[5]<<16)|(d[6]<<8)|d[7]; }
static bool    rtpMarkerBit(const uint8_t *d) { return (d[1] & 0x80) != 0; }
static constexpr uint8_t kFuAStart = 0x80;
static constexpr uint8_t kFuAEnd   = 0x40;

H264Decoder::H264Decoder(const QString &streamTag, QObject *parent) : QObject(parent), m_streamTag(streamTag)
{
    static bool s_cb_set = false;
    if (!s_cb_set) {
        av_log_set_callback(quiet_av_log_callback);
        s_cb_set = true;
    }
}

H264Decoder::~H264Decoder()
{
    reset();
    if (m_sws) { sws_freeContext(m_sws); m_sws = nullptr; }
}

void H264Decoder::closeDecoder()
{
    if (m_ctx) {
        qDebug() << "[H264] closeDecoder: 正在关闭解码器 m_codecOpen=" << m_codecOpen;
        avcodec_free_context(&m_ctx);
        m_ctx = nullptr;
    } else {
        qDebug() << "[H264] closeDecoder: m_ctx 已是 nullptr，无操作";
    }
    m_codecOpen = false;
}

bool H264Decoder::ensureDecoder()
{
    if (!m_codec) {
        m_codec = avcodec_find_decoder(AV_CODEC_ID_H264);
        if (!m_codec) {
            qCritical() << "[H264] ensureDecoder: 未找到 H264 解码器!";
            return false;
        }
        qDebug() << "[H264] ensureDecoder: 找到 H264 解码器";
    }
    if (!m_ctx) {
        m_ctx = avcodec_alloc_context3(m_codec);
        if (!m_ctx) {
            qCritical() << "[H264] ensureDecoder: avcodec_alloc_context3 失败!";
            return false;
        }
        m_ctx->thread_count = 1;
        qDebug() << "[H264] ensureDecoder: 已分配解码器上下文 (thread_count=1)";
    }
    if (m_codecOpen) return true;
    if (m_sps.isEmpty() || m_pps.isEmpty()) {
        qDebug() << "[H264] ensureDecoder: SPS 或 PPS 未就绪 sps.empty=" << m_sps.isEmpty()
                 << " pps.empty=" << m_pps.isEmpty();
        return false;
    }
    bool ok = openDecoderWithExtradata();
    if (!ok) {
        qCritical() << "[H264] ensureDecoder: openDecoderWithExtradata 失败!";
    }
    return ok;
}

bool H264Decoder::openDecoderWithExtradata()
{
    if (m_codecOpen || !m_ctx || m_sps.isEmpty() || m_pps.isEmpty())
        return false;

    const int spsLen = m_sps.size();
    const int ppsLen = m_pps.size();
    if (spsLen < 4 || ppsLen < 1) return false;

    const int extSize = 11 + spsLen + ppsLen;
    uint8_t *ext = static_cast<uint8_t*>(av_malloc(extSize + AV_INPUT_BUFFER_PADDING_SIZE));
    if (!ext) return false;

    uint8_t *p = ext;
    *p++ = 1;
    *p++ = static_cast<uint8_t>(m_sps[1]);
    *p++ = static_cast<uint8_t>(m_sps[2]);
    *p++ = static_cast<uint8_t>(m_sps[3]);
    *p++ = 0xFF;
    *p++ = 0xE1;
    *p++ = (spsLen >> 8) & 0xFF; *p++ = spsLen & 0xFF;
    memcpy(p, m_sps.constData(), spsLen); p += spsLen;
    *p++ = 1;
    *p++ = (ppsLen >> 8) & 0xFF; *p++ = ppsLen & 0xFF;
    memcpy(p, m_pps.constData(), ppsLen);
    memset(ext + extSize, 0, AV_INPUT_BUFFER_PADDING_SIZE);

    m_ctx->extradata = ext;
    m_ctx->extradata_size = extSize;

    if (avcodec_open2(m_ctx, m_codec, nullptr) < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(AVERROR_UNKNOWN, errbuf, sizeof(errbuf));
        qCritical() << "[H264] avcodec_open2 失败! err=" << errbuf
                     << " spsLen=" << spsLen << " ppsLen=" << ppsLen;
        av_freep(&m_ctx->extradata);
        m_ctx->extradata_size = 0;
        return false;
    }
    m_codecOpen = true;
    qDebug() << "[H264] openDecoderWithExtradata ok sps=" << spsLen << "pps=" << ppsLen;
    return true;
}

void H264Decoder::flushDecoder()
{
    if (!m_ctx || !m_codecOpen) {
        qDebug() << "[H264] flushDecoder: 无需刷新 m_ctx=" << (void*)m_ctx << " m_codecOpen=" << m_codecOpen;
        return;
    }
    qDebug() << "[H264] flushDecoder: 开始刷新解码器";
    avcodec_send_packet(m_ctx, nullptr);
    AVFrame *f = av_frame_alloc();
    if (f) {
        int drained = 0;
        while (avcodec_receive_frame(m_ctx, f) == 0) {
            drained++;
        }
        qDebug() << "[H264] flushDecoder: 已排出" << drained << "帧残留";
        av_frame_free(&f);
    } else {
        qWarning() << "[H264] flushDecoder: av_frame_alloc 失败";
    }
    avcodec_flush_buffers(m_ctx);

    // 重置 SPS/PPS 和关键帧标志
    m_sps.clear();
    m_pps.clear();
    m_haveDecodedKeyframe = false;
    m_needKeyframe = true;
    qDebug() << "[H264] flushDecoder: 完成，已清除 SPS/PPS/m_haveDecodedKeyframe";
}

void H264Decoder::reset()
{
    qDebug() << "[H264] reset: 开始重置"
             << " framesEmitted=" << m_framesEmitted
             << " droppedPFrames=" << m_droppedPFrameCount
             << " codecOpen=" << m_codecOpen
             << " bufSize=" << m_rtpBuffer.size();
    closeDecoder();
    m_codec = nullptr;
    if (m_sws) { sws_freeContext(m_sws); m_sws = nullptr; }
    m_width = m_height = 0;
    m_fuBuffer.clear();
    m_fuStarted = false;
    m_sps.clear();
    m_pps.clear();
    m_haveDecodedKeyframe = false;
    m_needKeyframe = true;   // 重置后需要等待 IDR
    m_framesSinceKeyframeRequest = 0;
    m_expectedSliceCount = 0;
    m_pendingFrame = PendingFrame();
    m_rtpBuffer.clear();
    m_rtpSeqInitialized = false;
    m_rtpNextExpectedSeq = 0;
    m_framesEmitted = 0;
    m_droppedPFrameCount = 0;
    m_rtpPacketsProcessed = 0;
    m_lastRtpSeq = 0;
    m_logSendCount = 0;
    // ── 诊断窗口重置 ─────────────────────────────────────────────────────────
    m_statsWindowStart = 0;
    m_statsFramesInWindow = 0;
    m_statsPacketsInWindow = 0;
    m_statsDroppedInWindow = 0;
    m_lastStatSeqNum = 0;
    m_frameBuffer = QImage();  // 释放帧缓冲（维度可能变化）
    qDebug() << "[H264] reset: 完成";
}

bool H264Decoder::handleParameterSet(const uint8_t *nal, size_t nalLen)
{
    if (nalLen < 1) {
        qWarning() << "[H264] handleParameterSet: nalLen=0，忽略";
        return false;
    }
    int nalType = nal[0] & 0x1f;

    if (nalType == 7) {
        QByteArray newSps(reinterpret_cast<const char*>(nal), static_cast<int>(nalLen));
        if (newSps != m_sps) {
            // ── 诊断：SPS 变化时打印完整 hex（前 8 字节）+ 与 ZLM 侧抓包对比 ─────
            QString spsHex;
            for (int i = 0; i < qMin(nalLen, static_cast<size_t>(8)); ++i)
                spsHex += QString::asprintf("%02X ", static_cast<unsigned char>(nal[i]));
            qInfo() << "[H264][SPS] SPS 变化 len=" << nalLen << " oldSize=" << m_sps.size()
                     << " hex(前8)=" << spsHex << "... 【与 ZLM 侧抓包对比确认参数集是否一致】";
            m_sps = newSps;
            closeDecoder();
            m_haveDecodedKeyframe = false;
            m_needKeyframe = true;
            m_framesSinceKeyframeRequest = 0;
        }
        return true;
    }
    if (nalType == 8) {
        QByteArray newPps(reinterpret_cast<const char*>(nal), static_cast<int>(nalLen));
        if (newPps != m_pps) {
            QString ppsHex;
            for (int i = 0; i < qMin(nalLen, static_cast<size_t>(8)); ++i)
                ppsHex += QString::asprintf("%02X ", static_cast<unsigned char>(nal[i]));
            qInfo() << "[H264][PPS] PPS 变化 len=" << nalLen << " oldSize=" << m_pps.size()
                     << " hex(前8)=" << ppsHex;
            m_pps = newPps;
            closeDecoder();
            m_haveDecodedKeyframe = false;
            m_needKeyframe = true;
            m_framesSinceKeyframeRequest = 0;
        }
        return true;
    }
    return false;
}

// ============================================================================
// RTP
// ============================================================================
void H264Decoder::feedRtp(const uint8_t *data, size_t len)
{
    // ── ★★★ 端到端追踪：feedRtp 进入（主线程） ★★★ ────────────────────────
    const int64_t feedRtpEnterTime = QDateTime::currentMSecsSinceEpoch();
    m_lastFeedRtpTime = feedRtpEnterTime;

    if (len <= kRtpHeaderMinLen) {
        qWarning() << "[H264][feedRtp] RTP 包太短，忽略 len=" << len;
        return;
    }

    quint16 seq = rtpSeqNum(data);
    quint32 ts  = rtpTimestamp(data);
    bool    marker = rtpMarkerBit(data);

    try {
        // ── 诊断：每秒帧统计（feedRtp 是最高频入口，每包都检查一次）────────────
        const int64_t now = feedRtpEnterTime;
        if (m_statsWindowStart == 0) m_statsWindowStart = now;
        ++m_statsPacketsInWindow;
        if (now - m_statsWindowStart >= kStatsIntervalMs) {
            // 窗口结束，打印统计
            int totalPackets = m_rtpPacketsProcessed - m_lastStatSeqNum;
            int lostPackets = totalPackets - m_statsPacketsInWindow;
            double lossRate = (totalPackets > 0) ? (100.0 * lostPackets / totalPackets) : 0.0;
            int droppedInWindow = m_droppedPFrameCount - m_statsDroppedInWindow;
            double fps = static_cast<double>(m_statsFramesInWindow) * 1000.0 / (now - m_statsWindowStart);
            // ── ★★★ 端到端追踪：每秒 Stats 统计 ★★★ ─────────────────────────
            qInfo() << "[H264][Stats] ★★★ 帧统计(每秒) stream=" << m_streamTag << " ★★★"
                     << " fps=" << fps
                     << " emitted=" << m_statsFramesInWindow
                     << " droppedBeforeIdr=" << droppedInWindow
                     << " rtpPackets=" << m_statsPacketsInWindow
                     << " lostPackets=" << lostPackets
                     << " lossRate=" << lossRate << "%"
                     << " duration=" << (now - m_statsWindowStart) << "ms"
                     << " bufSize=" << m_rtpBuffer.size()
                     << " needKeyframe=" << m_needKeyframe
                     << " codecOpen=" << m_codecOpen
                     << " totalProcessed=" << m_rtpPacketsProcessed;
            // 重置窗口
            m_statsWindowStart = now;
            m_statsFramesInWindow = 0;
            m_statsPacketsInWindow = 0;
            m_statsDroppedInWindow = m_droppedPFrameCount;
            m_lastStatSeqNum = m_rtpPacketsProcessed;
        }

        if (!m_rtpSeqInitialized) {
            m_rtpNextExpectedSeq = seq;
            m_rtpSeqInitialized = true;
            qInfo() << "[H264][feedRtp] ★★★ RTP 序列初始化 ★★★ stream=" << m_streamTag << " firstSeq=" << seq
                     << " ts=" << ts << " marker=" << marker;
        }

        if (seq == m_rtpNextExpectedSeq) {
            // ★ 关键修复：先处理当前包，再排空缓冲
            // 旧逻辑错误：先 drainRtpBuffer()，导致缓冲包比当前包晚一帧处理
            m_rtpNextExpectedSeq = static_cast<quint16>(seq + 1);
            // ── ★★★ 端到端追踪：RTP 包 seq 进入解码器 ★★★ ───────────────────────
            static QHash<QString, int> s_pktCountPerStream;
            const QString tag = m_streamTag.isEmpty() ? "unknown" : m_streamTag;
            ++s_pktCountPerStream[tag];
            if (s_pktCountPerStream[tag] <= 20) {
                qInfo() << "[H264][feedRtp] ★ RTP包seq=" << seq
                         << " → 解码器 stream=" << tag
                         << " pktCount=" << s_pktCountPerStream[tag]
                         << " ts=" << ts
                         << " marker=" << marker
                         << " payloadLen=" << (len - kRtpHeaderMinLen);
            }
            processRtpPacket(data, len);
            drainRtpBuffer();  // 排空已收到的连续包
            return;
        }

        int16_t diff = static_cast<int16_t>(seq - m_rtpNextExpectedSeq);
        if (diff > 0 && diff < 256) {
            // 乱序但可恢复：缓冲起来
            if (m_rtpBuffer.size() >= kRtpReorderBufferMax) {
                qWarning() << "[H264][feedRtp] 缓冲已满，丢弃最旧的包来接收 seq=" << seq
                           << " bufSize=" << m_rtpBuffer.size() << " max=" << kRtpReorderBufferMax;
                // 丢弃最早的一个
                QList<quint16> keys = m_rtpBuffer.keys();
                if (!keys.isEmpty()) {
                    std::sort(keys.begin(), keys.end());
                    m_rtpBuffer.remove(keys.first());
                }
            }
            m_rtpBuffer[seq] = QByteArray(reinterpret_cast<const char*>(data),
                                              static_cast<int>(len));
            // ── 诊断：乱序包进入缓冲 ─────────────────────────────────────────────
            static QHash<QString, int> s_outOfOrderCount;
            const QString tag2 = m_streamTag.isEmpty() ? "unknown" : m_streamTag;
            int ooCount = ++s_outOfOrderCount[tag2];
            if (ooCount <= 10) {
                qInfo() << "[H264][feedRtp] 乱序包缓冲 stream=" << tag2
                         << " seq=" << seq << " expected=" << m_rtpNextExpectedSeq << " diff=" << diff
                         << " bufSize=" << m_rtpBuffer.size() << " ooCount=" << ooCount;
            }
            return;
        }
        if (diff < 0 && diff > -256) {
            // 重复包，忽略
            return;
        }
        // 大幅跳跃（重连/乱序）：以新序列为基准，清空旧缓冲
        // ── 诊断：打印跳跃时间戳，便于与 ZLM 侧日志对比是哪一刻断的 ───────────
        qWarning() << "[H264][RTP][Diag] RTP 序列大幅跳跃: oldExpected=" << m_rtpNextExpectedSeq
                   << " newSeq=" << seq << " diff=" << diff
                   << " bufSize=" << m_rtpBuffer.size()
                   << " now=" << now << "ms【与 ZLM 日志对比：确认是 ZLM 停了还是网络丢了】";
        m_rtpBuffer.clear();
        m_rtpNextExpectedSeq = static_cast<quint16>(seq + 1);
        processRtpPacket(data, len);
        m_needKeyframe = true;
        m_framesSinceKeyframeRequest = 0;
    } catch (const std::exception& e) {
        qCritical() << "[H264][feedRtp] EXCEPTION: rtpSeq=" << seq
                    << "len=" << len << ":" << e.what();
    } catch (...) {
        qCritical() << "[H264][feedRtp] UNKNOWN EXCEPTION: rtpSeq=" << seq << "len=" << len;
    }
}

void H264Decoder::drainRtpBuffer()
{
    // 连续排空
    int drained = 0;
    int loopCount = 0;
    while (m_rtpBuffer.contains(m_rtpNextExpectedSeq)) {
        loopCount++;
        if (loopCount > 500) {
            qWarning() << "[H264][drain] 循环次数过多，强制退出 drained=" << drained
                       << " bufSize=" << m_rtpBuffer.size();
            break;
        }
        QByteArray pkt = m_rtpBuffer.take(m_rtpNextExpectedSeq);
        m_rtpNextExpectedSeq = static_cast<quint16>(m_rtpNextExpectedSeq + 1);
        processRtpPacket(reinterpret_cast<const uint8_t*>(pkt.constData()), pkt.size());
        drained++;

        // ★ 防止单次排空过多导致卡顿
        if (drained > 100) {
            qWarning() << "[H264][drain] 单次排空过多，强制退出 drained=" << drained
                       << " bufSize=" << m_rtpBuffer.size();
            break;
        }
    }

    // ★ 缓冲溢出处理
    if (m_rtpBuffer.size() > kRtpReorderBufferMax) {
        QList<quint16> keys = m_rtpBuffer.keys();
        if (keys.isEmpty()) {
            m_rtpBuffer.clear();
            return;
        }

        std::sort(keys.begin(), keys.end());
        quint16 minSeq = keys.first();
        quint16 maxSeq = keys.last();

        int16_t gap = static_cast<int16_t>(minSeq - m_rtpNextExpectedSeq);

        // ★ 智能判断：小间隙跳过，大间隙重置
        if (gap > 0 && gap < 200) {
            // 少量丢包：跳过缺失部分
            qDebug() << "[H264][drain] 跳过丢失 RTP 包，从 seq" << m_rtpNextExpectedSeq
                     << "跳到" << minSeq << "(gap=" << gap << ")";

            m_rtpNextExpectedSeq = minSeq;
            m_needKeyframe = true;
            m_framesSinceKeyframeRequest = 0;

            // 清空当前不完整的帧
            m_pendingFrame = PendingFrame();
            m_fuBuffer.clear();
            m_fuStarted = false;

            // 递归排空
            drainRtpBuffer();
        } else {
            // 大量丢包或乱序：清空重建
            qWarning() << "[H264][drain] RTP 缓冲严重错乱，清空重建"
                       << " bufSize=" << m_rtpBuffer.size()
                       << " expect=" << m_rtpNextExpectedSeq
                       << " minSeq=" << minSeq << " maxSeq=" << maxSeq
                       << " gap=" << gap;

            // 从最新的包开始
            m_rtpNextExpectedSeq = static_cast<quint16>(maxSeq + 1);
            m_rtpBuffer.clear();
            m_pendingFrame = PendingFrame();
            m_fuBuffer.clear();
            m_fuStarted = false;
            m_needKeyframe = true;
            m_framesSinceKeyframeRequest = 0;
        }
    }
}

void H264Decoder::processRtpPacket(const uint8_t *data, size_t len)
{
    if (len <= kRtpHeaderMinLen) return;

    quint16 seq    = rtpSeqNum(data);
    quint32 ts     = rtpTimestamp(data);
    bool    marker = rtpMarkerBit(data);

    m_lastRtpSeq = seq;
    m_rtpPacketsProcessed++;

    processRtpPayload(seq, ts, marker, data + kRtpHeaderMinLen, len - kRtpHeaderMinLen);
}

void H264Decoder::processRtpPayload(quint16 rtpSeq, quint32 rtpTs, bool marker,
                                     const uint8_t *payload, size_t payloadLen)
{
    try {
        if (payloadLen < 1) {
            qWarning() << "[H264][payload] 空 payload rtpSeq=" << rtpSeq;
            return;
        }

        if (payloadLen < 2) {
            qWarning() << "[H264][payload][WARN] payload 长度不足 rtpSeq=" << rtpSeq
                       << " payloadLen=" << payloadLen << "，忽略";
            return;
        }

        uint8_t nalByte = payload[0];
        int nalType = nalByte & 0x1f;

        if (nalType == 28) {
            if (payloadLen < 2) {
                qWarning() << "[H264][payload][WARN] FU-A 包太短 rtpSeq=" << rtpSeq << " len=" << payloadLen;
                return;
            }
            uint8_t fuHeader = payload[1];
            bool start = (fuHeader & kFuAStart) != 0;
            bool end   = (fuHeader & kFuAEnd) != 0;
            int fuNalType = fuHeader & 0x1f;

            if (start) {
                if (!m_fuStarted || m_fuNalType != fuNalType) {
                    // NAL type 变化或未开始，重置
                    m_fuBuffer.clear();
                }
                m_fuNalType = fuNalType;
                m_fuBuffer.clear();
                m_fuBuffer.append(static_cast<char>((nalByte & 0xe0) | fuNalType));
                m_fuBuffer.append(reinterpret_cast<const char*>(payload + 2),
                                  static_cast<int>(payloadLen - 2));
                m_fuStarted = true;
            } else if (m_fuStarted) {
                m_fuBuffer.append(reinterpret_cast<const char*>(payload + 2),
                                  static_cast<int>(payloadLen - 2));
            } else {
                // FU-A 中间/结束包，但 m_fuStarted=false（丢掉了 start）
                qDebug() << "[H264][payload] FU-A 片段丢失 start rtpSeq=" << rtpSeq
                         << " fuNalType=" << fuNalType << " ignored";
            }

            if (end && m_fuStarted) {
                const uint8_t *nal = reinterpret_cast<const uint8_t*>(m_fuBuffer.constData());
                size_t nalLen = m_fuBuffer.size();
                // ── ★★★ 端到端追踪：FU-A 组装完成，准备送入帧缓冲 ★★★ ──────────────
                static QSet<QString> s_loggedFuStreams;
                if (!s_loggedFuStreams.contains(m_streamTag)) {
                    s_loggedFuStreams.insert(m_streamTag);
                    qInfo() << "[H264][" << m_streamTag << "] FU-A 组装完成"
                             << " fuNalType=" << fuNalType << " nalLen=" << nalLen
                             << " ts=" << rtpTs << " marker=" << marker
                             << " ★ 对比 emitDecoded frameId 确认 FU-A→帧缓冲链路";
                }
                try {
                    bool handled = handleParameterSet(nal, nalLen);
                    if (!handled) {
                        // ── ★★★ 端到端追踪：NAL 送入帧缓冲 ★★★ ──────────────────────
                        appendNalToFrame(rtpTs, nal, nalLen, marker);
                    } else {
                        // ── SPS/PPS 参数集，跳过帧缓冲 ────────────────────────────────────
                        static QSet<QString> s_loggedSpsPps;
                        if (!s_loggedSpsPps.contains(m_streamTag)) {
                            s_loggedSpsPps.insert(m_streamTag);
                            qInfo() << "[H264][" << m_streamTag << "] SPS/PPS 已处理，FU-A type=" << fuNalType
                                     << " ★ 对比 emitDecoded 确认解码器参数就绪";
                        }
                    }
                } catch (const std::exception& e) {
                    qCritical() << "[H264][payload][ERROR] handleParameterSet 异常 rtpSeq=" << rtpSeq
                               << " error=" << e.what();
                } catch (...) {
                    qCritical() << "[H264][payload][ERROR] handleParameterSet 未知异常 rtpSeq=" << rtpSeq;
                }
                m_fuBuffer.clear();
                m_fuStarted = false;
            }
            return;
        }

        if (m_fuStarted) {
            // 收到非 FU-A NAL，但 FU-A 还未结束，清除 FU 缓冲
            qDebug() << "[H264][payload] FU-A 被中断 rtpSeq=" << rtpSeq
                     << " m_fuStarted=" << m_fuStarted << " nalType=" << nalType;
            m_fuBuffer.clear();
            m_fuStarted = false;
        }

        if (nalType == 24) {
            size_t offset = 1;
            std::vector<std::pair<const uint8_t*, size_t>> nalList;
            while (offset + 2 <= payloadLen) {
                uint16_t size = (static_cast<uint16_t>(payload[offset]) << 8)
                               | static_cast<uint16_t>(payload[offset + 1]);
                offset += 2;
                if (offset + size > payloadLen) {
                    qWarning() << "[H264][payload][WARN] STAP-A 长度字段越界 rtpSeq=" << rtpSeq
                               << " size=" << size << " remaining=" << (payloadLen - offset);
                    break;
                }
                const uint8_t *nal = payload + offset;
                try {
                    if (!handleParameterSet(nal, size)) {
                        nalList.push_back({nal, size});
                    }
                } catch (const std::exception& e) {
                    qCritical() << "[H264][payload][ERROR] STAP-A handleParameterSet 异常:"
                               << " rtpSeq=" << rtpSeq << " error=" << e.what();
                }
                offset += size;
            }
            for (size_t i = 0; i < nalList.size(); ++i) {
                bool isLast = (i == nalList.size() - 1);
                try {
                    appendNalToFrame(rtpTs, nalList[i].first, nalList[i].second,
                                    isLast && marker);
                } catch (const std::exception& e) {
                    qCritical() << "[H264][payload][ERROR] STAP-A appendNalToFrame 异常:"
                               << " rtpSeq=" << rtpSeq << " error=" << e.what();
                }
            }
            return;
        }

        if (nalType >= 1 && nalType <= 23) {
            try {
                if (!handleParameterSet(payload, payloadLen)) {
                    appendNalToFrame(rtpTs, payload, payloadLen, marker);
                }
            } catch (const std::exception& e) {
                qCritical() << "[H264][payload][ERROR] NAL type=" << nalType
                           << " handleParameterSet/appendNalToFrame 异常 rtpSeq=" << rtpSeq
                           << " error=" << e.what();
            }
            return;
        }

        // 其他 NAL type：暂时忽略
        qDebug() << "[H264][payload] 忽略未知 NAL type rtpSeq=" << rtpSeq << " nalType=" << nalType;
    } catch (const std::exception& e) {
        qCritical() << "[H264][payload][ERROR] processRtpPayload 总异常 rtpSeq=" << rtpSeq
                    << " payloadLen=" << payloadLen << " error=" << e.what();
    } catch (...) {
        qCritical() << "[H264][payload][ERROR] processRtpPayload 未知异常 rtpSeq=" << rtpSeq;
    }
}

// ============================================================================
// 帧聚合
// ============================================================================
void H264Decoder::appendNalToFrame(quint32 ts, const uint8_t *nal, size_t nalLen,
    bool marker)
{
    if (nalLen < 1) {
        qWarning() << "[H264][appendNal] nalLen=0，忽略";
        return;
    }

    int nalType = nal[0] & 0x1f;
    static QSet<int> s_reportedNalTypes;
    if (s_reportedNalTypes.size() < 20 && !s_reportedNalTypes.contains(nalType)) {
        s_reportedNalTypes.insert(nalType);
        qDebug() << "[H264][appendNal] 首次见到 NAL type=" << nalType
                 << " ts=" << ts << " marker=" << marker;
    }

    // ★ 时间戳切换处理
    if (m_pendingFrame.timestamp != 0 && m_pendingFrame.timestamp != ts) {
        if (!m_pendingFrame.nalUnits.empty()) {
            // ★ 宽容处理：只要有 slice 就尝试解码
            int sliceCount = 0;
            for (const auto &n : m_pendingFrame.nalUnits) {
                if (n.isEmpty()) continue;
                int t = static_cast<uint8_t>(n[0]) & 0x1f;
                if (t == 1 || t == 5) sliceCount++;
            }

            if (sliceCount > 0) {
                // ★ 即使 slice 数不足，也尝试解码（解码器会处理）
                if (m_expectedSliceCount > 0 && sliceCount < m_expectedSliceCount) {
                    if (!m_needKeyframe) {
                        m_needKeyframe = true;
                        m_framesSinceKeyframeRequest = 0;
                        qDebug() << "[H264][appendNal] 帧不完整 ts=" << m_pendingFrame.timestamp
                                 << " slices=" << sliceCount << "/" << m_expectedSliceCount;
                    }
                }
                m_pendingFrame.complete = true;
                try { flushPendingFrame(); } catch (...) {}
            }
        }
        m_pendingFrame = PendingFrame();
    }

    m_pendingFrame.timestamp = ts;
    m_pendingFrame.nalUnits.emplace_back(
        reinterpret_cast<const char*>(nal), static_cast<int>(nalLen));

    if (marker) {
        m_pendingFrame.complete = true;
        try { flushPendingFrame(); } catch (...) {}
    }
}

void H264Decoder::flushPendingFrame()
{
    try {
        if (m_pendingFrame.nalUnits.empty()) {
            m_pendingFrame = PendingFrame();
            return;
        }

        bool hasIdr = false;
        int sliceCount = 0;
        for (const auto &nal : m_pendingFrame.nalUnits) {
            if (nal.isEmpty()) continue;
            int t = static_cast<uint8_t>(nal[0]) & 0x1f;
            if (t == 5) hasIdr = true;
            if (t == 1 || t == 5) sliceCount++;
        }

        if (sliceCount == 0) {
            qDebug() << "[H264][flushPending] 无有效 slice，丢弃 ts=" << m_pendingFrame.timestamp;
            m_pendingFrame = PendingFrame();
            return;
        }

        // ★ 学习 slice 数量（从第一个完整帧开始）
        if (m_expectedSliceCount == 0 && m_pendingFrame.complete) {
            m_expectedSliceCount = sliceCount;
            qDebug() << "[H264] 学习到每帧 slice 数=" << m_expectedSliceCount;
        }

        // 首次必须等 IDR
        if (!m_haveDecodedKeyframe && !hasIdr) {
            m_droppedPFrameCount++;
            if (m_droppedPFrameCount <= 5 || m_droppedPFrameCount % 100 == 0)
                qDebug() << "[H264] 丢弃帧(首次等IDR) #" << m_droppedPFrameCount
                         << "ts=" << m_pendingFrame.timestamp
                         << "sliceCount=" << sliceCount;
            m_pendingFrame = PendingFrame();
            return;
        }

        // ★ 丢包恢复策略：
        if (m_needKeyframe) {
            if (hasIdr) {
                qInfo() << "[H264] 收到 IDR，丢包恢复完成 等待了"
                         << m_framesSinceKeyframeRequest << "帧"
                         << " ts=" << m_pendingFrame.timestamp;
                flushDecoder();
                m_needKeyframe = false;
                m_framesSinceKeyframeRequest = 0;
            } else {
                m_framesSinceKeyframeRequest++;
                if (m_framesSinceKeyframeRequest % 50 == 1) {
                    qDebug() << "[H264] 仍在等待 IDR，丢失 P 帧数=" << m_framesSinceKeyframeRequest
                             << " ts=" << m_pendingFrame.timestamp
                             << " sliceCount=" << sliceCount;
                }
            }
        }

        if (hasIdr) {
            m_haveDecodedKeyframe = true;
        }

        try {
            decodeCompleteFrame(m_pendingFrame.nalUnits);
        } catch (const std::exception& e) {
            qCritical() << "[H264][flushPending][ERROR] decodeCompleteFrame 异常:"
                       << " ts=" << m_pendingFrame.timestamp << " error=" << e.what();
        } catch (...) {
            qCritical() << "[H264][flushPending][ERROR] decodeCompleteFrame 未知异常:"
                       << " ts=" << m_pendingFrame.timestamp;
        }
        m_pendingFrame = PendingFrame();
    } catch (const std::exception& e) {
        qCritical() << "[H264][flushPending][ERROR] 总异常:" << e.what()
                   << " ts=" << m_pendingFrame.timestamp;
        m_pendingFrame = PendingFrame();
    } catch (...) {
        qCritical() << "[H264][flushPending][ERROR] 未知总异常 ts=" << m_pendingFrame.timestamp;
        m_pendingFrame = PendingFrame();
    }
}

// ============================================================================
// 解码
// ============================================================================

void H264Decoder::decodeCompleteFrame(const std::vector<QByteArray> &nalUnits)
{
    // ── ★★★ 端到端追踪：decodeCompleteFrame 进入 ★★★ ───────────────────────
    const int64_t funcEnterTime = QDateTime::currentMSecsSinceEpoch();
    const int64_t feedRtpToDecodeMs = (m_lastFeedRtpTime > 0) ? (funcEnterTime - m_lastFeedRtpTime) : -1;
    m_logSendCount++;
    if (m_logSendCount <= 10) {
        // 先计算 slice 信息
        int sliceCount = 0, idrCount = 0;
        for (const auto &nal : nalUnits) {
            if (nal.isEmpty()) continue;
            int t = static_cast<uint8_t>(nal[0]) & 0x1f;
            if (t == 1) sliceCount++;
            if (t == 5) { sliceCount++; idrCount++; }
        }
        size_t totalBytes = 0;
        for (const auto &nal : nalUnits) { if (!nal.isEmpty()) totalBytes += 4 + nal.size(); }
        qInfo() << "[H264][decode] ★★★ decodeCompleteFrame ENTER ★★★ stream=" << m_streamTag
                 << " frame#=" << m_logSendCount
                 << " slices=" << sliceCount << "(IDR=" << idrCount << ")"
                 << " totalBytes=" << totalBytes
                 << " rtpSeq=" << m_lastRtpSeq
                 << " feedRtpToDecodeMs=" << feedRtpToDecodeMs
                 << " needKeyframe=" << m_needKeyframe
                 << " codecOpen=" << m_codecOpen
                 << "（>100ms=主线程阻塞，<50ms=正常）";
    }

    try {
        if (!ensureDecoder()) {
            qWarning() << "[H264][decode] ensureDecoder 失败，丢弃帧";
            return;
        }

        static const uint8_t kStartCode[] = {0x00, 0x00, 0x00, 0x01};

        // ★ 计算总大小并构建 Annex-B 格式
        size_t totalSize = 0;
        int sliceCount = 0;
        int idrCount = 0;
        for (const auto &nal : nalUnits) {
            if (nal.isEmpty()) continue;
            totalSize += 4 + nal.size();
            int t = static_cast<uint8_t>(nal[0]) & 0x1f;
            if (t == 1) sliceCount++;
            if (t == 5) { sliceCount++; idrCount++; }
        }

        QByteArray annexB;
        annexB.reserve(static_cast<int>(totalSize));
        for (const auto &nal : nalUnits) {
            if (nal.isEmpty()) continue;
            annexB.append(reinterpret_cast<const char*>(kStartCode), 4);
            annexB.append(nal);
        }

        // m_logSendCount 已在函数入口递增，NAL/slice 信息已打印

        // ★ 创建并发送 AVPacket
        AVPacket *pkt = av_packet_alloc();
        if (!pkt) {
            qCritical() << "[H264][decode][ERROR] av_packet_alloc 失败";
            return;
        }

        pkt->data = reinterpret_cast<uint8_t*>(annexB.data());
        pkt->size = annexB.size();

        // ★ 标记可能损坏的帧
        if (m_needKeyframe) {
            pkt->flags |= AV_PKT_FLAG_CORRUPT;
        }

        int ret = avcodec_send_packet(m_ctx, pkt);
        av_packet_free(&pkt);

        if (ret == AVERROR(EAGAIN)) {
            // 解码器缓冲满，先取出帧
            qDebug() << "[H264][decode] avcodec_send_packet 返回 EAGAIN，先取出缓冲帧";
            try { emitDecodedFrames(); } catch (...) {}
            return;
        }

        if (ret == AVERROR_INVALIDDATA || ret == AVERROR(EIO)) {
            // 数据无效或硬件解码器 I/O 错误，立即 flush 解码器并标记需要 IDR
            const char* errName = (ret == AVERROR(EIO)) ? "EIO" : "INVALIDDATA";
            qWarning() << "[H264][decode][ERROR] avcodec_send_packet 失败(" << errName << ")"
                       << " ret=" << ret << " 立即 flush 并等待 IDR"
                       << " slices=" << sliceCount << " codecOpen=" << m_codecOpen;
            flushDecoder();
            return;
        }

        if (ret < 0 && ret != AVERROR_EOF && ret != AVERROR(EAGAIN) && ret != AVERROR(EIO)) {
            char errbuf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, errbuf, sizeof(errbuf));
            qCritical() << "[H264][decode][ERROR] avcodec_send_packet 未知错误 ret=" << ret
                         << " err=" << errbuf;
            return;
        }

        try { emitDecodedFrames(); } catch (...) {}
    } catch (const std::exception& e) {
        qCritical() << "[H264][decodeCompleteFrame][ERROR] 总异常:" << e.what();
    } catch (...) {
        qCritical() << "[H264][decodeCompleteFrame][ERROR] 未知总异常";
    }
}

void H264Decoder::emitDecodedFrames()
{
    // ── ★★★ 端到端追踪：emitDecodedFrames 进入 ★★★ ───────────────────────
    const int64_t funcEnterTime = QDateTime::currentMSecsSinceEpoch();
    const int64_t feedRtpToEmitMs = (m_lastFeedRtpTime > 0) ? (funcEnterTime - m_lastFeedRtpTime) : -1;
    qInfo() << "[H264][emit] ★★★ emitDecodedFrames ENTER ★★★ stream=" << m_streamTag
             << " feedRtpToEmitMs=" << feedRtpToEmitMs
             << "（从 RTP 包到解码输出的端到端耗时，>200ms 说明主线程阻塞或解码卡顿）";

    AVFrame *frame = av_frame_alloc();
    if (!frame) {
        qCritical() << "[H264][" << m_streamTag << "] av_frame_alloc 失败!";
        return;
    }

    int consecutive_errors = 0;
    const int max_consecutive_errors = 3;  // 连续3次错误后 flush
    int framesOut = 0;

    while (true) {
        int ret;
        try {
            ret = avcodec_receive_frame(m_ctx, frame);
        } catch (const std::exception& e) {
            qCritical() << "[H264][" << m_streamTag << "][ERROR] avcodec_receive_frame 异常:" << e.what()
                       << " m_ctx=" << (void*)m_ctx;
            break;
        } catch (...) {
            qCritical() << "[H264][" << m_streamTag << "][ERROR] avcodec_receive_frame 未知异常"
                       << " m_ctx=" << (void*)m_ctx;
            break;
        }

        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;  // 正常结束
        }

        if (ret < 0) {
            // 解码错误
            consecutive_errors++;
            if (consecutive_errors >= max_consecutive_errors && !m_needKeyframe) {
                qWarning() << "[H264][" << m_streamTag << "][ERROR] 连续" << consecutive_errors
                           << "次解码错误，强制 flush 并等待 IDR";
                flushDecoder();
                m_needKeyframe = true;
                m_framesSinceKeyframeRequest = 0;
            }
            break;  // 跳过当前帧
        }

        consecutive_errors = 0;  // 重置错误计数

        try {
            int w = frame->width;
            int h = frame->height;
            if (w <= 0 || h <= 0) {
                qDebug() << "[H264][" << m_streamTag << "] 无效帧尺寸 w=" << w << " h=" << h << "，跳过";
                continue;
            }

            if (m_width != w || m_height != h) {
                qInfo() << "[H264][" << m_streamTag << "] 视频分辨率变化: " << m_width << "x" << m_height
                         << " -> " << w << "x" << h << "，重建 sws 上下文";
                m_width = w;
                m_height = h;
                if (m_sws) { sws_freeContext(m_sws); m_sws = nullptr; }
            }
            if (!m_sws) {
                m_sws = sws_getContext(w, h,
                                       static_cast<AVPixelFormat>(frame->format),
                                       w, h, AV_PIX_FMT_RGB24,
                                       SWS_BILINEAR, nullptr, nullptr, nullptr);
                if (!m_sws) {
                    qWarning() << "[H264][" << m_streamTag << "][ERROR] sws_getContext 返回 nullptr:"
                               << " w=" << w << " h=" << h << " fmt=" << frame->format
                               << "，跳过帧";
                    continue;
                }
            }

            // 帧缓冲复用：尺寸不变时复用内存，detach() 在 refcount==1 时为 no-op
            if (m_frameBuffer.width() != w || m_frameBuffer.height() != h ||
                m_frameBuffer.format() != QImage::Format_RGB888) {
                m_frameBuffer = QImage(w, h, QImage::Format_RGB888);
                if (m_frameBuffer.isNull()) {
                    qCritical() << "[H264][" << m_streamTag << "][ERROR] QImage 分配失败 w=" << w << " h=" << h
                               << "，跳过帧";
                    continue;
                }
            } else {
                m_frameBuffer.detach();  // COW：若上帧仍在事件队列则拷贝一次，否则 no-op
            }
            uint8_t *dst[] = { m_frameBuffer.bits() };
            int dstStride[] = { static_cast<int>(m_frameBuffer.bytesPerLine()) };
            if (!dst[0]) {
                qWarning() << "[H264][" << m_streamTag << "][ERROR] m_frameBuffer.bits() 返回 nullptr:"
                           << " w=" << w << " h=" << h << "，跳过帧";
                continue;
            }
            int scaleRet = sws_scale(m_sws, frame->data, frame->linesize, 0, h, dst, dstStride);
            if (scaleRet != h) {
                qWarning() << "[H264][" << m_streamTag << "][WARN] sws_scale 返回" << scaleRet << "期望" << h
                           << " w=" << w << " h=" << h << "，帧可能损坏但继续使用";
            }

            // ── ★★★ 端到端追踪：色彩转换完成，QImage 帧数据就绪 ★★★ ─────────────
            // 此处 m_frameBuffer 已是完整 RGB888 QImage，可送入 Qt 信号系统
            // 记录这一刻的时间戳，用于计算 解码→QML 的端到端延迟
            const int64_t colorConvertDoneTime = QDateTime::currentMSecsSinceEpoch();
            m_framesEmitted++;
            m_statsFramesInWindow++;
            framesOut++;
            m_frameIdCounter++;
            // frameId 用于端到端追踪：feedRtp → frameReady → onVideoFrameFromDecoder → QML handler
            if (m_framesEmitted <= 5) {
                qInfo() << "[H264][" << m_streamTag << "] ★★★ emitDecoded 输出帧 ★★★ #" << m_framesEmitted
                         << " frameId=" << m_frameIdCounter
                         << " w=" << w << " h=" << h
                         << " rtpSeq=" << m_lastRtpSeq
                         << " codecOpen=" << m_codecOpen
                         << " colorConvertDoneMs=" << colorConvertDoneTime
                         << " ★ 对比 onVideoFrameFromDecoder frameId=" << m_frameIdCounter << " 确认解码→emit 链路";
            }

            try {
                // ── ★★★ 端到端追踪：发出 frameReady 信号（进入 Qt 事件队列）★★★ ─────────
                // 注意：QueuedConnection 下，emit 立即返回，实际 handler 在主线程事件循环中执行
                // frameId 必须与 onVideoFrameFromDecoder 中的 frameId 一致
                emit frameReady(m_frameBuffer, m_frameIdCounter);
                // ★★★ 如果此日志不出现但 emitDecoded 日志出现 → QueuedConnection 失效 ★★★
                if (m_framesEmitted <= 5) {
                    int queuedConnCount = this->receivers(SIGNAL(frameReady(const QImage&, quint64)));
                    qInfo() << "[H264][" << m_streamTag << "] ★★★ emit frameReady 完成 ★★★"
                             << " frameId=" << m_frameIdCounter
                             << " queuedConnections=" << queuedConnCount
                             << " ★ queuedConnections=0 → WebRtcClient::onVideoFrameFromDecoder 未连接到 frameReady 信号！"
                             << " queuedConnections>0 → 链路完整，对比 onVideoFrameFromDecoder frameId 确认";
                }
            } catch (const std::exception& e) {
                qCritical() << "[H264][" << m_streamTag << "][ERROR] emit frameReady 异常:" << e.what();
            } catch (...) {
                qCritical() << "[H264][" << m_streamTag << "][ERROR] emit frameReady 未知异常";
            }
        } catch (const std::exception& e) {
            qCritical() << "[H264][" << m_streamTag << "][ERROR] 帧处理循环异常:" << e.what();
            continue;
        } catch (...) {
            qCritical() << "[H264][" << m_streamTag << "][ERROR] 帧处理循环未知异常";
            continue;
        }
    }

    av_frame_free(&frame);

    if (framesOut > 0) {
        qDebug() << "[H264][" << m_streamTag << "] emitDecoded: 本次输出" << framesOut << "帧，total=" << m_framesEmitted;
    }
}
