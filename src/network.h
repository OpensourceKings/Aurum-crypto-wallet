#pragma once
#include "core.h"
#include "electrum.h"
#include "evm.h"
#include <QDateTime>
#include <QHash>
#include <QJsonArray>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QObject>
#include <QQueue>
#include <QTimer>
#include <QUrl>
#include <QtWebSockets/QWebSocket>
#include <array>
#include <functional>

namespace aurum {
struct EndpointSettings {
  QString bitcoin = "https://blockstream.info/api";
  QString litecoin = "ssl://electrum1.cipig.net:20063";
  QString solana = "https://api.mainnet-beta.solana.com";
  QString solanaWs = "wss://api.mainnet-beta.solana.com";
  QString ethereum = "https://ethereum-rpc.publicnode.com";
  QString polygon = "https://polygon-bor-rpc.publicnode.com";
  QString bnb = "https://bsc-rpc.publicnode.com";
};
struct AccountPlan {
  EvmTransfer evm;
  QByteArray message;
  quint64 fee = 0, rent = 0;
};
struct AccountState {
  QString address;
  quint64 balance = 0;
  QString units = "0";
  qint64 pending = 0;
  bool known = false;
  bool refreshing = false;
  QString status = "Not connected";
  QDateTime updated;
};
class Network : public QObject {
  Q_OBJECT
public:
  using Callback = std::function<void(QByteArray, QString)>;
  using JsonCallback = std::function<void(QJsonValue, QString)>;
  explicit Network(QObject *parent = nullptr);
  void setEndpoints(const EndpointSettings &settings);
  EndpointSettings endpoints() const { return endpoints_; }
  void setAddresses(const std::array<QString, AssetCount> &addresses);
  void setExternalState(Chain chain, const AccountState &state);
  void refresh();
  void pause();
  QString connectionStatus(Chain chain) const;
  const AccountState &state(Chain c) const {
    return states_[static_cast<int>(c)];
  }
  void
  prepareAccountTransfer(Chain, const QString &recipient, const QString &units,
                         std::function<void(AccountPlan, QString)> callback);
  void verifiedUtxos(Chain chain,
                     std::function<void(QVector<Utxo>, QString)> callback);
  void evmRpc(Chain chain, const QString &method, const QJsonArray &params,
              JsonCallback callback);
  void solRpc(const QString &method, const QJsonArray &params,
              JsonCallback callback) {
    rpc(method, params, callback);
  }
  void latestBlockhash(JsonCallback callback);
  void feeForMessage(const QByteArray &message, JsonCallback callback);
  void simulate(const SignedTransaction &tx, JsonCallback callback);
  void broadcast(Chain chain, const SignedTransaction &tx,
                 JsonCallback callback);
  void history(Chain chain, JsonCallback callback);
  static bool validEndpoint(const QString &text, bool websocket = false);
signals:
  void changed();

private:
  struct Job {
    QUrl url;
    QByteArray body;
    bool post = false;
    Callback callback;
  };
  QNetworkAccessManager manager_;
  QWebSocket socket_;
  ElectrumClient ltcElectrum_;
  QTimer refreshTimer_, pumpTimer_, reconnectTimer_;
  EndpointSettings endpoints_;
  std::array<AccountState, AssetCount> states_;
  QQueue<Job> jobs_;
  QHash<QString, qint64> nextRequest_, cooldown_;
  int active_ = 0;
  quint64 generation_ = 0;
  bool enabled_ = false;
  bool subscribed_ = false;
  bool solanaVerified_ = false;
  std::array<bool, 2> utxoVerified_{};
  void verifyUtxoNetwork(Chain, std::function<void(QString)>);
  void utxoGet(Chain, const QString &path, JsonCallback);
  void electrumGet(const QString &path, JsonCallback);
  bool usesElectrum(Chain c) const;
  void pump();
  void request(QUrl url, QByteArray body, bool post, Callback callback);
  void jsonGet(const QString &url, JsonCallback callback);
  void rpc(const QString &method, const QJsonArray &params,
           JsonCallback callback);
  QString rest(Chain chain) const;
  void connectSubscription();
  void refreshAccount(Chain chain);
};
} // namespace aurum
