#pragma once
#include "../core/performancemonitor.h"

#include <QJsonObject>
#include <QObject>
#include <QTimer>

class SafetyMonitorService;

/**
 * 诊断数据收集服务（《客户端架构设计》§7.3）。
 * 定期收集系统指标，并将诊断快照发送到管理后端（通过 DIAGNOSTIC 通道）。
 */
class DiagnosticsService : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY(DiagnosticsService)

 public:
  explicit DiagnosticsService(PerformanceMonitor* perf, QObject* parent = nullptr);

  void setSafetyMonitor(SafetyMonitorService* safety) { m_safety = safety; }

  void start(int intervalMs = 5000);
  void stop();

  QJsonObject buildSnapshot() const;

 signals:
  void diagnosticsAvailable(const QJsonObject& snapshot);

 private slots:
  void collect();

 private:
  PerformanceMonitor* m_perfMonitor;
  SafetyMonitorService* m_safety;
  QTimer m_timer;
};
