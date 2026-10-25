#pragma once

#include <QString>

namespace aurum {

// Small adapter for the official Mullvad client already installed on the
// host. Aurum never stores an account number or attempts to implement a VPN
// protocol; the Mullvad daemon owns keys, routing and its kill switch.
class MullvadClient final {
public:
  static bool available();
  static bool loginAndConnect(const QString &account, QString *error = nullptr);
  static bool disconnect(QString *error = nullptr);
  static bool connected(bool *known = nullptr);

private:
  static QString binary();
  static bool run(const QStringList &arguments, int timeoutMs,
                  QString *error = nullptr);
};

} // namespace aurum
