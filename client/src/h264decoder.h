// h264decoder.h
#ifndef H264DECODER_H
#define H264DECODER_H

#include <QObject>
#include <QImage>
#include <QByteArray>
#include <QHash>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
}

class H264Decoder : public QObject
{
    Q_OBJECT
public:
    explicit H264Decoder(const QString &streamTag = QString(), QObject *parent = nullptr);
    ~H264Decoder();

    void feedRtp(const uint8_t *data, size_t len);
    void reset();

signals:
    void frameReady(const QImage &image, quint64 frameId);

private:
    static constexpr size_t kRtpHeaderMinLen = 12;
    static constexpr int kRtpReorderBufferMax = 256;
    static constexpr int kRtpJitterThreshold = 50;   // 新增：抖动阈值

    // RTP 排序
    QHash<quint16, QByteArray> m_rtpBuffer;
    quint16 m_rtpNextExpectedSeq = 0;
    bool m_rtpSeqInitialized = false;

    /** 帧唯一编号：递增，每帧一个 ID，便于端到端追踪（feedRtp → emit frameReady → QML handler） */
    quint64 m_frameIdCounter = 0;

    void processRtpPacket(const uint8_t *data, size_t len);
    void processRtpPayload(quint16 seq, quint32 ts, bool marker,
                           const uint8_t *payload, size_t payloadLen);
    void drainRtpBuffer();

    // FU-A
    QByteArray m_fuBuffer;
    bool m_fuStarted = false;
    int m_fuNalType = 0;

    // 帧聚合
    struct PendingFrame {
        quint32 timestamp = 0;
        std::vector<QByteArray> nalUnits;
        bool complete = false;
    };
    PendingFrame m_pendingFrame;

    void appendNalToFrame(quint32 ts, const uint8_t *nal, size_t nalLen, bool marker);
    void flushPendingFrame();

    // SPS/PPS
    QByteArray m_sps;
    QByteArray m_pps;
    bool handleParameterSet(const uint8_t *nal, size_t nalLen);

    // 解码器
    const AVCodec *m_codec = nullptr;
    AVCodecContext *m_ctx = nullptr;
    bool m_codecOpen = false;
    bool m_haveDecodedKeyframe = false;
    // ★ 丢包恢复：丢包后不立即禁止P帧，而是标记"需要IDR"
    //   同时计数P帧，如果连续N帧没有IDR，才考虑更激进的策略
    bool m_needKeyframe = false;        // 丢包后标记，收到IDR时清除
    int m_framesSinceKeyframeRequest = 0; // 丢包后已过帧数
    int m_expectedSliceCount = 0;       // 学习到的每帧slice数

    bool ensureDecoder();
    bool openDecoderWithExtradata();
    void flushDecoder();
    void closeDecoder();

    void decodeCompleteFrame(const std::vector<QByteArray> &nalUnits);
    void emitDecodedFrames();

    // 色彩转换
    SwsContext *m_sws = nullptr;
    int m_width = 0;
    int m_height = 0;

    // 帧缓冲池：复用 QImage 内存，避免每帧堆分配（30fps × 1080p ≈ 6MB/帧）
    // QImage 隐式共享：若上一帧仍在事件队列中（refcount>1），detach() 会 COW
    // 一份；若已消费（refcount==1），detach() 是 no-op，零分配。
    QImage m_frameBuffer;

    // ★ 诊断标签：用于区分四路解码器的日志（streamTag = cam_front/rear/left/right）
    QString m_streamTag;

    // 统计
    int m_framesEmitted = 0;
    int m_droppedPFrameCount = 0;
    int m_rtpPacketsProcessed = 0;
    quint16 m_lastRtpSeq = 0;
    int m_logSendCount = 0;
    // ── 诊断日志增强 ─────────────────────────────────────────────────────────
    int64_t m_statsWindowStart = 0;     // 统计窗口开始时间
    int m_statsFramesInWindow = 0;      // 窗口内帧数
    int m_statsPacketsInWindow = 0;      // 窗口内 RTP 包数
    int m_statsDroppedInWindow = 0;      // 窗口内丢弃帧数
    int m_lastStatSeqNum = 0;           // 上次统计时的 RTP seq（用于算丢包率）
    static const int kStatsIntervalMs = 1000; // 1s 统计一次
    // ── 诊断日志增强结束 ───────────────────────────────────────────────────
};

#endif