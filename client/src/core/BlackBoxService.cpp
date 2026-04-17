#include "BlackBoxService.h"
#include <QDateTime>
#include <QDir>
#include <QDebug>
#include "TimeUtils.h"

BlackBoxService::BlackBoxService(QObject* parent) : QObject(parent) {}

BlackBoxService::~BlackBoxService() { stop(); }

bool BlackBoxService::start(const QString& logDir) {
  if (m_running) return true;

  QDir dir(logDir);
  if (!dir.exists()) {
    if (!dir.mkpath(".")) {
      qWarning() << "[Client][BlackBox] Failed to create log directory:" << logDir;
      return false;
    }
  }

  QString fileName = QString("%1/blackbox_%2.bin")
                         .arg(logDir)
                         .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
  m_file.setFileName(fileName);
  
  if (!m_file.open(QIODevice::WriteOnly | QIODevice::Append)) {
    qWarning() << "[Client][BlackBox] Failed to open log file:" << fileName;
    return false;
  }

  writeHeader();
  setupSubscriptions();
  
  m_running = true;
  qInfo() << "[Client][BlackBox] Started recording to" << fileName;
  return true;
}

void BlackBoxService::stop() {
  if (!m_running) return;
  
  EventBus::instance().unsubscribe(m_controlSub);
  EventBus::instance().unsubscribe(m_telemetrySub);
  EventBus::instance().unsubscribe(m_errorSub);
  EventBus::instance().unsubscribe(m_latencySub);
  
  m_file.flush();
  m_file.close();
  m_running = false;
  qInfo() << "[Client][BlackBox] Stopped recording";
}

void BlackBoxService::writeHeader() {
  // Simple header: Magic + Version
  const char magic[] = "TBBX";
  const uint32_t version = 1;
  m_file.write(magic, 4);
  m_file.write(reinterpret_cast<const char*>(&version), 4);
}

void BlackBoxService::setupSubscriptions() {
  m_controlSub = EventBus::instance().subscribe<VehicleControlEvent>(
      [this](const VehicleControlEvent& e) {
        struct {
          int64_t ts;
          float steering;
          float throttle;
          float brake;
          int8_t gear;
        } rec = {TimeUtils::nowUs(), (float)e.steeringAngle, (float)e.throttle, (float)e.brake, (int8_t)e.gear};
        writeRecord(VehicleControlEvent::TYPE_ID, rec);
      });

  m_telemetrySub = EventBus::instance().subscribe<TelemetryUpdateEvent>(
      [this](const TelemetryUpdateEvent& e) {
        struct {
          int64_t ts;
          float speed;
          float battery;
        } rec = {TimeUtils::nowUs(), (float)e.speed, (float)e.battery};
        writeRecord(TelemetryUpdateEvent::TYPE_ID, rec);
      });

  m_errorSub = EventBus::instance().subscribe<SystemErrorEvent>(
      [this](const SystemErrorEvent& e) {
        // 对于错误，记录关键信息
        struct {
          int64_t ts;
          uint32_t severity;
        } rec = {TimeUtils::nowUs(), (uint32_t)e.severity};
        writeRecord(SystemErrorEvent::TYPE_ID, rec);
      });
}

template<typename T>
void BlackBoxService::writeRecord(uint32_t typeId, const T& data) {
  if (!m_running) return;
  
  // Record format: [TypeID(4)][Size(4)][Payload(N)]
  const uint32_t size = sizeof(T);
  m_file.write(reinterpret_cast<const char*>(&typeId), 4);
  m_file.write(reinterpret_cast<const char*>(&size), 4);
  m_file.write(reinterpret_cast<const char*>(&data), size);
  
  // 对于关键控制指令，强制落盘
  if (typeId == VehicleControlEvent::TYPE_ID) {
    m_file.flush();
  }
}
