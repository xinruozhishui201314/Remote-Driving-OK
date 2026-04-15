#pragma once
#include "errorregistry.h"

#include <QObject>
#include <QString>
#include <QVariant>

#include <cmath>
#include <type_traits>

/**
 * 模型基类（提供统一的边界检查和异常处理）
 *
 * 《客户端架构设计》§7 产品化增强
 *
 * 特性：
 * - 统一的范围检查（double, int）
 * - NaN 检测与处理
 * - 非空字符串验证
 * - 统一的错误上报
 *
 * 使用方式：
 *   class VehicleModel : public ModelBase {
 *       Q_OBJECT
 *   public:
 *       explicit VehicleModel(QObject* parent = nullptr) : ModelBase(parent) {}
 *
 *       void setSpeed(double speed) {
 *           m_speed = clamp(speed, 0.0, 200.0, "speed");
 *       }
 *   };
 */
class ModelBase : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY(ModelBase)

 protected:
  explicit ModelBase(QObject* parent = nullptr) : QObject(parent) {}

  ~ModelBase() override = default;

  // ─────────────────────────────────────────────────────────────────
  // 范围检查 - double
  // ─────────────────────────────────────────────────────────────────

  /**
   * double 值范围检查
   * @param value 输入值
   * @param min 最小值
   * @param max 最大值
   * @param name 字段名称（用于日志）
   * @param clamp 是否自动 clamp 到边界（true），还是触发错误（false）
   * @return 有效值（可能已被 clamp）
   */
  double clamp(double value, double min, double max, const char* name, bool doClamp = true) {
    if (std::isnan(value)) {
      reportError(ErrorRegistry::Category::System, QString("NaN detected for %1").arg(name),
                  ErrorRegistry::Level::Warn, metaObject()->className());
      return 0.0;
    }
    if (std::isinf(value)) {
      reportError(ErrorRegistry::Category::System, QString("Inf detected for %1").arg(name),
                  ErrorRegistry::Level::Warn, metaObject()->className());
      return 0.0;
    }
    if (value < min || value > max) {
      if (doClamp) {
        const double clamped = qBound(min, value, max);
        qWarning().nospace() << "[" << metaObject()->className() << "] " << name
                             << " clamped: " << value << " -> " << clamped;
        return clamped;
      } else {
        reportError(
            ErrorRegistry::Category::System,
            QString("%1 out of range: %2 (valid: [%3, %4])").arg(name).arg(value).arg(min).arg(max),
            ErrorRegistry::Level::Error, metaObject()->className());
      }
    }
    return value;
  }

  // ─────────────────────────────────────────────────────────────────
  // 范围检查 - float
  // ─────────────────────────────────────────────────────────────────

  float clamp(float value, float min, float max, const char* name, bool doClamp = true) {
    return static_cast<float>(clamp(static_cast<double>(value), static_cast<double>(min),
                                    static_cast<double>(max), name, doClamp));
  }

  // ─────────────────────────────────────────────────────────────────
  // 范围检查 - int
  // ─────────────────────────────────────────────────────────────────

  int clampInt(int value, int min, int max, const char* name, bool doClamp = true) {
    if (value < min || value > max) {
      if (doClamp) {
        const int clamped = qBound(min, value, max);
        qWarning().nospace() << "[" << metaObject()->className() << "] " << name
                             << " (int) clamped: " << value << " -> " << clamped;
        return clamped;
      } else {
        reportError(ErrorRegistry::Category::System,
                    QString("%1 (int) out of range: %2 (valid: [%3, %4])")
                        .arg(name)
                        .arg(value)
                        .arg(min)
                        .arg(max),
                    ErrorRegistry::Level::Error, metaObject()->className());
      }
    }
    return value;
  }

  // ─────────────────────────────────────────────────────────────────
  // 范围检查 - unsigned int
  // ─────────────────────────────────────────────────────────────────

  unsigned int clampUInt(unsigned int value, unsigned int min, unsigned int max, const char* name,
                         bool doClamp = true) {
    if (value < min || value > max) {
      if (doClamp) {
        const unsigned int clamped = qBound(min, value, max);
        qWarning().nospace() << "[" << metaObject()->className() << "] " << name
                             << " (uint) clamped: " << value << " -> " << clamped;
        return clamped;
      } else {
        reportError(ErrorRegistry::Category::System,
                    QString("%1 (uint) out of range: %2 (valid: [%3, %4])")
                        .arg(name)
                        .arg(value)
                        .arg(min)
                        .arg(max),
                    ErrorRegistry::Level::Error, metaObject()->className());
      }
    }
    return value;
  }

  // ─────────────────────────────────────────────────────────────────
  // 范围检查 - qint64
  // ─────────────────────────────────────────────────────────────────

  qint64 clampInt64(qint64 value, qint64 min, qint64 max, const char* name, bool doClamp = true) {
    if (value < min || value > max) {
      if (doClamp) {
        const qint64 clamped = qBound(min, value, max);
        qWarning().nospace() << "[" << metaObject()->className() << "] " << name
                             << " (int64) clamped: " << value << " -> " << clamped;
        return clamped;
      } else {
        reportError(ErrorRegistry::Category::System,
                    QString("%1 (int64) out of range: %2 (valid: [%3, %4])")
                        .arg(name)
                        .arg(value)
                        .arg(min)
                        .arg(max),
                    ErrorRegistry::Level::Error, metaObject()->className());
      }
    }
    return value;
  }

  // ─────────────────────────────────────────────────────────────────
  // 非空检查
  // ─────────────────────────────────────────────────────────────────

  /**
   * 非空字符串检查
   * @param value 输入字符串
   * @param name 字段名称（用于日志）
   * @return 有效字符串（空时返回空字符串并上报错误）
   */
  QString requireNonEmpty(const QString& value, const char* name) {
    if (value.isEmpty()) {
      reportError(ErrorRegistry::Category::System, QString("%1 is empty").arg(name),
                  ErrorRegistry::Level::Error, metaObject()->className());
      return QString();
    }
    return value;
  }

  // ─────────────────────────────────────────────────────────────────
  // 指针检查
  // ─────────────────────────────────────────────────────────────────

  /**
   * 非空指针检查
   * @param ptr 输入指针
   * @param name 指针名称（用于日志）
   * @return 原始指针（空时上报错误并返回 nullptr）
   */
  template <typename T>
  T* requireNonNull(T* ptr, const char* name) {
    if (ptr == nullptr) {
      reportError(ErrorRegistry::Category::System, QString("%1 is null").arg(name),
                  ErrorRegistry::Level::Error, metaObject()->className());
    }
    return ptr;
  }

  // ─────────────────────────────────────────────────────────────────
  // 错误上报
  // ─────────────────────────────────────────────────────────────────

  void reportError(ErrorRegistry::Category category, const QString& message,
                   ErrorRegistry::Level level, const QString& component) {
    ErrorRegistry::instance().report(category, message, level, component);
  }

  // ─────────────────────────────────────────────────────────────────
  // 便捷模板方法
  // ─────────────────────────────────────────────────────────────────

  /**
   * 设置值（带范围检查）
   * @param target 目标变量引用
   * @param value 输入值
   * @param min 最小值
   * @param max 最大值
   * @param name 字段名称
   */
  template <typename T>
  void setClamped(T& target, T value, T min, T max, const char* name) {
    static_assert(std::is_arithmetic_v<T>, "T must be arithmetic type");
    target = clampInternal(value, min, max, name);
  }

 private:
  template <typename T>
  T clampInternal(T value, T min, T max, const char* name) {
    if constexpr (std::is_floating_point_v<T>) {
      if (std::isnan(value)) {
        reportError(ErrorRegistry::Category::System, QString("NaN detected for %1").arg(name),
                    ErrorRegistry::Level::Warn, metaObject()->className());
        return T{0};
      }
    }
    if (value < min || value > max) {
      const T clamped = qBound(min, value, max);
      qWarning().nospace() << "[" << metaObject()->className() << "] " << name
                           << " clamped: " << value << " -> " << clamped;
      return clamped;
    }
    return value;
  }
};
