#include "videorenderer.h"
#include <QPainter>
#include <QDebug>
#include <QElapsedTimer>

VideoRenderer::VideoRenderer(QQuickItem *parent) : QQuickPaintedItem(parent)
{
    setFillColor(Qt::black);
    setAntialiasing(false);
    // 确保 VideoRenderer 可见且可渲染
    setVisible(true);
    setEnabled(true);
    // 设置 clip 为 false，允许内容超出边界（如果需要）
    setClip(false);
}

void VideoRenderer::setLabel(const QString &label)
{
    if (m_label != label) {
        m_label = label;
        emit labelChanged();
    }
}

void VideoRenderer::setFillMode(const QString &mode)
{
    QString m = mode.trimmed();
    if (m.isEmpty()) m = QStringLiteral("Fill");
    if (m_fillMode != m) {
        m_fillMode = m;
        emit fillModeChanged();
        update();
    }
}

void VideoRenderer::setFrame(const QImage &image)
{
    if (image.isNull() || image.width() <= 0 || image.height() <= 0) {
        return;
    }

    // QImage 是隐式共享（写时拷贝）。解码器每帧创建新 QImage 对象后通过
    // QueuedConnection 传入，此时数据不会被解码器侧再次修改，直接共享引用安全。
    // 封装为 shared_ptr，原子交换到 pending 槽，paint() 从 pending 槽取出渲染。
    auto newFrame = std::make_shared<QImage>(image);

    {
        QMutexLocker lock(&m_swapMutex);
        m_pendingFrame = std::move(newFrame);  // O(1)，无深拷贝
    }

    // 请求主线程重绘（若已在主线程则直接排队一次 update）
    update();
}

void VideoRenderer::paint(QPainter *painter)
{
    // 在主线程中：把 pending 指针交换到 render 槽，然后渲染
    {
        QMutexLocker lock(&m_swapMutex);
        if (m_pendingFrame) {
            m_renderFrame = std::move(m_pendingFrame);
            m_pendingFrame = nullptr;
        }
    }

    // 从此处开始无锁访问 m_renderFrame（只有主线程读/写）
    const QImage &frame = m_renderFrame ? *m_renderFrame : QImage{};

    if (frame.isNull()) {
        // 无视频帧时填充黑色背景
        painter->fillRect(boundingRect(), fillColor());
        return;
    }
    
    QRectF dst = boundingRect();
    if (dst.isEmpty()) {
        qWarning() << "[VideoRenderer] boundingRect 为空，label=" << m_label;
        return;
    }
    
    qreal frameAspect = static_cast<qreal>(frame.width()) / static_cast<qreal>(frame.height());
    qreal dstAspect = dst.width() / dst.height();
    QRectF src(0, 0, frame.width(), frame.height());
    
    if (m_fillMode == QLatin1String("Contain")) {
        // Contain：适应容器，保持比例，可能留黑边
        if (frameAspect > dstAspect) {
            qreal h = dst.width() / frameAspect;
            dst.setY(dst.y() + (dst.height() - h) * 0.5);
            dst.setHeight(h);
        } else {
            qreal w = dst.height() * frameAspect;
            dst.setX(dst.x() + (dst.width() - w) * 0.5);
            dst.setWidth(w);
        }
    } else {
        // Fill（默认）：拉伸填满容器，不裁剪，无黑边（可能比例失真）
        dst = boundingRect();
        src = QRectF(0, 0, frame.width(), frame.height());
    }
    painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter->setRenderHint(QPainter::Antialiasing, false);  // 视频渲染不需要抗锯齿

    // ★ 记录 T5 渲染耗时
    QElapsedTimer renderTimer;
    renderTimer.start();
    
    painter->drawImage(dst, frame, src, Qt::NoFormatConversion);
    
    double t5_ms = renderTimer.nsecsElapsed() / 1000000.0;
    
    // Plan 4.2: Emit signal for T5 latency monitoring
    emit renderLatencyMeasured(t5_ms);
    
    // 每 200 帧输出一次渲染统计
    static int paintCount = 0;
    if (++paintCount % 200 == 0) {
        qDebug() << "[VideoRenderer] paint#" << paintCount << "label=" << m_label
                 << "size=" << frame.size() << "T5=" << t5_ms << "ms";
    }
}
