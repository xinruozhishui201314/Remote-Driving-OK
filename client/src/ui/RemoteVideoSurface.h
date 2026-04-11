#ifndef REMOTEVIDEOSURFACE_H
#define REMOTEVIDEOSURFACE_H

#include "media/CpuVideoRgba8888Frame.h"
#include "media/DmaBufFrameHandle.h"

#include <atomic>

#include <QImage>
#include <QMutex>
#include <QQuickItem>
#include <QSize>
#include <QtQml/qqmlregistration.h>

/**
 * @brief 远程视频 Quick 项：Scene Graph 纹理路径呈现，与 GUI 事件合理解耦。
 *
 * 解码 → 独立呈现线程（合帧/限频）→ 主线程仅 applyFrame()+update()（轻量）；
 * 纹理上传在 updatePaintNode（多平台下常由 Qt Quick 渲染线程执行，见
 * QQuickWindow::createTextureFromImage 文档）。
 *
 * 替代 QML VideoOutput + QVideoSink::setVideoFrame，减轻主线程 setVideoFrame 风暴。
 *
 * DMA-BUF（VAAPI NV12）路径：applyDmaBufFrame → Scene Graph 自定义材质（需 CLIENT_HAVE_NV12_DMABUF_SG）。
 */
enum class RemotePresentMode : uint8_t { CpuTexture = 0, Nv12DmaBuf = 1 };

class RemoteVideoSurface : public QQuickItem {
  Q_OBJECT
  QML_ELEMENT
  /** 0=拉伸填满 1=PreserveAspectCrop（与 VideoOutput.PreserveAspectCrop 行为对齐） */
  Q_PROPERTY(int fillMode READ fillMode WRITE setFillMode NOTIFY fillModeChanged)
  /**
   * QML 侧设置面板标题（如「主视图」「左视图」），用于 [VideoBind]/[DPR]/PNG 落盘与日志对齐 stream。
   */
  Q_PROPERTY(QString panelLabel READ panelLabel WRITE setPanelLabel NOTIFY panelLabelChanged)

 public:
  explicit RemoteVideoSurface(QQuickItem *parent = nullptr);

  int fillMode() const { return m_fillMode; }
  void setFillMode(int mode);

  QString panelLabel() const { return m_panelLabel; }
  void setPanelLabel(const QString &label);

  /** 主线程：提交一帧并触发 Scene Graph 更新（由 WebRtcClient 从呈现线程经 QueuedConnection 调用）
   */
 public slots:
  /**
   * QML/兼容路径：接受任意 QImage，内部 normalize（默认严格模式则拒绝非 RGBA8888）。
   * C++ 主路径请用 applyCpuRgba8888Frame + CpuVideoRgba8888Frame::tryAdopt。
   */
  void applyFrame(QImage image, quint64 frameId, const QString &streamTag = QString());
  void applyDmaBufFrame(SharedDmaBufFrame handle, quint64 frameId,
                        const QString &streamTag = QString());

 signals:
  void fillModeChanged();
  void panelLabelChanged();
  /** 与 VideoOutput 对齐：有帧可显示时 width/height>0 */
  void frameGeometryChanged();
  /**
   * DMA-BUF Scene Graph 节点创建/导入失败（常在渲染线程 emit）；WebRtcClient 应 QueuedConnection
   * 接收并通知解码器关闭 PRIME 导出、改走 CPU RGBA。
   */
  void dmaBufSceneGraphFailed(const QString& reason);

 public:
  /** C++ 主路径：已满足 RGBA8888+stride 契约（与 applyDmaBufFrame 隔离；非 QML slot） */
  void applyCpuRgba8888Frame(CpuVideoRgba8888Frame cpuFrame, quint64 frameId,
                             const QString &streamTag = QString());

 protected:
  QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *data) override;

 private:
  void commitCpuTextureFrame(QImage &&image, quint64 frameId, const QString &streamTag);
  QRectF mapImageToRect(const QImage &img) const;
  QRectF mapSourceSizeToRect(const QSize &sourceSize) const;

  int m_fillMode = 1;  // PreserveAspectCrop
  QString m_panelLabel;
  qreal m_lastCommittedDpr = -1.0;
  std::atomic<bool> m_itemGeomLoggedOnce{false};
  QMutex m_mutex;
  RemotePresentMode m_presentMode = RemotePresentMode::CpuTexture;
  QImage m_frame;
  SharedDmaBufFrame m_dmaDisplay;
  quint64 m_lastFrameId = 0;
  bool m_hasPendingFrame = false;
  QSize m_lastSize;
  QString m_streamTag;
  /** 每轮 DMA 呈现尝试仅触发一次回退信号（applyDmaBufFrame 时清零） */
  std::atomic<bool> m_dmaBufFallbackEmitted{false};
  /** 连续 SG 失败次数（成功呈现时清零）；与永久错误/阈值共同决定是否切 CPU RGBA */
  std::atomic<int> m_dmaSgFailStreak{0};
};

#endif
