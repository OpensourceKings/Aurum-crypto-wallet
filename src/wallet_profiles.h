#pragma once

#include <QSettings>
#include <QString>
#include <QVector>
#include <memory>

namespace aurum {

struct WalletProfile {
  QString id;
  QString name;
  QString vaultPath;
  QString moneroDirectory;
};

// Keeps only profile names and paths. Every profile has an independent,
// password-encrypted vault; the original primary paths remain unchanged.
class WalletProfiles final {
public:
  explicit WalletProfiles(QString dataDirectory = {}, QString settingsFile = {});

  QVector<WalletProfile> all() const;
  WalletProfile get(const QString &id) const;
  WalletProfile active() const;
  QString activeId() const;
  WalletProfile add(const QString &name);
  void setActive(const QString &id);

private:
  QString dataDirectory_;
  std::unique_ptr<QSettings> settings_;

  bool contains(const QString &id) const;
  static bool validId(const QString &id);
  static QString cleanName(const QString &name);
};

} // namespace aurum
