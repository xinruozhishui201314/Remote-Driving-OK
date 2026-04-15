#pragma once
#include <QObject>

/**
 * 安全状态 MVVM 模型（《客户端架构设计》§3.4.2）。
 */
class SafetyStatusModel : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY(SafetyStatusModel)

  Q_PROPERTY(bool allSafe READ allSafe NOTIFY safetyChanged)
  Q_PROPERTY(bool emergencyStop READ emergencyStop NOTIFY emergencyStopChanged)
  Q_PROPERTY(bool deadmanActive READ deadmanActive NOTIFY deadmanChanged)
  Q_PROPERTY(int warningCount READ warningCount NOTIFY safetyChanged)
  Q_PROPERTY(QString lastWarning READ lastWarning NOTIFY safetyChanged)
  Q_PROPERTY(QString systemState READ systemState NOTIFY systemStateChanged)
  Q_PROPERTY(double latencyMs READ latencyMs NOTIFY safetyChanged)
  Q_PROPERTY(bool latencyWarning READ latencyWarning NOTIFY safetyChanged)

 public:
  explicit SafetyStatusModel(QObject* parent = nullptr);

  bool allSafe() const { return m_allSafe; }
  bool emergencyStop() const { return m_emergencyStop; }
  bool deadmanActive() const { return m_deadmanActive; }
  int warningCount() const { return m_warningCount; }
  QString lastWarning() const { return m_lastWarning; }
  QString systemState() const { return m_systemState; }
  double latencyMs() const { return m_latencyMs; }
  bool latencyWarning() const { return m_latencyMs > 100.0; }

  void setAllSafe(bool safe);
  void setEmergencyStop(bool active, const QString& reason = {});
  void setDeadmanActive(bool active);
  void addWarning(const QString& message);
  void clearWarnings();
  void setSystemState(const QString& state);
  void setLatencyMs(double ms);

 signals:
  void safetyChanged();
  void emergencyStopChanged(bool active);
  void deadmanChanged(bool active);
  void systemStateChanged(const QString& state);

 private:
  bool m_allSafe = true;
  bool m_emergencyStop = false;
  bool m_deadmanActive = false;
  int m_warningCount = 0;
  QString m_lastWarning;
  QString m_systemState = "IDLE";
  double m_latencyMs = 0.0;
};
