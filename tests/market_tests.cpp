#include "market.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrlQuery>
#include <QtTest>
using namespace aurum;
namespace {
QByteArray prices(double ltc = 64.05) {
  QJsonObject o;
  for (int i = 0; i < AssetCount; ++i)
    o[MarketData::priceSymbol(Chain(i))] =
        QJsonObject{{"USD", i == 1 ? ltc : 1.001}};
  return QJsonDocument(o).toJson(QJsonDocument::Compact);
}
QByteArray ticker() {
  QJsonObject o;
  for (const auto &symbol : MarketData::priceSymbols())
    o[symbol] = QJsonObject{{"USD", QJsonObject{{"c24h", 1.5}}}};
  return QJsonDocument(o).toJson(QJsonDocument::Compact);
}
QByteArray history(qint64 period, int step, int count) {
  QJsonObject o;
  for (int i = 0; i < AssetCount; ++i) {
    QJsonArray rows;
    for (int n = 0; n < count; ++n)
      rows.append(QJsonObject{{"time", period - qint64(count - n - 1) * step},
                              {"close", 10.0 * (i + 1) + n / 100.0}});
    o[MarketData::priceSymbol(Chain(i))] = QJsonObject{{"USD", rows}};
  }
  return QJsonDocument(o).toJson(QJsonDocument::Compact);
}
void respond(QTcpSocket *socket, const QByteArray &body, int status = 200,
             const QByteArray &extra = {}) {
  socket->write("HTTP/1.1 " + QByteArray::number(status) +
                " Test\r\nConnection: close\r\nContent-Length: " +
                QByteArray::number(body.size()) + "\r\n" + extra + "\r\n" +
                body);
  socket->disconnectFromHost();
}
// Accumulates fragmented HTTP requests, with all tests confined to loopback.
void serve(QTcpServer &server, QObject *owner,
           std::function<void(QTcpSocket *, QByteArray)> handler) {
  QObject::connect(&server, &QTcpServer::newConnection, owner,
                   [&server, handler] {
                     auto *socket = server.nextPendingConnection();
                     auto bytes = std::make_shared<QByteArray>();
                     QObject::connect(socket, &QTcpSocket::readyRead, socket,
                                      [socket, bytes, handler] {
                                        *bytes += socket->readAll();
                                        if (bytes->contains("\r\n\r\n"))
                                          handler(socket, *bytes);
                                      });
                     QObject::connect(socket, &QTcpSocket::disconnected, socket,
                                      &QObject::deleteLater);
                   });
}
QUrl base(const QTcpServer &server) {
  return QUrl("http://127.0.0.1:" + QString::number(server.serverPort()));
}
} // namespace
class MarketTests : public QObject {
  Q_OBJECT
private slots:
  void historicalPrices() {
    const auto body = history(1700000000, 3600, 3);
    const auto rows = MarketData::parseHistory(body, Chain::Litecoin);
    QCOMPARE(rows.size(), 3);
    QCOMPARE(rows.back().x(), 1700000000000.0);
    QCOMPARE(rows.back().y(), 20.02);
    QCOMPARE(MarketData::parseHistory(body, Chain::Bitcoin).back().y(), 10.02);
    for (const auto &invalid :
         {QByteArray("{\"LTC\":{\"USD\":[{\"time\":1700000000,\"close\":1},{"
                     "\"time\":1700000000,\"close\":2}]}}"),
          QByteArray("{\"LTC\":{\"USD\":[{\"time\":1700000000,\"close\":1},{"
                     "\"time\":1700000001,\"close\":-1}]}}"),
          QByteArray("{\"LTC\":{\"USD\":[{\"time\":1700000000,\"close\":1},{"
                     "\"time\":1700000001000,\"close\":2}]}}"),
          QByteArray("{\"LTC\":{\"USD\":[{\"time\":1700000000,\"close\":1},{"
                     "\"time\":1700000001.5,\"close\":2}]}}"),
          QByteArray("{}"), QByteArray("not json")})
      QVERIFY_EXCEPTION_THROWN(
          MarketData::parseHistory(invalid, Chain::Litecoin),
          std::runtime_error);
    QVERIFY_EXCEPTION_THROWN(
        MarketData::parseHistory(history(1700000000, 3600, 368),
                                 Chain::Litecoin),
        std::runtime_error);
  }
  void mappingsAndFreshness() {
    const auto now = QDateTime::currentDateTimeUtc();
    const auto q = MarketData::parse(prices(), ticker(), now, now);
    QCOMPARE(q[1].usd, 64.05);
    QCOMPARE(q[1].change24h, 1.5);
    QVERIFY(q[1].hasChange);
    QVERIFY(q[1].fresh(now));
    QVERIFY(!q[1].fresh(now.addSecs(301)));
    QCOMPARE(q[6].usd, q[8].usd);
    QCOMPARE(q[7].usd, q[9].usd);
    QVERIFY(q[6].usd != 1.0);
    QCOMPARE(MarketData::priceSymbols().size(), 9);
    QCOMPARE(MarketData::priceSymbol(Chain::Polygon), QString("MATICNATIVE"));
    QCOMPARE(MarketData::priceSymbol(Chain::Bnb), QString("BNBBSC"));
    QCOMPARE(usdText(1234.567), QString("$1,234.57"));
    QVERIFY_EXCEPTION_THROWN(MarketData::parse("not json", ticker(), now, now),
                             std::runtime_error);
    QVERIFY_EXCEPTION_THROWN(
        MarketData::parse(prices(), ticker(), now.addSecs(-400), now),
        std::runtime_error);
    const auto partial = MarketData::parse(prices(-1), "{}", now, now);
    QVERIFY(!partial[1].fresh(now));
    QVERIFY(partial[0].fresh(now));
    QVERIFY(!partial[0].hasChange);
    QVERIFY_EXCEPTION_THROWN(MarketData::priceSymbol(Chain(100)),
                             std::invalid_argument);
  }
  void requestPrivacyAndThrottle() {
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    QList<QByteArray> requests;
    serve(server, this, [&](QTcpSocket *socket, QByteArray request) {
      requests << request;
      respond(socket, request.startsWith("GET /ticker?") ? ticker() : prices(),
              200,
              "Last-Modified: " +
                  QDateTime::currentDateTimeUtc()
                  .toString("ddd, dd MMM yyyy HH:mm:ss 'GMT'")
                      .toLatin1() +
                  "\r\nSet-Cookie: tracking=not-accepted\r\n");
    });
    MarketData market;
    market.setEndpoint(base(server));
    market.start();
    QTRY_VERIFY_WITH_TIMEOUT(market.quote(Chain::Litecoin).hasChange, 2000);
    QCOMPARE(requests.size(), 2);
    for (const auto &request : requests) {
      QVERIFY(request.contains(
          "from=BTC,LTC,SOL,ETH,MATICNATIVE,BNBBSC,USDT,USDC"));
      QVERIFY(request.contains("to=USD"));
      QVERIFY(!request.contains("address"));
      QVERIFY(!request.contains("password"));
      QVERIFY(!request.contains("Authorization:"));
      QVERIFY(!request.contains("Cookie:"));
    }
    market.refresh();
    QTest::qWait(50);
    QCOMPARE(requests.size(), 2);
    market.stop();
    QVERIFY(!market.quote(Chain::Bitcoin).fresh());
  }
  void historyNetworkAndCache() {
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    int chartRequests = 0;
    serve(server, this, [&](QTcpSocket *socket, QByteArray request) {
      if (!request.contains("/historical-price?")) {
        respond(socket,
                request.startsWith("GET /ticker?") ? ticker() : prices());
        return;
      }
      ++chartRequests;
      const QUrlQuery query(
          QUrl(QString::fromLatin1(request.split(' ').at(1))));
      const bool hourly = query.queryItemValue("granularity") == "hour";
      QCOMPARE(query.queryItemValue("limit"),
               hourly ? QString("168") : QString("366"));
      const auto period = query.queryItemValue("timestamp").toLongLong();
      const int step = hourly ? 3600 : 86400;
      QCOMPARE(period, QDateTime::currentSecsSinceEpoch() / step * step - step);
      QVERIFY(!request.contains("address"));
      QVERIFY(!request.contains("Cookie:"));
      respond(socket, history(period, step, hourly ? 169 : 367));
    });
    MarketData market;
    market.setEndpoint(base(server));
    market.start();
    market.requestHistory(Chain::UsdtEthereum, 365);
    const auto today = QDateTime::currentDateTimeUtc().date();
    const auto yearPoints = today.addMonths(-12).daysTo(today) + 1;
    const auto monthPoints = today.addMonths(-1).daysTo(today) + 1;
    QTRY_COMPARE_WITH_TIMEOUT(market.history(Chain::UsdtEthereum, 365).size(),
                              yearPoints, 2000);
    for (int i = 0; i < AssetCount; ++i) {
      market.requestHistory(Chain(i), 30);
      QCOMPARE(market.history(Chain(i), 30).size(), monthPoints);
    }
    QCOMPARE(market.history(Chain::UsdtEthereum, 365),
             market.history(Chain::UsdtSolana, 365));
    QCOMPARE(chartRequests, 1);
    market.requestHistory(Chain::Litecoin, 1);
    QTRY_COMPARE_WITH_TIMEOUT(market.history(Chain::Litecoin, 1).size(), 25,
                              12000);
    QCOMPARE(market.history(Chain::Litecoin, 7).size(), 169);
    QCOMPARE(chartRequests, 2);
    market.stop();
    QVERIFY(market.history(Chain::UsdtSolana, 365).isEmpty());
  }
  void rateLimitAndStaleData() {
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    int hits = 0;
    serve(server, this, [&](QTcpSocket *socket, QByteArray request) {
      ++hits;
      if (request.contains("/historical-price?"))
        respond(socket, "{}", 429, "Retry-After: 600\r\n");
      else
        respond(socket,
                request.startsWith("GET /ticker?") ? ticker() : prices(), 200,
                "Last-Modified: Tue, 14 Nov 2023 22:13:20 +0000\r\n");
    });
    MarketData market;
    market.setEndpoint(base(server));
    market.start();
    market.requestHistory(Chain::Litecoin, 365);
    QTRY_COMPARE_WITH_TIMEOUT(hits, 3, 2000);
    QTRY_COMPARE(market.historyStatus(), QString("Chart service rate limited"));
    QVERIFY(!market.quote(Chain::Litecoin).fresh());
    QVERIFY(market.history(Chain::Litecoin, 365).isEmpty());
    market.refresh();
    market.requestHistory(Chain::Bitcoin, 1);
    QTest::qWait(100);
    QCOMPARE(hits, 3);
    market.stop();
  }
  void cancelledResponsesAndEndpointValidation() {
    MarketData market;
    for (const auto &url :
         {"http://example.com", "https://user:pass@example.com",
          "https://example.com?key=secret", "file:///tmp/prices"})
      QVERIFY_EXCEPTION_THROWN(market.setEndpoint(QUrl(url)),
                               std::runtime_error);
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    QList<QTcpSocket *> waiting;
    serve(server, this,
          [&](QTcpSocket *socket, QByteArray) { waiting << socket; });
    market.setEndpoint(base(server));
    market.start();
    market.requestHistory(Chain::Bitcoin, 365);
    QTRY_COMPARE_WITH_TIMEOUT(waiting.size(), 3, 2000);
    market.stop();
    QTest::qWait(50);
    QVERIFY(!market.quote(Chain::Bitcoin).fresh());
    QCOMPARE(market.status(), QString("Prices unavailable"));
    QVERIFY(market.history(Chain::Bitcoin, 365).isEmpty());
  }
};
QTEST_GUILESS_MAIN(MarketTests)
#include "market_tests.moc"
