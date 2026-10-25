#pragma once
#include "assets.h"
#include <QDateTime>
#include <QMap>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QPointF>
#include <QTimer>
#include <QUrl>
#include <array>
namespace aurum {
struct MarketQuote {
  double usd = 0, change24h = 0;
  bool hasChange = false;
  QDateTime updated;
  bool fresh(const QDateTime &now = QDateTime::currentDateTimeUtc()) const;
};
// Display-only market data. No addresses or signing data enter this service.
class MarketData : public QObject {
  Q_OBJECT
public:
  explicit MarketData(QObject *parent = nullptr);
  void start();
  void stop();
  void refresh();
  // Base URL; production uses Exodus's anonymous pricing service.
  void setEndpoint(const QUrl &);
  const MarketQuote &quote(Chain c) const { return quotes_[int(c)]; }
  QString status() const { return status_; }
  void requestHistory(Chain, int days);
  QVector<QPointF> history(Chain, int days) const;
  QString historyStatus() const;
  static QVector<QPointF> parseHistory(const QByteArray &, Chain);
  static QString priceSymbol(Chain);
  static QStringList priceSymbols();
  static std::array<MarketQuote, AssetCount>
  parse(const QByteArray &prices, const QByteArray &ticker,
        const QDateTime &updated = QDateTime::currentDateTimeUtc(),
        const QDateTime &now = QDateTime::currentDateTimeUtc());
signals:
  void changed();

private:
  QNetworkAccessManager manager_;
  QTimer timer_;
  QUrl endpoint_{"https://pricing.a.exodus.io"};
  std::array<MarketQuote, AssetCount> quotes_{};
  QString status_ = "Prices unavailable";
  bool active_ = false, pending_ = false;
  qint64 nextRequest_ = 0;
  int backoffSeconds_ = 60;
  quint64 generation_ = 0;
  struct HistoryEntry {
    QVector<QPointF> points;
    qint64 period = 0;
  };
  QMap<QString, HistoryEntry> histories_;
  QTimer historyTimer_;
  Chain historyChain_ = Chain::Bitcoin;
  int historyDays_ = 365;
  bool historyPending_ = false, historyRequested_ = false;
  qint64 nextHistory_ = 0;
  QString historyStatus_ = "Chart unavailable";
  void fetchHistory();
  void rateLimited(QNetworkReply *);
  QUrl serviceUrl(const QString &) const;
};
QString usdText(double value);
} // namespace aurum
