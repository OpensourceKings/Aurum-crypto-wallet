#include "mullvad.h"

#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>

namespace aurum {
namespace {
constexpr int AccountDigits = 16;
}

QString MullvadClient::binary() {
  // Prefer the root-owned system installation. Falling back to PATH keeps
  // the adapter usable on distributions that package it elsewhere.
  const QString system = "/usr/bin/mullvad";
  if (QFileInfo::exists(system) && QFileInfo(system).isExecutable())
    return system;
  return QStandardPaths::findExecutable("mullvad");
}

bool MullvadClient::available() { return !binary().isEmpty(); }

bool MullvadClient::run(const QStringList &arguments, int timeoutMs,
                        QString *error) {
  const auto executable = binary();
  if (executable.isEmpty()) {
    if (error)
      *error = "The official Mullvad client is not installed";
    return false;
  }
  QProcess process;
  process.setProgram(executable);
  process.setArguments(arguments);
  process.setProcessChannelMode(QProcess::SeparateChannels);
  process.start();
  if (!process.waitForStarted(3000)) {
    if (error)
      *error = "The Mullvad service could not be started";
    return false;
  }
  if (!process.waitForFinished(timeoutMs)) {
    process.kill();
    process.waitForFinished(1000);
    if (error)
      *error = "The Mullvad service did not respond in time";
    return false;
  }
  if (process.exitStatus() != QProcess::NormalExit ||
      process.exitCode() != 0) {
    if (error)
      *error = "The Mullvad service rejected the request";
    return false;
  }
  return true;
}

bool MullvadClient::loginAndConnect(const QString &account, QString *error) {
  const auto digits = account.simplified().remove(' ');
  static const QRegularExpression pattern(QString("^[0-9]{%1}$").arg(
      AccountDigits));
  if (!pattern.match(digits).hasMatch()) {
    if (error)
      *error = "Enter the 16-digit Mullvad account number";
    return false;
  }
  // The number is passed directly to the official client and is never put in
  // QSettings, logs, environment variables or a child command line retained
  // by Aurum after this call returns.
  if (!run({"account", "login", digits}, 15000, error))
    return false;
  return run({"connect", "--wait"}, 30000, error);
}

bool MullvadClient::disconnect(QString *error) {
  return run({"disconnect"}, 10000, error);
}

bool MullvadClient::connected(bool *known) {
  if (known)
    *known = false;
  const auto executable = binary();
  if (executable.isEmpty())
    return false;
  QProcess process;
  process.setProgram(executable);
  process.setArguments({"status", "--json"});
  process.setProcessChannelMode(QProcess::SeparateChannels);
  process.start();
  if (!process.waitForStarted(3000) || !process.waitForFinished(5000) ||
      process.exitStatus() != QProcess::NormalExit ||
      process.exitCode() != 0)
    return false;
  const auto json = QJsonDocument::fromJson(process.readAllStandardOutput());
  const auto state = json.object().value("state").toString();
  if (known)
    *known = !state.isEmpty();
  return state == "connected";
}

} // namespace aurum
