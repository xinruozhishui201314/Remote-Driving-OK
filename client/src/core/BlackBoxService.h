#pragma once
#include <QFile>
#include <QObject>
#include <QThread>
#include "eventbus.h"

/**
 * 黑匣子记录系统（《客户端架构设计》§8.1 可靠性规范）。
 * 
 * 职责：
 *   1. 订阅 EventBus 中的关键控制和状态事件。
 *   2. 采用异步环形缓冲区或独立线程持久化到二进制文件。
 *   3. 确保在系统崩溃前关键数据已落盘。
 */
class BlackBoxService : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY(BlackBoxService)

 public:
  static BlackBoxService& instance() {
    static BlackBoxService inst;
    return inst;
  }

  explicit BlackBoxService(QObject* parent = nullptr);
  ~BlackBoxService() override;

  bool start(const QString& logDir = "/var/log/teleop/");
  void stop();

 private:
  void setupSubscriptions();
  void writeHeader();
  
  // 核心记录逻辑（二进制格式优化，紧凑存储）
  template<typename T>
  void writeRecord(uint32_t typeId, const T& data);

  QFile m_file;
  QThread m_ioThread;
  bool m_running = false;
  
  SubscriptionHandle m_controlSub = 0;
  SubscriptionHandle m_telemetrySub = 0;
  SubscriptionHandle m_errorSub = 0;
  SubscriptionHandle m_latencySub = 0;
};
