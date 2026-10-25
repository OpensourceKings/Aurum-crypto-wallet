#include "swap.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrlQuery>
#include <functional>
#include <stdexcept>
namespace aurum {
namespace {
constexpr qint64 maxResponse = 256 * 1024;
double number(const QJsonValue &v) {
  return v.isString() ? v.toString().toDouble() : v.toDouble();
}
QJsonObject object(const QByteArray &body) {
  QJsonParseError err;
  const auto doc = QJsonDocument::fromJson(body, &err);
  if (err.error != QJsonParseError::NoError || !doc.isObject())
    throw std::runtime_error("Swap service returned invalid data");
  const auto o = doc.object();
  if (o.contains("error") && !o.value("error").toString().isEmpty())
    throw std::runtime_error(o.value("error").toString().toStdString());
  return o;
}
} // namespace
SwapService::SwapService(QObject *parent) : QObject(parent) {}
QString SwapService::envApiKey() {
  return QString::fromUtf8(qgetenv("AURUM_TROCADOR_API_KEY")).trimmed();
}
void SwapService::get(const QString &path,
                      std::function<void(QByteArray)> done) {
  if (apiKey_.isEmpty()) {
    emit failed("Set AURUM_TROCADOR_API_KEY to use swaps");
    return;
  }
  QUrl url(base_);
  url.setPath(path);
  QNetworkRequest request(url);
  request.setRawHeader("API-Key", apiKey_.toUtf8());
  request.setRawHeader("Accept", "application/json");
  request.setTransferTimeout(20000);
  request.setMaximumRedirectsAllowed(0);
  auto *reply = manager_.get(request);
  connect(reply, &QNetworkReply::finished, this, [this, reply, done] {
    reply->deleteLater();
    if (reply->error() == QNetworkReply::OperationCanceledError)
      return;
    const auto body = reply->read(maxResponse);
    if (reply->error() != QNetworkReply::NoError && body.isEmpty()) {
      emit failed("Swap request failed: " + reply->errorString());
      return;
    }
    try {
      done(body);
    } catch (const std::exception &e) {
      emit failed(QString::fromUtf8(e.what()));
    }
  });
}
void SwapService::cancel() {
  for (auto *reply : manager_.findChildren<QNetworkReply *>())
    reply->abort();
}
SwapRate SwapService::parseRate(const QByteArray &body) {
  const auto o = object(body);
  SwapRate r;
  r.tradeId = o.value("trade_id").toString();
  r.amountFrom = number(o.value("amount_from"));
  const auto quotes = o.value("quotes").toObject().value("quotes").toArray();
  if (!quotes.isEmpty()) {
    const auto best = quotes.first().toObject();
    r.provider = best.value("provider").toString();
    r.kycRating = best.value("kycrating").toString();
    r.amountTo = number(best.value("amount_to"));
  }
  if (r.amountTo <= 0)
    r.amountTo = number(o.value("amount_to"));
  if (r.provider.isEmpty())
    r.provider = o.value("provider").toString();
  if (r.tradeId.isEmpty() || r.amountTo <= 0)
    throw std::runtime_error("No swap route available for this pair");
  return r;
}
SwapTrade SwapService::parseTrade(const QByteArray &body) {
  const auto o = object(body);
  SwapTrade t;
  t.id = o.value("trade_id").toString();
  t.status = o.value("status").toString();
  t.depositAddress = o.value("address_provider").toString();
  t.payoutAddress = o.value("address_user").toString();
  t.memo = o.value("address_provider_memo").toString();
  t.amountFrom = number(o.value("amount_from"));
  t.amountTo = number(o.value("amount_to"));
  if (t.id.isEmpty())
    throw std::runtime_error("Swap service returned no trade id");
  return t;
}
void SwapService::requestRate(const QString &ft, const QString &fn,
                              const QString &tt, const QString &tn,
                              double amount) {
  if (!(amount > 0)) {
    emit failed("Enter an amount to swap");
    return;
  }
  QUrlQuery q;
  q.addQueryItem("ticker_from", ft.toLower());
  q.addQueryItem("network_from", fn);
  q.addQueryItem("ticker_to", tt.toLower());
  q.addQueryItem("network_to", tn);
  q.addQueryItem("amount_from", QString::number(amount, 'f', 8));
  q.addQueryItem("payment", "False");
  get("/new_rate?" + q.toString(QUrl::FullyEncoded),
      [this](QByteArray b) { emit rateReady(parseRate(b)); });
}
void SwapService::createTrade(const SwapRate &rate, const QString &payout,
                              const QString &refund) {
  if (payout.trimmed().isEmpty()) {
    emit failed("Enter a payout address");
    return;
  }
  QUrlQuery q;
  q.addQueryItem("trade_id", rate.tradeId);
  q.addQueryItem("address", payout.trimmed());
  q.addQueryItem("refund", refund.trimmed());
  q.addQueryItem("provider", rate.provider);
  get("/new_trade?" + q.toString(QUrl::FullyEncoded),
      [this](QByteArray b) { emit tradeReady(parseTrade(b)); });
}
void SwapService::pollTrade(const QString &id) {
  QUrlQuery q;
  q.addQueryItem("id", id);
  get("/trade?" + q.toString(QUrl::FullyEncoded),
      [this](QByteArray b) { emit tradeReady(parseTrade(b)); });
}
} // namespace aurum
