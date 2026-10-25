#pragma once

#include "network.h"
#include <QNetworkAccessManager>
#include <QTimer>
#ifdef _MSC_VER
using pid_t = int;
#else
#include <sys/types.h>
#endif
#include <functional>

namespace aurum {

// Thin, local-only adapter for the upstream Monero wallet RPC. The Monero
// wallet engine owns the XMR spend/view keys; Aurum never implements a second
// transaction format or attempts to re-create Monero's ring construction.
class MoneroClient final : public QObject {
  Q_OBJECT
public:
  using Callback = std::function<void(QString)>;
  using TransferCallback = std::function<void(QString, QString)>;

  explicit MoneroClient(QObject *parent = nullptr);
  ~MoneroClient() override;

  void setNode(const QString &endpoint, const QString &proxy = {});
  void setWalletDirectory(const QString &directory);
  void setRpcBinary(const QString &path);
  QString node() const { return node_; }
  QString proxy() const { return proxy_; }
  QString walletDirectory() const { return walletDir_; }
  QString walletFile() const { return walletDir_ + "/aurum-xmr"; }
  bool running() const { return pid_ > 0; }
  const AccountState &state() const { return state_; }

  // The supplied password is consumed and cleared. It is used only to open
  // or create the encrypted Monero wallet and is not retained by Aurum.
  void start(QByteArray password, Callback callback);
  void stop();
  void refresh();
  void transfer(const QString &recipient, const QString &amount,
                QByteArray password, TransferCallback callback);

signals:
  void changed();

private:
  QNetworkAccessManager manager_;
  QTimer refreshTimer_;
  AccountState state_;
  // Public restricted RPC exposed as HTTP on a Tor onion service. The local
  // SOCKS proxy keeps the daemon route off clearnet.
  QString node_ =
      "http://un4yrhwq4d53caoiaadeiur5e5wgkgp74zw3p3twqh3nxh6ztz347dad.onion:18081";
  QString proxy_ = "127.0.0.1:9050";
  QString walletDir_;
  QString rpcBinary_;
  QString daemonBinary_;
  QByteArray rpcUser_;
  QByteArray rpcPassword_;
  quint16 port_ = 0;
  pid_t pid_ = -1;
  pid_t daemonPid_ = -1;
  quint64 generation_ = 0;
  bool opening_ = false;
  bool polling_ = false, synchronized_ = false;
  quint64 unlockedBalance_ = 0;

  void rpc(const QString &method, const QJsonObject &params,
           std::function<void(QJsonObject, QString)> callback);
  void waitForRpc(int attempt, Callback callback);
  void openOrCreate(QByteArray password, Callback callback);
  void openWallet(QByteArray password, Callback callback);
  void updateAddress(Callback callback);
  void refreshBalance(Callback callback);
  void reportError(const QString &error);
  bool spawnDaemon(QString *error);
  void stopDaemon();
  bool spawn(QString *error);
};

} // namespace aurum
