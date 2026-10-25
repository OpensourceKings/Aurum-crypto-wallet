#include "electrum.h"
#include "network.h"
#include <QJsonDocument>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>
#include <sodium.h>
using namespace aurum;
namespace {
const QString genesis =
    "12a765e31ffd4059bada1e25190f6e98c99d9714d334efa41a195a7e7e04bfe2";
class Server : public QTcpServer {
public:
  std::function<QJsonValue(QString, QJsonArray)> answer;
  QStringList methods;
  bool split = false;
  Server() {
    connect(this, &QTcpServer::newConnection, this, [this] {
      auto *s = nextPendingConnection();
      auto buf = std::make_shared<QByteArray>();
      connect(s, &QTcpSocket::readyRead, this, [this, s, buf] {
        *buf += s->readAll();
        while (buf->contains('\n')) {
          auto end = buf->indexOf('\n');
          auto obj = QJsonDocument::fromJson(buf->left(end)).object();
          buf->remove(0, end + 1);
          const auto method = obj.value("method").toString();
          methods << method;
          QJsonValue result =
              method == "server.features"
                  ? QJsonValue(QJsonObject{{"genesis_hash", genesis},
                                           {"hash_function", "sha256"}})
                  : QJsonValue{};
          if (method == "server.version")
            result = QJsonArray{"Fixture", "1.4"};
          else if (answer)
            result = answer(method, obj.value("params").toArray());
          auto bytes = QJsonDocument(QJsonObject{{"id", obj.value("id")},
                                                 {"result", result},
                                                 {"jsonrpc", "2.0"}})
                           .toJson(QJsonDocument::Compact) +
                       '\n';
          if (split) {
            s->write("{\"method\":\"notice\"}\n");
            s->write(bytes.left(7));
            QTimer::singleShot(10, s, [s, bytes] { s->write(bytes.mid(7)); });
          } else
            s->write(bytes);
        }
      });
      connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
    });
    if (!listen(QHostAddress::LocalHost))
      qFatal("fixture listen failed");
  }
  QString url() const {
    return "tcp://127.0.0.1:" + QString::number(serverPort());
  }
};
QJsonValue features() {
  return QJsonObject{{"genesis_hash", genesis}, {"hash_function", "sha256"}};
}
} // namespace
class ElectrumTests : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() { QVERIFY(sodium_init() >= 0); }
  void endpointValidation() {
    QVERIFY(ElectrumClient::validEndpoint("ssl://electrum1.cipig.net:20063"));
    QVERIFY(ElectrumClient::validEndpoint("tcp://127.0.0.1:50001"));
    for (auto s : {"tcp://example.org:50001", "ssl://user:pass@example.org:2",
                   "ssl://example.org:2/path", "ssl://example.org:2?q=1",
                   "ssl://example.org:2#frag", "ssl://example.org"})
      QVERIFY(!ElectrumClient::validEndpoint(s));
  }
  void verifiedRawTransaction() {
    unsigned char key[32]{};
    key[31] = 1;
    auto recipient = address(key, Chain::Litecoin);
    auto tx = signUtxo(key, Chain::Litecoin, {{QByteArray(32, 1), 0, 1000000}},
                       recipient, 500000, 1);
    auto parsed = verifiedElectrumTransaction(
        QString::fromLatin1(tx.wire.toHex()), tx.id);
    QCOMPARE(parsed.value("txid").toString(), tx.id);
    auto output = parsed.value("vout").toArray()[0].toObject();
    QCOMPARE(output.value("value").toInteger(), 500000);
    QCOMPARE(
        output.value("scriptpubkey").toString(),
        QString::fromLatin1(addressScript(recipient, Chain::Litecoin).toHex()));
    QVERIFY_EXCEPTION_THROWN(
        verifiedElectrumTransaction(QString::fromLatin1(tx.wire.toHex()),
                                    QString(64, '0')),
        std::runtime_error);
    QVERIFY_EXCEPTION_THROWN(
        verifiedElectrumTransaction(QString::fromLatin1(tx.wire.toHex()) + "00",
                                    tx.id),
        std::runtime_error);
    for (int size : {0, 1, 4, 5, 6, 20, int(tx.wire.size() - 1)})
      QVERIFY_EXCEPTION_THROWN(
          verifiedElectrumTransaction(
              QString::fromLatin1(tx.wire.left(size).toHex()), tx.id),
          std::runtime_error);
    auto changed = tx.wire;
    changed[5] = 2;
    QVERIFY_EXCEPTION_THROWN(verifiedElectrumTransaction(
                                 QString::fromLatin1(changed.toHex()), tx.id),
                             std::runtime_error);
  }
  void fragmentedProtocolAndNetworkFlows() {
    Server server;
    server.split = true;
    unsigned char key[32]{};
    key[31] = 1;
    const auto addr = address(key, Chain::Litecoin);
    const auto previous =
        signUtxo(key, Chain::Litecoin, {{QByteArray(32, 1), 0, 1000000}}, addr,
                 500000, 1);
    bool wrongAmount = false;
    QString broadcast;
    server.answer = [&](QString method, QJsonArray params) -> QJsonValue {
      if (method == "server.features")
        return features();
      if (method == "blockchain.scripthash.get_balance")
        return QJsonObject{{"confirmed", 500000}, {"unconfirmed", -1000}};
      if (method == "blockchain.scripthash.listunspent")
        return QJsonArray{QJsonObject{{"tx_hash", previous.id},
                                      {"tx_pos", 0},
                                      {"value", wrongAmount ? 500001 : 500000},
                                      {"height", 100}}};
      if (method == "blockchain.transaction.get")
        return QString::fromLatin1(previous.wire.toHex());
      if (method == "blockchain.scripthash.get_history")
        return QJsonArray{
            QJsonObject{{"tx_hash", previous.id}, {"height", 100}}};
      if (method == "blockchain.transaction.broadcast") {
        broadcast = params[0].toString();
        return previous.id;
      }
      return {};
    };
    Network n;
    auto endpoints = n.endpoints();
    endpoints.litecoin = server.url();
    n.setEndpoints(endpoints);
    std::array<QString, AssetCount> addresses;
    addresses[int(Chain::Litecoin)] = addr;
    n.setAddresses(addresses);
    QTRY_VERIFY_WITH_TIMEOUT(n.state(Chain::Litecoin).known, 3000);
    QCOMPARE(n.state(Chain::Litecoin).balance, 500000ULL);
    QCOMPARE(n.state(Chain::Litecoin).pending, -1000);
    // Test transport, raw previous transaction verification, history and
    // one-shot broadcast.
    bool done = false;
    QString err;
    QVector<Utxo> inputs;
    n.verifiedUtxos(Chain::Litecoin, [&](QVector<Utxo> v, QString e) {
      inputs = v;
      err = e;
      done = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(done, 3000);
    QVERIFY2(err.isEmpty(), qPrintable(err));
    QCOMPARE(inputs.size(), 1);
    QCOMPARE(inputs[0].amount, 500000ULL);
    wrongAmount = true;
    done = false;
    n.verifiedUtxos(Chain::Litecoin, [&](QVector<Utxo>, QString e) {
      err = e;
      done = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(done, 3000);
    QVERIFY(err.contains("ownership mismatch"));
    done = false;
    QJsonValue history;
    n.history(Chain::Litecoin, [&](QJsonValue v, QString e) {
      history = v;
      err = e;
      done = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(done, 3000);
    QVERIFY(err.isEmpty());
    QCOMPARE(history.toArray().size(), 1);
    done = false;
    n.broadcast(Chain::Litecoin, previous, [&](QJsonValue, QString e) {
      err = e;
      done = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(done, 3000);
    QVERIFY(err.isEmpty());
    QCOMPARE(broadcast, QString::fromLatin1(previous.wire.toHex()));
    QCOMPARE(server.methods.count("blockchain.transaction.broadcast"), 1);
    n.pause();
    QCOMPARE(n.connectionStatus(Chain::Litecoin), QString("Wallet locked"));
  }
  void wrongNetworkAndCancellation() {
    Server server;
    server.answer = [](QString, QJsonArray) {
      return QJsonObject{{"genesis_hash", QString(64, '0')},
                         {"hash_function", "sha256"}};
    };
    ElectrumClient client;
    client.setEndpoint(server.url());
    bool done = false;
    QString err;
    client.request("blockchain.scripthash.get_balance", {},
                   [&](QJsonValue, QString e) {
                     err = e;
                     done = true;
                   });
    QTRY_VERIFY_WITH_TIMEOUT(done, 3000);
    QVERIFY(err.contains("not Litecoin mainnet"));
    QCOMPARE(server.methods.size(), 2);
    done = false;
    client.request("blockchain.scripthash.get_balance", {},
                   [&](QJsonValue, QString e) {
                     err = e;
                     done = true;
                   });
    client.cancel();
    QVERIFY(done);
    QVERIFY(err.contains("cancelled"));
  }
};
QTEST_GUILESS_MAIN(ElectrumTests)
#include "electrum_tests.moc"
