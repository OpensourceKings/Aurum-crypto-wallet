#include "monero.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using namespace aurum;

class MoneroTests : public QObject {
  Q_OBJECT
private slots:
  void rpcWalletLifecycle() {
    const auto binary = qEnvironmentVariable("AURUM_MONERO_WALLET_RPC");
    if (binary.isEmpty() || !QFileInfo(binary).isExecutable())
      QSKIP("AURUM_MONERO_WALLET_RPC is not set to the verified CLI binary");
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    MoneroClient client;
    client.setRpcBinary(binary);
    client.setWalletDirectory(dir.path());
    client.setNode("http://127.0.0.1:18081");
    bool done = false;
    QString error;
    client.start(QByteArray("monero integration password"),
                 [&](QString e) { error = e; done = true; });
    QTRY_VERIFY_WITH_TIMEOUT(done, 15000);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(client.state().address.size(), 95);
    QVERIFY(client.running());
    const QFileInfo wallet(client.walletFile());
    const QFileInfo keys(client.walletFile() + ".keys");
    QVERIFY(wallet.isFile());
    QVERIFY(keys.isFile());
    QFile keysFile(keys.filePath());
    QVERIFY(keysFile.open(QIODevice::ReadOnly));
    const auto keysHash = QCryptographicHash::hash(keysFile.readAll(),
                                                   QCryptographicHash::Sha256);
    keysFile.close();
    const auto privateBits = QFileDevice::ReadGroup | QFileDevice::WriteGroup |
                             QFileDevice::ExeGroup | QFileDevice::ReadOther |
                             QFileDevice::WriteOther | QFileDevice::ExeOther;
    QVERIFY(!(wallet.permissions() & privateBits));
    QVERIFY(!(keys.permissions() & privateBits));

    bool transferDone = false;
    QString transferError;
    client.transfer(client.state().address, "0.01",
                    QByteArray("wrong integration password"),
                    [&](QString, QString e) {
                      transferError = e;
                      transferDone = true;
                    });
    QTRY_VERIFY_WITH_TIMEOUT(transferDone, 10000);
    QVERIFY(transferError.contains("synchronization", Qt::CaseInsensitive));
    QVERIFY(!client.state().known);
    client.stop();
    QVERIFY(!client.running());
    done = false;
    client.start(QByteArray("wrong integration password"),
                 [&](QString e) { error = e; done = true; });
    QTRY_VERIFY_WITH_TIMEOUT(done, 15000);
    QVERIFY(error.contains("password", Qt::CaseInsensitive));
    QVERIFY(!client.running());
    QVERIFY(keysFile.open(QIODevice::ReadOnly));
    QCOMPARE(QCryptographicHash::hash(keysFile.readAll(),
                                      QCryptographicHash::Sha256), keysHash);
  }

  void liveTorSynchronization() {
    if (!qEnvironmentVariableIsSet("AURUM_TEST_LIVE_MONERO"))
      QSKIP("Opt-in public node test requires running Tor");
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    MoneroClient client;
    client.setRpcBinary(qEnvironmentVariable("AURUM_MONERO_WALLET_RPC"));
    client.setWalletDirectory(dir.path());
    const auto node = qEnvironmentVariable("AURUM_TEST_MONERO_NODE");
    if (!node.isEmpty())
      client.setNode(node, "127.0.0.1:9050");
    QString lastStatus;
    connect(&client, &MoneroClient::changed, this, [&] {
      if (client.state().status != lastStatus) {
        lastStatus = client.state().status;
        qInfo().noquote() << lastStatus;
      }
    });
    bool done = false;
    QString error;
    client.start(QByteArray("temporary live Monero test password"),
                 [&](QString e) { error = e; done = true; });
    QTRY_VERIFY_WITH_TIMEOUT(done, 180000);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(client.state().address.size(), 95);
    const auto address = client.state().address;
    QTRY_VERIFY_WITH_TIMEOUT(client.state().known, 240000);
    QVERIFY2(client.state().status.startsWith("Synced"),
             qPrintable(client.state().status));
    QVERIFY(!client.state().refreshing);
    QCOMPARE(client.state().balance, quint64(0));
    client.stop();
    QVERIFY(!client.running());
  }
};

QTEST_MAIN(MoneroTests)
#include "monero_tests.moc"
