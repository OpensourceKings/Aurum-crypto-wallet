#pragma once
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QUrl>
namespace aurum {
struct SwapRate {
  QString tradeId, provider, kycRating;
  double amountFrom = 0, amountTo = 0;
};
struct SwapTrade {
  QString id, status, depositAddress, payoutAddress, memo;
  double amountFrom = 0, amountTo = 0;
};
// Trocador (https://trocador.app) swap client. Only public swap metadata and
// the user's payout/refund addresses are sent; no keys or signing data.
// The endpoint paths follow Trocador's public API as known to the author; the
// docs site sits behind a browser check, so verify against a live key.
class SwapService : public QObject {
  Q_OBJECT
public:
  explicit SwapService(QObject *parent = nullptr);
  void setApiKey(const QString &key) { apiKey_ = key.trimmed(); }
  bool hasApiKey() const { return !apiKey_.isEmpty(); }
  static QString envApiKey();
  void requestRate(const QString &fromTicker, const QString &fromNetwork,
                   const QString &toTicker, const QString &toNetwork,
                   double amount);
  void createTrade(const SwapRate &rate, const QString &payoutAddress,
                   const QString &refundAddress);
  void pollTrade(const QString &id);
  void cancel();
  static SwapRate parseRate(const QByteArray &);
  static SwapTrade parseTrade(const QByteArray &);
signals:
  void rateReady(SwapRate);
  void tradeReady(SwapTrade);
  void failed(QString);

private:
  QNetworkAccessManager manager_;
  QString apiKey_;
  QUrl base_{"https://api.trocador.app"};
  void get(const QString &path, std::function<void(QByteArray)> done);
};
} // namespace aurum
