#pragma once
#include <QObject>
#include <QString>
#include <QStringList>

/**
 * 安全密钥存储管理器
 *
 * 使用 libsecret (Linux Keychain) 或 Qt Credential Storage 存储敏感数据
 * 优先级：libsecret > Qt Secure Storage > QSettings fallback (with warning)
 */
class KeystoreManager : public QObject {
  Q_OBJECT
 public:
  static KeystoreManager& instance();

  // 存储令牌
  bool storeToken(const QString& key, const QString& token);

  // 获取令牌
  QString getToken(const QString& key) const;

  // 删除令牌
  bool deleteToken(const QString& key);

  // 检查是否支持安全存储
  bool isSecureStorageAvailable() const;

  // 列出所有存储的key
  QStringList storedKeys() const;

  // 清除所有存储的令牌
  void clearAll();

 signals:
  void tokenStored(const QString& key);
  void tokenDeleted(const QString& key);
  void error(const QString& message);

 private:
  explicit KeystoreManager(QObject* parent = nullptr);
  ~KeystoreManager() override;

  // 尝试使用 libsecret (Linux)
  bool storeWithLibsecret(const QString& key, const QString& value);
  QString getFromLibsecret(const QString& key) const;
  bool deleteFromLibsecret(const QString& key);

  // Fallback: Qt Secure Storage
  bool storeWithQtSecure(const QString& key, const QString& value);
  QString getFromQtSecure(const QString& key) const;
  bool deleteFromQtSecure(const QString& key);

  // Last resort: QSettings with encryption warning
  bool storeWithQSettings(const QString& key, const QString& value);
  QString getFromQSettings(const QString& key) const;
  bool deleteFromQSettings(const QString& key);

  QString m_serviceName = "RemoteDrivingClient";
};
