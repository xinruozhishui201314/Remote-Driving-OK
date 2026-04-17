#ifndef WEBRTCSTREAMMANAGER_H
#define WEBRTCSTREAMMANAGER_H

#include "webrtcclient.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>

#include <atomic>

class QNetworkAccessManager;
class QNetworkReply;

class VehicleManager;
class MqttController;

/**
 * @brief 四路相机 WebRTC 流管理器
 * 管理前/后/左/右四路 WHEP 拉流，供 DrivingInterface 四宫格显示。
 * 多车隔离：流 ID 带 VIN 前缀，格式 {vin}_cam_front 等；VIN 为空时退化为
 * cam_front（兼容单车测试）。 VIN 通过 setCurrentVin() 或由 connectFourStreams() 从 VehicleManager
 * 同步（见 setVehicleManager）。
 *
 * 诊断增强：
 * - 每 15s 轮询 ZLM getMediaList，验证 carla-bridge 推流是否仍在 ZLM 注册
 * - 若推流在册但客户端断流 → 诊断 RTCP/ICE/配置问题
 * - 若推流失册 → 诊断 carla-bridge / MQTT / CARLA 问题
 */
class WebRtcStreamManager : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY(WebRtcStreamManager)
  Q_PROPERTY(WebRtcClient *frontClient READ frontClient CONSTANT)
  Q_PROPERTY(WebRtcClient *rearClient READ rearClient CONSTANT)
  Q_PROPERTY(WebRtcClient *leftClient READ leftClient CONSTANT)
  Q_PROPERTY(WebRtcClient *rightClient READ rightClient CONSTANT)
  Q_PROPERTY(bool anyConnected READ anyConnected NOTIFY anyConnectedChanged)
  /** 车端推流就绪状态（由 VehicleStatus 同步） */
  Q_PROPERTY(bool streamingReady READ streamingReady WRITE setStreamingReady NOTIFY streamingReadyChanged)

 public:
  explicit WebRtcStreamManager(QObject *parent = nullptr);
  virtual ~WebRtcStreamManager();

  WebRtcClient *frontClient() const { return m_front; }
  WebRtcClient *rearClient() const { return m_rear; }
  WebRtcClient *leftClient() const { return m_left; }
  WebRtcClient *rightClient() const { return m_right; }

  bool anyConnected() const;
  bool streamingReady() const { return m_streamingReady; }
  void setStreamingReady(bool ready);

  /** 设置当前 VIN，供 connectFourStreams 构造 VIN-prefixed 流名（实现于 .cpp，带
   * [Client][StreamE2E] 日志） */
  virtual void setCurrentVin(const QString &vin);
  QString currentVin() const { return m_vin; }

  /** 注入车辆管理器后，每次 connectFourStreams 会以 VehicleManager::currentVin()
   * 为准同步流名（单一事实来源） */
  void setVehicleManager(VehicleManager *vm) { m_vehicleManager = vm; }

  /** 解码条状自愈时 WebRtcClient 发 encoder hint → MQTT 车端闭环（见 teleop/client_encoder_hint） */
  void connectEncoderHintMqttRelay(MqttController *mqtt);
  /** 设置 MQTT 控制器，用于在 ZLM 未就绪时重试 start_stream */
  void setMqttController(MqttController *mqtt);

  /**
   * 从 WHEP URL 解析 base URL 与 app，再连接四路（流名加 VIN 前缀）；
   * 若 whepUrl 为空则用 ZLM_VIDEO_URL 环境变量。
   */
  Q_INVOKABLE virtual void connectFourStreams(const QString &whepUrl = QString());
  /**
   * 在 ZLM getMediaList 上出现本车四路 {vin}_cam_* 后再 connectFourStreams（轮询，超时则仍拉流兜底）。
   * @param pollIntervalMs 探测间隔
   * @param maxWaitMs 最长等待；超时后调用 connectFourStreams 交由 WebRTC 重试 stream not found
   */
  Q_INVOKABLE void scheduleConnectFourStreamsWhenZlmReady(const QString &whepUrl = QString(),
                                                          int pollIntervalMs = 1000,
                                                          int maxWaitMs = 45000);
  /** 断开四路 */
  Q_INVOKABLE virtual void disconnectAll();

  /**
   * 以下为 QML 诊断专用 API，必须放在 public：Qt QML 引擎对 C++ 方法的可调用性与可见性
   * 约定为 public slot / public Q_INVOKABLE（见 Qt 文档「Exposing Attributes of C++ Types to
   * QML」）。 若放在 private，QML 中 _wsm.getLeftSignalReceiverCount 等为 undefined，PeriodicDiag
   * 会误报 myRc=-1。
   */
  Q_INVOKABLE QString getStreamDebugInfo() const;
  Q_INVOKABLE int getQmlSignalReceiverCount() const;
  Q_INVOKABLE int getFrontSignalReceiverCount() const;
  Q_INVOKABLE int getRearSignalReceiverCount() const;
  Q_INVOKABLE int getLeftSignalReceiverCount() const;
  Q_INVOKABLE int getRightSignalReceiverCount() const;
  Q_INVOKABLE QString getStreamSignalMetaInfo() const;
  Q_INVOKABLE void dumpStreamInfo() const;
  Q_INVOKABLE void forceRefreshAllRenderers(const QString &reason = QString());
  /** 诊断：四路解码→主线程 handler 待处理深度之和（闪烁/卡顿相关） */
  Q_INVOKABLE int getTotalPendingFrames() const;

  /**
   * QML 侧检测到「everHadVideoFrame true→false」等盖层风险时调用，便于与 C++ 1Hz
   * 分类写入同一日志文件。 对应日志前缀：[Client][VideoFlickerClass][QML_LAYER]
   */
  Q_INVOKABLE void reportVideoFlickerQmlLayerEvent(const QString &where,
                                                   const QString &detail = QString());

 signals:
  void anyConnectedChanged();
  void streamingReadyChanged(bool ready);

 private:
  /** 诊断：每 15s 查询一次 ZLM getMediaList，验证推流是否在册 */
  void checkZlmStreamRegistration();
  QString baseUrlFromWhep(const QString &whepUrl) const;
  QString appFromWhep(const QString &whepUrl) const;
  /** 解析并规范化 base URL；空时返回空字符串（由调用方决定 fallback） */
  QString resolveBaseUrl(const QString &whepUrl) const;
  void syncStreamVinFromVehicleManager();

  WebRtcClient *m_front = nullptr;
  WebRtcClient *m_rear = nullptr;
  WebRtcClient *m_left = nullptr;
  WebRtcClient *m_right = nullptr;
  QString m_app = QStringLiteral("teleop");
  QString m_vin;  // 当前 VIN，用于构造 {vin}_cam_front 等流名
  VehicleManager *m_vehicleManager = nullptr;
  /** 防止同一 base URL 下的无意义断连：比较新旧 base，只有实际变更才断连重建 */
  QString m_currentBase;
  QTimer *m_zlmPollTimer = nullptr;  // ZLM 流状态轮询定时器
  /** 主线程呈现 1Hz 汇总（CLIENT_VIDEO_PRESENT_1HZ_SUMMARY=0 可关） */
  QTimer *m_presentDiag1HzTimer = nullptr;
  /** 防止 ZLM 流状态检查 log 刷屏：同状态 diff 时才打日志 */
  QString m_lastZlmStreamsSeen;
  /** 上次已向 QML 发出的 anyConnected 聚合值；避免单路 connectionStatusChanged 重复触发 NOTIFY */
  bool m_lastEmittedAnyConnected = false;
  /** 本秒内 QML 报告的盖层风险次数（emitVideoPresent1HzSummary 开头清零并用于 cat= 分类） */
  std::atomic<int> m_qmlLayerEventsThisSecond{0};
  /** 首次 1Hz 汇总时打印实际 OpenGL Renderer 字符串（GL_VENDOR/GL_RENDERER/GL_VERSION），之后不重复
   */
  bool m_glInfoLoggedOnce = false;
  /** 运行期呈现 SLO：maxQueuedLag 连续超阈秒数（1Hz 定时器驱动） */
  int m_runtimeHighQueuedLagStreakSec = 0;
  int m_runtimeLowQueuedLagStreakSec = 0;
  bool m_runtimeQueuedLagSloBreached = false;
  bool m_streamingReady = false;

 private slots:
  void onZlmReadyTimerTick();
  /** 与 disconnectWaveId 同窗去重后拉取 ZLM getMediaList 写日志（可由环境变量
   * CLIENT_DISABLE_ZLM_WAVE_SNAPSHOT=1 关闭） */
  void onZlmSnapshotRequested(int disconnectWaveId, const QString &stream, int peerStateEnum);
  /** 四路 QVideoSink::setVideoFrame 路径 1Hz 单行汇总 */
  void emitVideoPresent1HzSummary();

 private:
  void fetchZlmMediaListSnapshotForWave(int disconnectWaveId, const QString &triggerStream,
                                        int peerStateEnum);
  void cancelZlmReadySchedule();

  QString m_streamsConnectedVin;
  QString m_startStreamRequestedVin; // ★ 记录已发送 start_stream 的 VIN，避免重复触发 Bridge 重置
  QTimer *m_zlmReadyTimer = nullptr;
  QNetworkAccessManager *m_zlmReadyNam = nullptr;
  QNetworkReply *m_zlmReadyReply = nullptr;
  QString m_zlmReadyWhep;
  qint64 m_zlmReadyDeadlineMs = 0;
  qint64 m_lastStartStreamRetryMs = 0; // 上次尝试重发 start_stream 的时间
  bool m_zlmReadyPollInFlight = false;
  QPointer<MqttController> m_mqtt; // [SystemicFix] 使用 QPointer 避免析构顺序导致的野指针崩溃
};

#endif  // WEBRTCSTREAMMANAGER_H
