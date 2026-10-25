#include "market.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QUrlQuery>
#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>
namespace aurum {
namespace {
qint64 periodSeconds(int days) { return days <= 7 ? 3600 : 86400; }
qint64 completedPeriod(int days) {
  const auto step = periodSeconds(days);
  return QDateTime::currentSecsSinceEpoch() / step * step - step;
}
QString historyKey(Chain c, int days) {
  return MarketData::priceSymbol(c) + (days <= 7 ? "/hour" : "/day");
}
} // namespace
QVector<QPointF> MarketData::parseHistory(const QByteArray &body, Chain c) {
  QJsonParseError error;
  const auto doc = QJsonDocument::fromJson(body, &error);
  const auto rows =
      doc.object().value(priceSymbol(c)).toObject().value("USD").toArray();
  if (error.error != QJsonParseError::NoError || rows.size() < 2 ||
      rows.size() > 367)
    throw std::runtime_error("Invalid chart response");
  QVector<QPointF> result;
  const auto latest = QDateTime::currentSecsSinceEpoch();
  double previous = 0;
  for (const auto &row : rows) {
    const auto point = row.toObject();
    const auto stampValue = point.value("time"), close = point.value("close");
    const double stamp = stampValue.toDouble(), price = close.toDouble();
    if (!stampValue.isDouble() || !close.isDouble() || !std::isfinite(stamp) ||
        std::floor(stamp) != stamp || stamp <= previous || stamp > latest ||
        !std::isfinite(price) || price <= 0 || price > 1e12)
      throw std::runtime_error("Invalid chart values");
    previous = stamp;
    result.push_back({stamp * 1000, price});
  }
  return result;
}
QVector<QPointF> MarketData::history(Chain c, int days) const {
  const auto it = histories_.constFind(historyKey(c, days));
  if (it == histories_.cend() || it->period != completedPeriod(days))
    return {};
  const auto end = QDateTime::currentSecsSinceEpoch() / 3600 * 3600;
  const auto endDate = QDateTime::fromSecsSinceEpoch(end, Qt::UTC);
  const int months = days == 30 ? 1 : days == 90 ? 3 : days == 180 ? 6 : 12;
  const auto start =
      (days <= 7 ? endDate.addDays(-days) : endDate.addMonths(-months))
          .toSecsSinceEpoch();
  const auto step = periodSeconds(days);
  QVector<QPointF> result;
  // Match the desktop chart's calendar range and current-hour alignment.
  // Daily prices remain day buckets even though the x-axis begins this hour.
  for (auto stamp = start; stamp < end; stamp += step) {
    const qint64 bucket = stamp / step * step * 1000;
    const auto point =
        std::lower_bound(it->points.cbegin(), it->points.cend(), bucket,
                         [](const QPointF &p, qint64 t) { return p.x() < t; });
    if (point != it->points.cend() && point->x() == bucket)
      result.push_back({double(stamp * 1000), point->y()});
  }
  const auto &current = quote(c);
  if (current.fresh())
    result.push_back({double(end * 1000), current.usd});
  return result.size() >= 2 ? result : QVector<QPointF>{};
}
QString MarketData::historyStatus() const {
  return history(historyChain_, historyDays_).isEmpty()
             ? historyStatus_
             : QString("Exodus pricing · USD");
}
void MarketData::requestHistory(Chain c, int days) {
  if (int(c) < 0 || int(c) >= AssetCount ||
      !QVector<int>{1, 7, 30, 90, 180, 365}.contains(days))
    return;
  historyChain_ = c;
  historyDays_ = days;
  historyRequested_ = true;
  if (active_ && history(c, days).isEmpty()) {
    historyStatus_ = "Loading price chart…";
    fetchHistory();
  }
}
void MarketData::fetchHistory() {
  if (!active_ || !historyRequested_ || historyPending_ ||
      !history(historyChain_, historyDays_).isEmpty())
    return;
  const auto now = QDateTime::currentSecsSinceEpoch();
  if (now < nextHistory_) {
    historyTimer_.start(int((nextHistory_ - now) * 1000));
    return;
  }
  const int days = historyDays_;
  const auto step = periodSeconds(days), period = completedPeriod(days);
  const auto gen = generation_;
  auto url = serviceUrl("historical-price");
  QUrlQuery query;
  query.addQueryItem("from", priceSymbols().join(','));
  query.addQueryItem("to", "USD");
  query.addQueryItem("granularity", days <= 7 ? "hour" : "day");
  query.addQueryItem("limit", days <= 7 ? "168" : "366");
  query.addQueryItem("timestamp", QString::number(period));
  query.addQueryItem("ignoreInvalidSymbols", "true");
  url.setQuery(query);
  QNetworkRequest req(url);
  req.setTransferTimeout(12000);
  req.setHeader(QNetworkRequest::UserAgentHeader, "AurumWallet/0.7.2");
  req.setRawHeader("Cache-Control", "max-age=3600");
  req.setAttribute(QNetworkRequest::CookieLoadControlAttribute,
                   QNetworkRequest::Manual);
  req.setAttribute(QNetworkRequest::CookieSaveControlAttribute,
                   QNetworkRequest::Manual);
  req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                   QNetworkRequest::SameOriginRedirectPolicy);
  historyPending_ = true;
  nextHistory_ = now + 10;
  auto *reply = manager_.get(req);
  reply->setReadBufferSize(1024 * 1024 + 1);
  auto body = std::make_shared<QByteArray>();
  connect(reply, &QNetworkReply::readyRead, this, [reply, body] {
    *body += reply->readAll();
    if (body->size() > 1024 * 1024)
      reply->abort();
  });
  connect(
      reply, &QNetworkReply::finished, this,
      [this, reply, body, days, step, period, gen] {
        *body += reply->readAll();
        reply->deleteLater();
        if (gen != generation_)
          return;
        historyPending_ = false;
        const int code =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        historyStatus_ = "Chart unavailable";
        if (code == 429) {
          rateLimited(reply);
          historyStatus_ = "Chart service rate limited";
        } else if (reply->error() == QNetworkReply::NoError && code == 200 &&
                   body->size() <= 1024 * 1024) {
          int valid = 0;
          for (int i = 0; i < AssetCount; ++i) {
            try {
              auto points = parseHistory(*body, Chain(i));
              const auto last = qint64(points.back().x() / 1000);
              if (last != period || points.size() > (days <= 7 ? 169 : 367))
                throw std::runtime_error("Incomplete chart response");
              for (const auto &point : points)
                if (qint64(point.x() / 1000) % step != 0)
                  throw std::runtime_error("Misaligned chart response");
              for (int n = 1; n < points.size(); ++n)
                if (points[n].x() - points[n - 1].x() != step * 1000)
                  throw std::runtime_error("Gapped chart response");
              histories_[historyKey(Chain(i), days)] = {points, period};
              ++valid;
            } catch (const std::exception &) {
              // Missing data for one asset cannot contaminate another asset.
            }
          }
          historyStatus_ =
              valid ? "Exodus pricing · USD" : "Invalid chart response";
        }
        emit changed();
        // A selection made during the request may need the other granularity.
        // Failures wait for the next minute or an explicit refresh, never spin.
        if ((days <= 7) != (historyDays_ <= 7))
          fetchHistory();
      });
}
} // namespace aurum
