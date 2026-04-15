#include "keystoremanager.h"

#include "core/logger.h"

#include <QCoreApplication>
#include <QDebug>
#include <QMutex>
#include <QMutexLocker>
#include <QSettings>
#include <QStandardPaths>

// ═══════════════════════════════════════════════════════════════════════════════
// KeystoreManager 实现 — QSettings fallback（libsecret 动态加载暂不可用）
//
// libsecret 需安装 libsecret-1-dev 并通过 pkg-config 链接，当前版本
// 采用 dlsym 动态加载方式复杂度高。后续可通过 CMake FindPkgConfig
// 集成 libsecret，届时重新启用安全存储路径。
// ═══════════════════════════════════════════════════════════════════════════════

KeystoreManager::KeystoreManager(QObject* parent)
    : QObject(parent), m_serviceName(QStringLiteral("RemoteDrivingClient")) {
  qDebug() << "[KeystoreManager] Initialized (QSettings fallback)";
}

KeystoreManager::~KeystoreManager() = default;

KeystoreManager& KeystoreManager::instance() {
  static KeystoreManager manager;
  return manager;
}

bool KeystoreManager::isSecureStorageAvailable() const {
  // QSettings 始终可用
  return true;
}

bool KeystoreManager::storeToken(const QString& key, const QString& token) {
  if (key.isEmpty() || token.isEmpty()) {
    qWarning() << "[KeystoreManager] storeToken: skipped empty key or token";
    return false;
  }

  qDebug().noquote() << "[KeystoreManager] Storing token: key=" << key
                     << " tokenLen=" << token.size();

  if (storeWithQSettings(key, token)) {
    emit tokenStored(key);
    return true;
  }

  QString err = QString("Failed to store token: %1").arg(key);
  qCritical() << "[KeystoreManager]" << err;
  emit error(err);
  return false;
}

QString KeystoreManager::getToken(const QString& key) const {
  if (key.isEmpty()) {
    return QString();
  }
  return getFromQSettings(key);
}

bool KeystoreManager::deleteToken(const QString& key) {
  if (key.isEmpty()) {
    return false;
  }

  qDebug().noquote() << "[KeystoreManager] Deleting token: key=" << key;

  if (deleteFromQSettings(key)) {
    emit tokenDeleted(key);
    return true;
  }
  return false;
}

QStringList KeystoreManager::storedKeys() const {
  QSettings settings;
  settings.beginGroup("keystore");
  QStringList keys = settings.childKeys();
  settings.endGroup();

  qDebug() << "[KeystoreManager] storedKeys: count=" << keys.size();
  return keys;
}

void KeystoreManager::clearAll() {
  qDebug() << "[KeystoreManager] clearAll: clearing all stored tokens";

  QStringList keys = storedKeys();
  for (const QString& key : keys) {
    deleteToken(key);
  }

  QSettings settings;
  settings.beginGroup("keystore");
  settings.remove("");
  settings.endGroup();

  qDebug() << "[KeystoreManager] clearAll: done";
}

// ═══════════════════════════════════════════════════════════════════════════════
// QSettings fallback 实现
// ═══════════════════════════════════════════════════════════════════════════════

bool KeystoreManager::storeWithQSettings(const QString& key, const QString& value) {
  QSettings settings;
  settings.beginGroup("keystore");
  settings.setValue(key, value);
  settings.endGroup();
  return true;
}

QString KeystoreManager::getFromQSettings(const QString& key) const {
  QSettings settings;
  settings.beginGroup("keystore");
  QString value = settings.value(key).toString();
  settings.endGroup();
  return value;
}

bool KeystoreManager::deleteFromQSettings(const QString& key) {
  QSettings settings;
  settings.beginGroup("keystore");
  settings.remove(key);
  settings.endGroup();
  return true;
}

// ═══════════════════════════════════════════════════════════════════════════════
// 未使用的占位桩（保留接口契约，后续集成 libsecret 时填充）
// ═══════════════════════════════════════════════════════════════════════════════

bool KeystoreManager::storeWithLibsecret(const QString& key, const QString& value) {
  Q_UNUSED(key);
  Q_UNUSED(value);
  return false;
}

QString KeystoreManager::getFromLibsecret(const QString& key) const {
  Q_UNUSED(key);
  return QString();
}

bool KeystoreManager::deleteFromLibsecret(const QString& key) {
  Q_UNUSED(key);
  return false;
}

bool KeystoreManager::storeWithQtSecure(const QString& key, const QString& value) {
  return storeWithQSettings(key, value);
}

QString KeystoreManager::getFromQtSecure(const QString& key) const { return getFromQSettings(key); }

bool KeystoreManager::deleteFromQtSecure(const QString& key) { return deleteFromQSettings(key); }
