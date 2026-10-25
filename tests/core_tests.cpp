#include "core.h"
#include "evm.h"
#include "network.h"
#include "spl.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtTest>
#include <algorithm>
#include <secp256k1.h>
#include <sodium.h>
using namespace aurum;
static QByteArray hex(const char *s) { return QByteArray::fromHex(s); }
static QByteArray reverse(QByteArray s) {
  std::reverse(s.begin(), s.end());
  return s;
}
class CoreTests : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() { QVERIFY(sodium_init() >= 0); }
  void exactAmounts() {
    QCOMPARE(parseAmount("1.00000001", 8), 100000001ULL);
    QCOMPARE(parseAmount("0.000000001", 9), 1ULL);
    QCOMPARE(parseAmount("0.000000000001", 12), 1ULL);
    QCOMPARE(parseAmount("18446744073.709551615", 9), 18446744073709551615ULL);
    QCOMPARE(formatAmount(123456789, 8), QString("1.23456789"));
    QCOMPARE(formatAmount(100000000, 8), QString("1"));
    for (const auto &s : {"-1", "1e8", "1,000", ".1", "1.", " 1", "1.000000001",
                          "184467440738"})
      QVERIFY_EXCEPTION_THROWN(parseAmount(s, 8), std::runtime_error);
  }
  void hashes() {
    QCOMPARE(hash160(QByteArray::fromHex("0279be667ef9dcbbac55a06295ce870b07029"
                                         "bfcdb2dce28d959f2815b16f81798"))
                 .toHex(),
             QByteArray("751e76e8199196d454941c45d1b3a323f1433bd6"));
  }
  void addresses() {
    unsigned char key[32] = {};
    key[31] = 1;
    QCOMPARE(publicKey(key, Chain::Bitcoin).toHex(),
             QByteArray("0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2"
                        "815b16f81798"));
    QCOMPARE(address(key, Chain::Bitcoin),
             QString("bc1qw508d6qejxtdg4y5r3zarvary0c5xw7kv8f3t4"));
    QCOMPARE(
        addressScript(address(key, Chain::Litecoin), Chain::Litecoin).toHex(),
        QByteArray("0014751e76e8199196d454941c45d1b3a323f1433bd6"));
    QCOMPARE(addressScript("1BgGZ9tcN4rm9KBzDn7KprQz87SZ26SAMH", Chain::Bitcoin)
                 .toHex(),
             QByteArray("76a914751e76e8199196d454941c45d1b3a323f1433bd688ac"));
    auto tap = segwitAddress("bc", 1, QByteArray(32, '\x42'));
    QCOMPARE(addressScript(tap, Chain::Bitcoin),
             QByteArray::fromHex("5120") + QByteArray(32, '\x42'));
    for (const auto &s : {"bc1qw508d6qejxtdg4y5r3zarvary0c5xw7kygt080",
                          "bC1qw508d6qejxtdg4y5r3zarvary0c5xw7kv8f3t4",
                          "ltc1qw508d6qejxtdg4y5r3zarvary0c5xw7kv8f3t4",
                          "1BgGZ9tcN4rm9KBzDn7KprQz87SZ26SAMI"})
      QVERIFY_EXCEPTION_THROWN(addressScript(s, Chain::Bitcoin),
                               std::runtime_error);
    QVERIFY_EXCEPTION_THROWN(
        addressScript(address(key, Chain::Bitcoin), Chain::Litecoin),
        std::runtime_error);
    for (int n = 1; n < 65; ++n) {
      QByteArray bytes(n, 0);
      randombytes_buf(bytes.data(), bytes.size());
      QCOMPARE(unbase58(base58(bytes)), bytes);
    }
    QCOMPARE(base58(QByteArray(32, 0)), QString(32, '1'));
  }
  void bip143PublishedVector() {
    QVector<Utxo> ins{{reverse(hex("fff7f7881a8099afa6940d42d1e7f6362bec38171ea"
                                   "3edf433541db4e4ad969f")),
                       0, 625000000, 0xffffffee},
                      {reverse(hex("ef51e1b804cc89d182d279655c3aa89e815b1b309fe"
                                   "287d9b2b55d57b90ec68a")),
                       1, 600000000, 0xffffffff}};
    auto outs = hex(
        "202cb206000000001976a9148280b37df378db99f66f85c95a783a76ac7a6d5988ac90"
        "93510d000000001976a9143bde42dbee7e4dbe6a21b2d50ce2f0167faa815988ac");
    auto digest = bip143Digest(
        ins, 1, hex("76a9141d0f172a0ecb48aee1be1f2687d2963ae33f71a188ac"), outs,
        17, 1);
    QCOMPARE(digest.toHex(), QByteArray("c37af31116d1b27caf68aae9e3ac82f1477929"
                                        "014d5b917657d0eb49478cb670"));
    auto key =
        hex("619c335025c7f4012e556c2a58b2506e30b8511b53ade95ea316fd8c3286feb9");
    QCOMPARE(
        signDigest(reinterpret_cast<const unsigned char *>(key.constData()),
                   digest)
            .toHex(),
        QByteArray("304402203609e17b84f6a7d30c80bfa610b5b4542f32a8a0d5447a12fb1"
                   "366d7f01cc44a0220573a954c4518331561406f90300e8f3358f51928d4"
                   "3c212a8caed02de67eebee"));
  }
  void ed25519Rfc8032() {
    auto seed =
        hex("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60");
    auto k = reinterpret_cast<const unsigned char *>(seed.constData());
    QCOMPARE(publicKey(k, Chain::Solana).toHex(),
             QByteArray("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a"
                        "68f707511a"));
    QCOMPARE(
        signEd25519(k, {}).toHex(),
        QByteArray(
            "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555f"
            "b8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b"));
  }
  void solanaWire() {
    unsigned char seed[32] = {1};
    auto pub = publicKey(seed, Chain::Solana);
    auto msg = solanaMessage(pub, QByteArray(32, 2), QByteArray(32, 3), 12345);
    QCOMPARE(msg.size(), 150);
    QCOMPARE(msg.left(4).toHex(), QByteArray("01000103"));
    QCOMPARE(msg.right(12).toHex(), QByteArray("020000003930000000000000"));
    auto tx = signSolana(seed, msg);
    QCOMPARE(tx.wire.size(), 215);
    QCOMPARE(
        crypto_sign_verify_detached(
            reinterpret_cast<const unsigned char *>(tx.wire.constData() + 1),
            reinterpret_cast<const unsigned char *>(msg.constData()),
            msg.size(),
            reinterpret_cast<const unsigned char *>(pub.constData())),
        0);
    QVERIFY_EXCEPTION_THROWN(solanaMessage(pub, pub, QByteArray(32, 1), 1),
                             std::runtime_error);
  }
  void vaultRoundtripAndTamper() {
    QTemporaryDir dir;
    QString path = dir.path() + "/wallet.aurum";
    Vault v;
    QByteArray pw("a long test password");
    v.create(path, pw);
    QVERIFY(pw.isEmpty());
    QVERIFY(v.unlocked());
    QString first = address(v.key(Chain::Bitcoin), Chain::Bitcoin),
            sol = address(v.key(Chain::Solana), Chain::Solana);
    QCOMPARE(QFile(path).size(), 160);
    QVERIFY(!(QFile(path).permissions() &
              (QFileDevice::ReadGroup | QFileDevice::WriteGroup |
               QFileDevice::ReadOther | QFileDevice::WriteOther)));
    std::array<QString, AssetCount> addresses;
    for (int i = 0; i < AssetCount; ++i) {
      if (static_cast<Chain>(i) == Chain::Monero)
        continue;
      addresses[i] =
          address(v.key(static_cast<Chain>(i)), static_cast<Chain>(i));
    }
    v.lock();
    QVERIFY(!v.unlocked());
    pw = "wrong password";
    QVERIFY_EXCEPTION_THROWN(v.unlock(path, pw), std::runtime_error);
    QVERIFY(!v.unlocked());
    QVERIFY(pw.isEmpty());
    pw = "a long test password";
    v.unlock(path, pw);
    QCOMPARE(address(v.key(Chain::Bitcoin), Chain::Bitcoin), first);
    QCOMPARE(address(v.key(Chain::Solana), Chain::Solana), sol);
    for (int i = 0; i < AssetCount; ++i) {
      if (static_cast<Chain>(i) == Chain::Monero)
        continue;
      QCOMPARE(address(v.key(static_cast<Chain>(i)), static_cast<Chain>(i)),
               addresses[i]);
    }
    QCOMPARE(addresses[3], addresses[6]);
    QCOMPARE(addresses[2], addresses[8]);
    QVERIFY(addresses[3] != addresses[4]);
    v.lock();
    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadWrite));
    f.seek(159);
    auto b = f.read(1);
    b[0] ^= 1;
    f.seek(159);
    f.write(b);
    f.close();
    pw = "a long test password";
    QVERIFY_EXCEPTION_THROWN(v.unlock(path, pw), std::runtime_error);
    QVERIFY(!v.unlocked());
  }
  void signedUtxoConservation() {
    unsigned char key[32] = {};
    key[31] = 1;
    for (auto c : {Chain::Bitcoin, Chain::Litecoin}) {
      QVector<Utxo> ins{{QByteArray(32, 1), 0, 100000}};
      auto tx = signUtxo(key, c, ins, address(key, c), 50000, 2);
      QCOMPARE(50000 + tx.change + tx.fee, 100000ULL);
      QVERIFY(tx.fee >= quint64(tx.virtualBytes) * 2);
      QCOMPARE(tx.wire.mid(4, 2).toHex(), QByteArray("0001"));
      QCOMPARE(tx.id.size(), 64);
      QVERIFY_EXCEPTION_THROWN(
          signUtxo(key, c, ins, address(key, c), 100000, 2),
          std::runtime_error);
      QVERIFY_EXCEPTION_THROWN(signUtxo(key, c, ins, address(key, c), 1, 2),
                               std::runtime_error);
      QVERIFY_EXCEPTION_THROWN(signUtxo(key, c, ins, address(key, c), 50000, 0),
                               std::runtime_error);
      ins << ins[0];
      QVERIFY_EXCEPTION_THROWN(
          signUtxo(key, c, ins, address(key, c), 150000, 2),
          std::runtime_error);
    }
  }
  void evmReferenceVectors() {
    QCOMPARE(keccak({}).toHex(), QByteArray("c5d2460186f7233c927e7db2dcc703c0e5"
                                            "00b653ca82273b7bfad8045d85a470"));
    auto key = QByteArray(32, '\x46');
    EvmTransfer t;
    t.nonce = 9;
    t.gasPrice = "20000000000";
    t.to = "0x3535353535353535353535353535353535353535";
    t.value = "1000000000000000000";
    auto tx =
        signEvm(reinterpret_cast<const unsigned char *>(key.constData()), t);
    QCOMPARE(
        tx.wire.toHex(),
        QByteArray("f86c098504a817c80082520894353535353535353535353535353535353"
                   "5353535880de0b6b3a76400008025a028ef61340bd939bc2195fe537567"
                   "866003e1a15d3c71ff63e1590620aa636276a067cbe9d8997f761aecb70"
                   "3304b3800ccf555c9f3dc64214b297fb1966a3b6d83"));
    QCOMPARE(parseUnits("100.123456789012345678", 18),
             QString("100123456789012345678"));
    QCOMPARE(formatUnits("100123456789012345678", 18),
             QString("100.123456789012345678"));
    QCOMPARE(erc20Transfer(t.to, "1000000").toHex(),
             QByteArray("a9059cbb0000000000000000000000003535353535353535353535"
                        "353535353535353535000000000000000000000000000000000000"
                        "00000000000000000000000f4240"));
    for (quint64 id : {1, 56, 137}) {
      t.chain = id;
      auto different =
          signEvm(reinterpret_cast<const unsigned char *>(key.constData()), t);
      QVERIFY(!different.wire.isEmpty());
    }
    QVERIFY_EXCEPTION_THROWN(parseUnits("1.0000001", 6), std::runtime_error);
    QVERIFY_EXCEPTION_THROWN(uint256(QString(79, '9')), std::runtime_error);
    QVERIFY_EXCEPTION_THROWN(
        evmAddressBytes("0x353535353535353535353535353535353535353Z"),
        std::runtime_error);
    QCOMPARE(checksumAddress(QByteArray::fromHex(
                 "52908400098527886e0f7030069857d2e4169ee7")),
             QString("0x52908400098527886E0F7030069857D2E4169EE7"));
  }
  void splDerivation() {
    unsigned char key[32] = {};
    key[31] = 1;
    auto owner = publicKey(key, Chain::Solana),
         mint = unbase58(tokenAddress(Chain::UsdcSolana));
    QVERIFY(isEdwardsPoint(owner));
    auto ata = associatedTokenAddress(owner, mint);
    QCOMPARE(ata.size(), 32);
    QVERIFY(!isEdwardsPoint(ata));
    auto msg = splTransferMessage(owner, QByteArray(32, '\x23'), ata, mint,
                                  QByteArray(32, '\x44'), 1000000);
    QCOMPARE(msg.size(), 320);
    auto tx = signSolana(key, msg);
    QCOMPARE(tx.wire.size(), 385);
    QCOMPARE(
        crypto_sign_verify_detached(
            reinterpret_cast<const unsigned char *>(tx.wire.constData() + 1),
            reinterpret_cast<const unsigned char *>(msg.constData()),
            msg.size(),
            reinterpret_cast<const unsigned char *>(owner.constData())),
        0);
  }

  void endpointValidation() {
    QVERIFY(Network::validEndpoint("https://blockstream.info/api"));
    QVERIFY(Network::validEndpoint("http://127.0.0.1:1234"));
    QVERIFY(Network::validEndpoint("wss://api.mainnet-beta.solana.com", true));
    QVERIFY(!Network::validEndpoint("http://public.example/api"));
    QVERIFY(!Network::validEndpoint("https://user:password@example.com"));
    QVERIFY(!Network::validEndpoint("file:///etc/passwd"));
  }
  void rejectsWrongNetworks() {
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    connect(&server, &QTcpServer::newConnection, this, [&] {
      while (server.hasPendingConnections()) {
        auto *socket = server.nextPendingConnection();
        connect(socket, &QTcpSocket::readyRead, socket, [socket] {
          auto request = socket->readAll();
          if (!request.contains("\r\n\r\n"))
            return;
          QByteArray body =
              request.startsWith("GET")
                  ? QByteArray("wrong-genesis")
                  : QByteArray(R"({"jsonrpc":"2.0","id":1,"result":"0x5"})");
          socket->write("HTTP/1.1 200 OK\r\nContent-Length: " +
                        QByteArray::number(body.size()) +
                        "\r\nConnection: close\r\n\r\n" + body);
          socket->disconnectFromHost();
        });
        connect(socket, &QTcpSocket::disconnected, socket,
                &QObject::deleteLater);
      }
    });
    Network n;
    auto base = QString("http://127.0.0.1:%1").arg(server.serverPort());
    n.setEndpoints({base, base, base, "ws://127.0.0.1:1", base, base, base});
    int done = 0;
    QStringList errors;
    n.verifiedUtxos(Chain::Litecoin, [&](QVector<Utxo>, QString error) {
      errors << error;
      ++done;
    });
    n.solRpc("getBalance", QJsonArray{"ignored"},
             [&](QJsonValue, QString error) {
               errors << error;
               ++done;
             });
    n.evmRpc(Chain::Ethereum, "eth_getBalance", QJsonArray{"ignored", "latest"},
             [&](QJsonValue, QString error) {
               errors << error;
               ++done;
             });
    QTRY_COMPARE_WITH_TIMEOUT(done, 3, 5000);
    for (const auto &error : errors)
      QVERIFY2(!error.isEmpty(), "Wrong network was accepted");
  }
  void connectionErrorsAreVisible() {
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    int status = 503;
    connect(&server, &QTcpServer::newConnection, this, [&] {
      while (server.hasPendingConnections()) {
        auto *s = server.nextPendingConnection();
        connect(s, &QTcpSocket::readyRead, s, [&, s] {
          auto request = s->readAll();
          if (!request.contains("\r\n\r\n"))
            return;
          QByteArray body = "unavailable";
          if (status == 200) {
            body =
                request.contains("/block-height/0")
                    ? QByteArray("12a765e31ffd4059bada1e25190f6e98c99d9714d334e"
                                 "fa41a195a7e7e04bfe2")
                    : QByteArray("{\"chain_stats\":{\"funded_txo_sum\":0,"
                                 "\"spent_txo_sum\":0},\"mempool_stats\":{"
                                 "\"funded_txo_sum\":0,\"spent_txo_sum\":0}}");
          }
          s->write(
              "HTTP/1.1 " + QByteArray::number(status) +
              " Status\r\nContent-Length: " + QByteArray::number(body.size()) +
              "\r\nConnection: close\r\n\r\n" + body);
          s->disconnectFromHost();
        });
        connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
      }
    });
    Network n;
    QCOMPARE(n.connectionStatus(Chain::Litecoin), QString("Wallet locked"));
    auto settings = n.endpoints();
    settings.litecoin = QString("http://127.0.0.1:%1").arg(server.serverPort());
    n.setEndpoints(settings);
    std::array<QString, AssetCount> accounts;
    accounts[1] = "ltc1qw508d6qejxtdg4y5r3zarvary0c5xw7kgmn4n9";
    n.setAddresses(accounts);
    QCOMPARE(n.connectionStatus(Chain::Litecoin), QString("Connecting…"));
    QTRY_VERIFY_WITH_TIMEOUT(!n.state(Chain::Litecoin).refreshing, 5000);
    QVERIFY(!n.state(Chain::Litecoin).known);
    QVERIFY(!n.connectionStatus(Chain::Litecoin).contains("Awaiting"));
    QCOMPARE(n.connectionStatus(Chain::Litecoin),
             QString("Node returned HTTP 503"));
    status = 200;
    n.refresh();
    QTRY_VERIFY_WITH_TIMEOUT(n.state(Chain::Litecoin).known, 5000);
    QCOMPARE(n.connectionStatus(Chain::Litecoin), QString("Synced · HTTPS"));
    status = 503;
    n.refresh();
    QTRY_VERIFY_WITH_TIMEOUT(!n.state(Chain::Litecoin).refreshing, 5000);
    QVERIFY(n.state(Chain::Litecoin)
                .known); // Keep the last balance, not a false connected status.
    QCOMPARE(n.connectionStatus(Chain::Litecoin),
             QString("Node returned HTTP 503"));
    n.pause();
    QCOMPARE(n.connectionStatus(Chain::Litecoin), QString("Wallet locked"));
  }
  void mockNetworkBalances() {
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    QList<QByteArray> requests;
    connect(&server, &QTcpServer::newConnection, this, [&] {
      while (server.hasPendingConnections()) {
        auto *s = server.nextPendingConnection();
        auto bytes = std::make_shared<QByteArray>();
        connect(s, &QTcpSocket::readyRead, s, [&, s, bytes] {
          bytes->append(s->readAll());
          if (!bytes->contains("\r\n\r\n"))
            return;
          QByteArray body;
          if (bytes->contains("/block-height/0")) {
            body = bytes->contains("/btc/")
                       ? "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f"
                         "1b60a8ce26f"
                       : "12a765e31ffd4059bada1e25190f6e98c99d9714d334efa41a195"
                         "a7e7e04bfe2";
          } else if (bytes->startsWith("POST")) {
            int head = bytes->indexOf("\r\n\r\n");
            if (bytes->size() < head + 4)
              return;
            auto request =
                QJsonDocument::fromJson(bytes->mid(head + 4)).object();
            auto method = request.value("method").toString();
            QJsonValue result;
            if (method == "getGenesisHash")
              result = "5eykt4UsFv8P8NJdTREpY1vzqKqZKvdpKuc147dw2N9d";
            else if (method == "eth_chainId")
              result = bytes->contains("POST /eth")   ? "0x1"
                       : bytes->contains("POST /pol") ? "0x89"
                                                      : "0x38";
            else if (method == "eth_getBalance" || method == "eth_call")
              result = "0xde0b6b3a7640000";
            else if (method == "getTokenAccountsByOwner")
              result = QJsonObject{{"value", QJsonArray{}}};
            else
              result = QJsonObject{{"value", QJsonValue(1000000001)}};
            body = QJsonDocument(QJsonObject{{"jsonrpc", "2.0"},
                                             {"id", 1},
                                             {"result", result}})
                       .toJson(QJsonDocument::Compact);
          } else
            body = "{\"chain_stats\":{\"funded_txo_sum\":123456789,\"spent_txo_"
                   "sum\":100000000},\"mempool_stats\":{\"funded_txo_sum\":100,"
                   "\"spent_txo_sum\":0}}";
          requests << *bytes;
          s->write("HTTP/1.1 200 OK\r\nContent-Type: "
                   "application/json\r\nContent-Length: " +
                   QByteArray::number(body.size()) +
                   "\r\nConnection: close\r\n\r\n" + body);
          s->disconnectFromHost();
        });
        connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
      }
    });
    Network n;
    QString base = QString("http://127.0.0.1:%1").arg(server.serverPort());
    n.setEndpoints({base + "/btc", base + "/ltc", base + "/sol",
                    "ws://127.0.0.1:1", base + "/eth", base + "/pol",
                    base + "/bnb"});
    std::array<QString, AssetCount> accounts;
    unsigned char key[32] = {};
    key[31] = 1;
    for (int i = 0; i < AssetCount; ++i) {
      if (static_cast<Chain>(i) != Chain::Monero)
        accounts[i] = address(key, static_cast<Chain>(i));
    }
    n.setAddresses(accounts);
    QTRY_VERIFY_WITH_TIMEOUT(n.state(Chain::Bitcoin).known &&
                                 n.state(Chain::Litecoin).known &&
                                 n.state(Chain::Solana).known,
                             15000);
    for (int i = 0; i < AssetCount; ++i) {
      if (static_cast<Chain>(i) != Chain::Monero)
        QTRY_VERIFY_WITH_TIMEOUT(n.state(static_cast<Chain>(i)).known, 15000);
    }
    QCOMPARE(n.state(Chain::Bitcoin).balance, 23456789ULL);
    QCOMPARE(n.state(Chain::Bitcoin).pending, 100LL);
    QCOMPARE(n.state(Chain::Solana).balance, 1000000001ULL);
    QCOMPARE(n.state(Chain::Ethereum).units, QString("1000000000000000000"));
    QCOMPARE(n.state(Chain::UsdcSolana).balance, 0ULL);
    n.pause();
  }
};
QTEST_GUILESS_MAIN(CoreTests)
#include "core_tests.moc"
