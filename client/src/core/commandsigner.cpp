#include "commandsigner.h"

#include <QCryptographicHash>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMessageAuthenticationCode>
#include <QStringList>

#include <algorithm>

// 参与签名的字段集合（有序，确保 canonical 序列确定性）
static const QStringList kSignedFields = {"steering", "throttle",       "brake",
                                          "gear",     "emergency_stop", "timestampMs",
                                          "seq",      "sessionId",      "vin"};

// ─── setCredentials ──────────────────────────────────────────────────────────

void CommandSigner::setCredentials(const QString& vin, const QString& sessionId,
                                   const QString& token) {
  m_signingKey = deriveKey(vin, sessionId, token);
  qInfo() << "[Client][CommandSigner] credentials set for vin=" << vin
          << "session=" << sessionId.left(8) << "...";
}

void CommandSigner::clearCredentials() {
  m_signingKey.fill(0);  // 安全清零
  m_signingKey.clear();
  qInfo() << "[Client][CommandSigner] credentials cleared";
}

// ─── sign ─────────────────────────────────────────────────────────────────────

bool CommandSigner::sign(QJsonObject& json) const {
  if (m_signingKey.isEmpty()) {
    qWarning() << "[Client][CommandSigner] sign() called without credentials";
    return false;
  }

  // 移除旧 hmac（避免重签时叠加）
  json.remove("hmac");

  const QByteArray payload = canonicalize(json);
  const QByteArray mac = computeHmac(payload);

  json["hmac"] = QString::fromLatin1(mac.toHex());
  return true;
}

// ─── verify ──────────────────────────────────────────────────────────────────

bool CommandSigner::verify(const QJsonObject& json, QString* reason) const {
  if (m_signingKey.isEmpty()) {
    if (reason)
      *reason = "no signing key";
    return false;
  }

  if (!json.contains("hmac")) {
    if (reason)
      *reason = "missing hmac field";
    qWarning() << "[Client][CommandSigner] received message without hmac";
    return false;
  }

  const QByteArray receivedMac = QByteArray::fromHex(json["hmac"].toString().toLatin1());

  // 重建不含 hmac 的对象并规范化
  QJsonObject withoutHmac = json;
  withoutHmac.remove("hmac");
  const QByteArray payload = canonicalize(withoutHmac);
  const QByteArray expected = computeHmac(payload);

  // 时间安全比较（恒定时间，防止时序攻击）
  if (receivedMac.size() != expected.size()) {
    if (reason)
      *reason = "hmac length mismatch";
    return false;
  }

  uint8_t diff = 0;
  for (int i = 0; i < expected.size(); ++i) {
    diff |= static_cast<uint8_t>(receivedMac[i]) ^ static_cast<uint8_t>(expected[i]);
  }

  if (diff != 0) {
    if (reason)
      *reason = "hmac mismatch";
    qWarning() << "[Client][CommandSigner] HMAC verification FAILED";
    return false;
  }

  return true;
}

// ─── computeHmac ─────────────────────────────────────────────────────────────

QByteArray CommandSigner::computeHmac(const QByteArray& payload) const {
  return QMessageAuthenticationCode::hash(payload, m_signingKey, QCryptographicHash::Sha256);
}

// ─── canonicalize ─────────────────────────────────────────────────────────────

QByteArray CommandSigner::canonicalize(const QJsonObject& json) {
  // 按 kSignedFields 固定顺序取字段，生成确定性字符串
  // 格式："field1=value1&field2=value2&..."，缺失字段用空字符串代替
  QString canonical;
  canonical.reserve(256);

  for (const QString& field : kSignedFields) {
    if (!canonical.isEmpty())
      canonical += '&';
    canonical += field + '=';
    if (json.contains(field)) {
      const QJsonValue v = json[field];
      if (v.isBool())
        canonical += v.toBool() ? "true" : "false";
      else if (v.isDouble())
        canonical += QString::number(v.toDouble(), 'f', 6);
      else
        canonical += v.toString();
    }
  }

  return canonical.toUtf8();
}

// ─── deriveKey ────────────────────────────────────────────────────────────────

QByteArray CommandSigner::deriveKey(const QString& vin, const QString& sessionId,
                                    const QString& token) {
  // HKDF 简化版：HMAC(token, vin | "|" | sessionId)
  // 足够强度，不引入额外依赖
  const QByteArray info = (vin + "|" + sessionId).toUtf8();
  return QMessageAuthenticationCode::hash(info, token.toUtf8(), QCryptographicHash::Sha256);
}
