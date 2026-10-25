#include "wallet_profiles.h"

#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUuid>
#include <stdexcept>

namespace aurum {
namespace {
constexpr auto PrimaryId = "primary";
constexpr auto ProfileIdsKey = "wallets/ids";
constexpr auto ActiveIdKey = "wallets/active";
constexpr auto ProfileNamesKey = "wallets/names";

void requireDirectory(const QString &path) {
  if (!QDir().mkpath(path) ||
      !QFile::setPermissions(path, QFileDevice::ReadOwner |
                                       QFileDevice::WriteOwner |
                                       QFileDevice::ExeOwner))
    throw std::runtime_error("Cannot secure wallet profile directory");
}
} // namespace

WalletProfiles::WalletProfiles(QString dataDirectory, QString settingsFile) {
  dataDirectory_ = dataDirectory.isEmpty()
                       ? QStandardPaths::writableLocation(
                             QStandardPaths::AppDataLocation)
                       : QFileInfo(dataDirectory).absoluteFilePath();
  if (dataDirectory_.isEmpty())
    throw std::runtime_error("Wallet data directory is unavailable");
  if (settingsFile.isEmpty())
    settings_ = std::make_unique<QSettings>();
  else
    settings_ = std::make_unique<QSettings>(settingsFile, QSettings::IniFormat);
}

bool WalletProfiles::validId(const QString &id) {
  static const QRegularExpression uuid(
      "^[0-9a-f]{8}-[0-9a-f]{4}-[1-8][0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$");
  return id == QLatin1String(PrimaryId) || uuid.match(id).hasMatch();
}

QString WalletProfiles::cleanName(const QString &name) {
  const QString clean = name.simplified();
  if (clean.isEmpty() || clean.size() > 40 ||
      clean.contains(QChar::Null))
    throw std::runtime_error("Wallet name must be between 1 and 40 characters");
  return clean;
}

bool WalletProfiles::contains(const QString &id) const {
  if (id == QLatin1String(PrimaryId))
    return true;
  if (!validId(id))
    return false;
  return settings_->value(ProfileIdsKey).toStringList().contains(id);
}

QVector<WalletProfile> WalletProfiles::all() const {
  QStringList ids{QString::fromLatin1(PrimaryId)};
  const auto configured = settings_->value(ProfileIdsKey).toStringList();
  for (const auto &id : configured)
    if (id != QLatin1String(PrimaryId) && validId(id) && !ids.contains(id))
      ids.push_back(id);

  QVector<WalletProfile> result;
  result.reserve(ids.size());
  for (const auto &id : ids)
    result.push_back(get(id));
  return result;
}

WalletProfile WalletProfiles::get(const QString &id) const {
  if (!contains(id))
    throw std::runtime_error("Wallet profile does not exist");
  const QString name =
      id == QLatin1String(PrimaryId)
          ? QStringLiteral("Main wallet")
          : settings_->value(QString::fromLatin1(ProfileNamesKey) + "/" + id,
                             QStringLiteral("Wallet"))
                .toString();
  const QString base = id == QLatin1String(PrimaryId)
                           ? dataDirectory_
                           : QDir(dataDirectory_).filePath("wallets/" + id);
  return {id, name, QDir(base).filePath("wallet.aurum"),
          id == QLatin1String(PrimaryId)
              ? QDir(dataDirectory_).filePath("monero")
              : QDir(base).filePath("monero")};
}

QString WalletProfiles::activeId() const {
  const auto id = settings_->value(ActiveIdKey, PrimaryId).toString();
  return contains(id) ? id : QString::fromLatin1(PrimaryId);
}

WalletProfile WalletProfiles::active() const { return get(activeId()); }

WalletProfile WalletProfiles::add(const QString &name) {
  const QString clean = cleanName(name);
  QString id;
  do {
    id = QUuid::createUuid().toString(QUuid::WithoutBraces).toLower();
  } while (contains(id));

  const auto profileDirectory = QDir(dataDirectory_).filePath("wallets/" + id);
  requireDirectory(QDir(dataDirectory_).filePath("wallets"));
  requireDirectory(profileDirectory);
  auto ids = settings_->value(ProfileIdsKey).toStringList();
  ids.push_back(id);
  settings_->setValue(ProfileIdsKey, ids);
  settings_->setValue(QString::fromLatin1(ProfileNamesKey) + "/" + id, clean);
  settings_->sync();
  if (settings_->status() != QSettings::NoError) {
    QDir(profileDirectory).removeRecursively();
    throw std::runtime_error("Could not save wallet profile list");
  }
  return get(id);
}

void WalletProfiles::setActive(const QString &id) {
  if (!contains(id))
    throw std::runtime_error("Wallet profile does not exist");
  settings_->setValue(ActiveIdKey, id);
  settings_->sync();
  if (settings_->status() != QSettings::NoError)
    throw std::runtime_error("Could not save active wallet profile");
}

} // namespace aurum
