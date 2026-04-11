#pragma once
#include <QDateTime>
#include <QMutex>
#include <QMutexLocker>
#include <QObject>
#include <QString>
#include <QVector>

#include <atomic>
#include <cstdint>

/**
 * 错误注册表 - 产品化错误集中管理（《客户端架构设计》§7 产品化增强）
 *
 * 特性：
 * - 单例模式，全局唯一
 * - 按 Category 分类错误：Network, Video, Control, Auth, Session, Safety, System
 * - 按 Level 分级：Info, Warn, Error, Fatal
 * - 错误聚合统计：occurrenceCount 记录同类错误出现次数
 * - 线程安全：所有操作使用 QMutex 保护
 * - 有界存储：每类错误最多保留 MAX_ERRORS_PER_CATEGORY 条
 *
 * 使用方式：
 *   ErrorRegistry::instance().report(
 *       ErrorRegistry::Category::Network,
 *       "WebSocket 连接失败",
 *       ErrorRegistry::Level::Error,
 *       "WebRTCClient"
 *   );
 *
 * 获取错误摘要：
 *   auto summary = ErrorRegistry::instance().getErrorSummary();
 */
class ErrorRegistry : public QObject {
  Q_OBJECT

 public:
  // 错误分类
  enum class Category { Network, Video, Control, Auth, Session, Safety, System, Unknown };

  // 错误级别
  enum class Level { Info, Warn, Error, Fatal };

  // 单条错误结构
  struct Error {
    Category category = Category::Unknown;
    Level level = Level::Info;
    QString message;
    QString component;
    qint64 timestampMs = 0;
    int occurrenceCount = 1;
  };

  // 有界存储容量
  static constexpr int MAX_ERRORS_PER_CATEGORY = 100;
  static constexpr int MAX_TOTAL_ERRORS = 1000;

  static ErrorRegistry& instance() {
    static ErrorRegistry registry;
    return registry;
  }

  explicit ErrorRegistry(QObject* parent = nullptr);
  ~ErrorRegistry() override = default;

  // ─────────────────────────────────────────────────────────────────
  // 核心 API
  // ─────────────────────────────────────────────────────────────────

  /**
   * 上报错误
   * @param category 错误分类
   * @param message 错误消息
   * @param level 错误级别
   * @param component 组件名称
   * @return 错误 ID（可用于后续关联查询）
   */
  int report(Category category, const QString& message, Level level, const QString& component);

  /**
   * 获取指定分类的错误列表
   * @param category 错误分类（传空获取所有）
   * @param maxCount 最大返回数量（-1 表示全部）
   * @return 错误列表（按时间倒序）
   */
  QVector<Error> getErrors(Category category = Category::Unknown, int maxCount = -1) const;

  /**
   * 清除指定分类的错误
   * @param category 错误分类（传 Unknown 清除所有）
   */
  void clearErrors(Category category = Category::Unknown);

  /**
   * 获取错误摘要
   * @return 摘要信息（JSON 格式字符串）
   */
  QString getErrorSummary() const;

  // ─────────────────────────────────────────────────────────────────
  // 属性访问
  // ─────────────────────────────────────────────────────────────────

  int fatalErrors() const { return m_fatalErrors.load(); }
  int errorErrors() const { return m_errorErrors.load(); }
  int totalErrorCount() const { return m_totalErrorCount.load(); }

  // ─────────────────────────────────────────────────────────────────
  // 工具方法
  // ─────────────────────────────────────────────────────────────────

  static QString categoryToString(Category category);
  static QString levelToString(Level level);
  static Category stringToCategory(const QString& str);
  static Level stringToLevel(const QString& str);

 signals:
  void errorReported(const Error& error, int errorId);
  void fatalErrorsChanged(int count);
  void errorSummaryChanged(const QString& summary);

 private:
  Error makeError(Category category, const QString& message, Level level, const QString& component);
  void updateStats();
  void pruneOldErrors();
  void checkLevelDowngrade();

  mutable QMutex m_mutex;
  QVector<Error> m_errors;
  std::atomic<int> m_fatalErrors{0};
  std::atomic<int> m_errorErrors{0};
  std::atomic<int> m_totalErrorCount{0};
  int m_nextErrorId = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// Qt 便捷宏
// ─────────────────────────────────────────────────────────────────────────────

#define ERROR_INFO(cat, msg, comp)                                                                \
  ErrorRegistry::instance().report(ErrorRegistry::Category::cat, msg, ErrorRegistry::Level::Info, \
                                   comp)

#define ERROR_WARN(cat, msg, comp)                                                                \
  ErrorRegistry::instance().report(ErrorRegistry::Category::cat, msg, ErrorRegistry::Level::Warn, \
                                   comp)

#define ERROR_ERR(cat, msg, comp)                                                                  \
  ErrorRegistry::instance().report(ErrorRegistry::Category::cat, msg, ErrorRegistry::Level::Error, \
                                   comp)

#define ERROR_FATAL(cat, msg, comp)                                                                \
  ErrorRegistry::instance().report(ErrorRegistry::Category::cat, msg, ErrorRegistry::Level::Fatal, \
                                   comp)
