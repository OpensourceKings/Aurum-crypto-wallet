#include "market.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QNetworkReply>
#include <QUrlQuery>
#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>
namespace aurum {
bool MarketQuote::fresh(const QDateTime &now) const {
  return usd > 0 && std::isfinite(usd) && updated.isValid() &&
         updated.secsTo(now) >= -300 && updated.secsTo(now) <= 300;
}
QString MarketData::priceSymbol(Chain c) {
  // These are service identifiers, distinct from the tickers shown in the UI.
  static const QString symbols[]{"BTC",         "LTC",    "SOL",  "ETH",
                                 "MATICNATIVE", "BNBBSC", "USDT", "USDC",
                                 "USDT",        "USDC",   "XMR"};
  if (int(c) < 0 || int(c) >= AssetCount)
    throw std::invalid_argument("Invalid market asset");
  return symbols[int(c)];
}
QStringList MarketData::priceSymbols() {
  QStringList symbols;
  for (int i = 0; i < AssetCount; ++i)
    if (!symbols.contains(priceSymbol(Chain(i))))
      symbols << priceSymbol(Chain(i));
  return symbols;
}
QString usdText(double value) {
  if (!std::isfinite(value) || value < 0)
    return "—";
  return "$" + QLocale(QLocale::English, QLocale::UnitedStates)
                   .toString(value, 'f', 2);
}
std::array<MarketQuote, AssetCount> MarketData::parse(const QByteArray &prices,
                                                      const QByteArray &ticker,
                                                      const QDateTime &updated,
                                                      const QDateTime &now) {
  QJsonParseError err;
  const auto doc = QJsonDocument::fromJson(prices, &err);
  if (err.error != QJsonParseError::NoError || !doc.isObject())
    throw std::runtime_error("Invalid price response");
  const auto changes = QJsonDocument::fromJson(ticker).object();
  std::array<MarketQuote, AssetCount> result{};
  int valid = 0;
  for (int i = 0; i < AssetCount; ++i) {
    const auto key = priceSymbol(Chain(i));
    const auto value = doc.object().value(key).toObject().value("USD");
    auto &q = result[i];
    q.usd = value.isDouble() ? value.toDouble() : 0;
    q.updated = updated;
    const auto change =
        changes.value(key).toObject().value("USD").toObject().value("c24h");
    q.change24h = change.toDouble();
    q.hasChange = change.isDouble() && std::isfinite(q.change24h) &&
                  std::abs(q.change24h) <= 1e9;
    if (q.usd > 1e12 || !q.fresh(now))
      q = {};
    else
      ++valid;
  }
  if (!valid)
    throw std::runtime_error("Price data is stale or unavailable");
  return result;
}
MarketData::MarketData(QObject *parent) : QObject(parent) {
  historyTimer_.setSingleShot(true);
  connect(&historyTimer_, &QTimer::timeout, this, &MarketData::fetchHistory);
  timer_.setInterval(60000);
  connect(&timer_, &QTimer::timeout, this, [this] {
    emit changed();
    refresh();
    if (historyRequested_)
      fetchHistory();
  });
}
QUrl MarketData::serviceUrl(const QString &path) const {
  QUrl url = endpoint_;
  QString base = url.path();
  while (base.endsWith('/'))
    base.chop(1);
  url.setPath(base + '/' + path);
  return url;
}
void MarketData::setEndpoint(const QUrl &u) {
  const bool local =
      u.host() == "localhost" || u.host() == "127.0.0.1" || u.host() == "::1";
  if (!u.isValid() || u.host().isEmpty() || !u.userInfo().isEmpty() ||
      u.hasFragment() || u.hasQuery() ||
      !(u.scheme() == "https" || (local && u.scheme() == "http")))
    throw std::runtime_error("Invalid price endpoint");
  stop();
  endpoint_ = u;
  nextRequest_ = nextHistory_ = 0;
  backoffSeconds_ = 60;
}
void MarketData::start() {
  active_ = true;
  timer_.start();
  refresh();
}
void MarketData::stop() {
  active_ = false;
  ++generation_;
  timer_.stop();
  historyTimer_.stop();
  // Cancel outstanding public-data traffic when the wallet is locked.
  for (auto *reply : manager_.findChildren<QNetworkReply *>())
    if (reply->isRunning())
      reply->abort();
  pending_ = historyPending_ = historyRequested_ = false;
  quotes_ = {};
  histories_.clear();
  historyStatus_ = "Chart unavailable";
  status_ = "Prices unavailable";
  emit changed();
}
void MarketData::rateLimited(QNetworkReply *reply) {
  bool ok = false;
  const auto delay = reply->rawHeader("Retry-After").toInt(&ok);
  backoffSeconds_ = std::min(3600, backoffSeconds_ * 2);
  const auto until = QDateTime::currentSecsSinceEpoch() +
                     std::clamp(ok ? delay : backoffSeconds_, 60, 3600);
  nextRequest_ = std::max(nextRequest_, until);
  nextHistory_ = std::max(nextHistory_, until);
}
void MarketData::refresh() {
  const auto now = QDateTime::currentSecsSinceEpoch();
  if (!active_ || pending_ || now < nextRequest_)
    return;
  pending_ = true;
  nextRequest_ = now + 60;
  const auto gen = generation_;
  struct Batch {
    QByteArray prices, ticker;
    QDateTime updated;
    int remaining = 2;
    bool limited = false;
  };
  auto batch = std::make_shared<Batch>();
  for (const bool ticker : {false, true}) {
    auto url = serviceUrl(ticker ? "ticker" : "current-price");
    QUrlQuery query;
    query.addQueryItem("from", priceSymbols().join(','));
    query.addQueryItem("to", "USD");
    query.addQueryItem("ignoreInvalidSymbols", "true");
    url.setQuery(query);
    QNetworkRequest req(url);
    req.setTransferTimeout(12000);
    req.setHeader(QNetworkRequest::UserAgentHeader, "AurumWallet/0.7.2");
    req.setRawHeader("Cache-Control", "max-age=60");
    req.setAttribute(QNetworkRequest::CookieLoadControlAttribute,
                     QNetworkRequest::Manual);
    req.setAttribute(QNetworkRequest::CookieSaveControlAttribute,
                     QNetworkRequest::Manual);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::SameOriginRedirectPolicy);
    auto *reply = manager_.get(req);
    reply->setReadBufferSize(256 * 1024 + 1);
    auto body = std::make_shared<QByteArray>();
    connect(reply, &QNetworkReply::readyRead, this, [reply, body] {
      *body += reply->readAll();
      if (body->size() > 256 * 1024)
        reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, body, batch, ticker, gen] {
              *body += reply->readAll();
              reply->deleteLater();
              if (gen != generation_)
                return;
              const auto code =
                  reply->attribute(QNetworkRequest::HttpStatusCodeAttribute)
                      .toInt();
              if (code == 429) {
                rateLimited(reply);
                batch->limited = true;
              } else if (code == 200 &&
                         reply->error() == QNetworkReply::NoError &&
                         body->size() <= 256 * 1024) {
                if (ticker) {
                  batch->ticker = *body;
                } else {
                  batch->prices = *body;
                  // Prefer the server's entity timestamp to avoid relabeling a
                  // stale cached quote as freshly received. Never keep server
                  // cookies.
                  auto stamp = reply->rawHeader("Last-Modified");
                  if (stamp.isEmpty())
                    stamp = reply->rawHeader("Date");
                  // HTTP uses a literal GMT zone; Qt's RFC2822 parser requires
                  // a numeric offset. Preserve the server's actual timestamp.
                  if (stamp.endsWith(" GMT")) {
                    stamp.chop(3);
                    stamp.append("+0000");
                  }
                  batch->updated =
                      stamp.isEmpty()
                          ? QDateTime::currentDateTimeUtc()
                          : QDateTime::fromString(QString::fromLatin1(stamp),
                                                  Qt::RFC2822Date)
                                .toUTC();
                }
              }
              if (--batch->remaining)
                return;
              pending_ = false;
              try {
                quotes_ = parse(batch->prices, batch->ticker, batch->updated);
                status_ = "Exodus pricing · updated " +
                          batch->updated.toLocalTime().toString("HH:mm:ss");
                if (!batch->limited)
                  backoffSeconds_ = 60;
              } catch (const std::exception &) {
                status_ = "Price service unavailable";
              }
              if (batch->limited)
                status_ = "Price service rate limited";
              emit changed();
            });
  }
}
} // namespace aurum
