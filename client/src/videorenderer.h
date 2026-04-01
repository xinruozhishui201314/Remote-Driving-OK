#ifndef VIDEORENDERER_H
#define VIDEORENDERER_H

#include <QQuickPaintedItem>
#include <QImage>
#include <QMutex>
#include <memory>
#include <atomic>

/**
 * QML 可用的视频渲染器：在 paint() 中绘制当前帧（QImage）。
 *
 * 性能优化：双缓冲 + shared_ptr 原子交换
 *  - setFrame: 将 decoded QImage 封装为 shared_ptr，原子写入 m_pendingFrame；
 *    不做 deep copy（QImage 隐式共享，解码器每帧创建新对象，safe）。
 *  - paint: 原子读取 shared_ptr，仅做引用计数操作（O(1)），无 mutex 竞争。
 */
class VideoRenderer : public QQuickPaintedItem
{
    Q_OBJECT
    Q_PROPERTY(QString label READ label WRITE setLabel NOTIFY labelChanged)
    /** Fill: 拉伸填满容器（不裁剪）；Contain: 适应容器（留黑边）。默认 Fill */
    Q_PROPERTY(QString fillMode READ fillMode WRITE setFillMode NOTIFY fillModeChanged)
public:
    explicit VideoRenderer(QQuickItem *parent = nullptr);

    QString label() const { return m_label; }
    void setLabel(const QString &label);
    QString fillMode() const { return m_fillMode; }
    void setFillMode(const QString &mode);

    /** 从任意线程调用；原子交换帧指针，然后请求重绘 */
    Q_INVOKABLE void setFrame(const QImage &image);
    void paint(QPainter *painter) override;

signals:
    void labelChanged();
    void fillModeChanged();
    void renderLatencyMeasured(qreal latencyMs);

private:
    // 双缓冲：pending 由解码线程写，render 由 GUI 线程读
    // 使用 mutex 保护交换（std::atomic<shared_ptr> 需要 C++20）
    std::shared_ptr<QImage> m_pendingFrame;
    std::shared_ptr<QImage> m_renderFrame;
    QMutex m_swapMutex; // 仅保护指针交换，不持锁做 IO/拷贝

    QString m_fillMode = QStringLiteral("Fill");
    QString m_label;
};

#endif // VIDEORENDERER_H
