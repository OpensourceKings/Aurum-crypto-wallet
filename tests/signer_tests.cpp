#include "signer.h"
#include "spl.h"
#include <QCoreApplication>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtTest>
#include <fcntl.h>
#include <sys/uio.h>
#include <unistd.h>
using namespace aurum;
class SignerTests : public QObject {
  Q_OBJECT
private slots:
  void runningHelperRejectsMemoryReads() {
    QProcess helper;
    helper.start(QCoreApplication::applicationDirPath() + "/aurum-signer", {});
    QVERIFY(helper.waitForStarted());
    QByteArray metadata("{\"op\":\"inspect\"}");
    quint32 header[] = {qToBigEndian(quint32(metadata.size())),
                        qToBigEndian(quint32(20))};
    helper.write(reinterpret_cast<const char *>(header), sizeof header);
    helper.write(metadata);
    QVERIFY(helper.waitForBytesWritten());
    auto denied = [&] {
      unsigned char output;
      iovec local{&output, 1}, remote{reinterpret_cast<void *>(1), 1};
      errno = 0;
      auto n = process_vm_readv(helper.processId(), &local, 1, &remote, 1, 0);
      return n == -1 && errno == EPERM;
    };
    QTRY_VERIFY_WITH_TIMEOUT(denied(), 3000);
    auto path =
        QByteArray("/proc/") + QByteArray::number(helper.processId()) + "/mem";
    int fd = open(path.constData(), O_RDONLY);
    if (fd >= 0)
      close(fd);
    QVERIFY(fd < 0);
    helper.kill();
    QVERIFY(helper.waitForFinished());
  }
  void isolatedOperations() {
    QTemporaryDir directory;
    auto file = directory.filePath("wallet.aurum");
    QByteArray pass("isolated test password");
    const auto created = openIsolated(file, pass, true);
    QVERIFY(pass.isEmpty());
    pass = "isolated test password";
    const auto reopened = openIsolated(file, pass);
    QCOMPARE(reopened.publicKeys, created.publicKeys);
    QCOMPARE(reopened.addresses, created.addresses);
    pass = "wrong test password";
    QVERIFY_EXCEPTION_THROWN(openIsolated(file, pass), std::runtime_error);
    QVERIFY(pass.isEmpty());
    // Test-only oracle: the production GUI never opens a plaintext Vault.
    Vault oracle;
    pass = "isolated test password";
    oracle.unlock(file, pass);
    for (int i = 0; i < AssetCount; ++i) {
      auto c = static_cast<Chain>(i);
      if (c == Chain::Monero)
        continue;
      QCOMPARE(created.publicKeys[i], publicKey(oracle.key(c), c));
      QCOMPARE(created.addresses[i], address(oracle.key(c), c));
      pass = "isolated test password";
      SignedTransaction got, expected;
      if (i < 2) {
        QVector<Utxo> inputs{{QByteArray(32, char(i + 1)), 0, 10000000}};
        got = signUtxoIsolated(file, pass, created.publicKeys[i], c, inputs,
                               created.addresses[i], 1000000, 2);
        expected = signUtxo(oracle.key(c), c, inputs, created.addresses[i],
                            1000000, 2);
      } else if (isEvm(c)) {
        EvmTransfer t;
        t.chain = chainId(c);
        t.nonce = 7;
        t.gasPrice = "1000000000";
        t.to = "0x3535353535353535353535353535353535353535";
        t.value = "123456789";
        if (isToken(c)) {
          t.to = tokenAddress(c);
          t.value = "0";
          t.gasLimit = 70000;
          t.data = erc20Transfer("0x3535353535353535353535353535353535353535",
                                 "123456");
        }
        got = signEvmIsolated(file, pass, created.publicKeys[i], c, t);
        expected = signEvm(oracle.key(c), t);
      } else {
        QByteArray recipient(32, char(6)), hash(32, char(9));
        QByteArray message =
            isSpl(c)
                ? splTransferMessage(
                      created.publicKeys[i], recipient,
                      associatedTokenAddress(created.publicKeys[i],
                                             unbase58(tokenAddress(c))),
                      unbase58(tokenAddress(c)), hash, 123456)
                : solanaMessage(created.publicKeys[i], recipient, hash, 123456);
        got = signSolanaIsolated(file, pass, created.publicKeys[i], c, message);
        expected = signSolana(oracle.key(c), message);
      }
      QVERIFY(pass.isEmpty());
      QCOMPARE(got.wire, expected.wire);
      QCOMPARE(got.id, expected.id);
      QCOMPARE(got.fee, expected.fee);
    }
    oracle.lock();
    pass = "isolated test password";
    EvmTransfer bad;
    bad.chain = 56;
    bad.gasPrice = "1";
    bad.value = "1";
    bad.to = created.addresses[3];
    QVERIFY_EXCEPTION_THROWN(signEvmIsolated(file, pass, created.publicKeys[3],
                                             Chain::Ethereum, bad),
                             std::runtime_error);
    QVERIFY(pass.isEmpty());
    pass = "isolated test password";
    bad.chain = 1;
    QVERIFY_EXCEPTION_THROWN(
        signEvmIsolated(file, pass, QByteArray(65, 'x'), Chain::Ethereum, bad),
        std::runtime_error);
    pass = "isolated test password";
    bad.to = tokenAddress(Chain::UsdcEthereum);
    bad.value = "0";
    bad.gasLimit = 70000;
    bad.data = erc20Transfer(created.addresses[3], "1000");
    bad.data.replace(
        0, 4, QByteArray::fromHex("095ea7b3")); // approve is never permitted.
    QVERIFY_EXCEPTION_THROWN(signEvmIsolated(file, pass, created.publicKeys[7],
                                             Chain::UsdcEthereum, bad),
                             std::runtime_error);
    pass = "isolated test password";
    auto wrongMint = splTransferMessage(
        created.publicKeys[8], QByteArray(32, 6),
        associatedTokenAddress(created.publicKeys[8],
                               unbase58(tokenAddress(Chain::UsdcSolana))),
        unbase58(tokenAddress(Chain::UsdcSolana)), QByteArray(32, 9), 1000);
    QVERIFY_EXCEPTION_THROWN(signSolanaIsolated(file, pass,
                                                created.publicKeys[8],
                                                Chain::UsdtSolana, wrongMint),
                             std::runtime_error);
    pass = "isolated test password";
    auto message = solanaMessage(created.publicKeys[2], QByteArray(32, 6),
                                 QByteArray(32, 9), 1000);
    message[0] = 2;
    QVERIFY_EXCEPTION_THROWN(signSolanaIsolated(file, pass,
                                                created.publicKeys[2],
                                                Chain::Solana, message),
                             std::runtime_error);
    QFile corrupt(file);
    QVERIFY(corrupt.open(QIODevice::ReadWrite));
    QVERIFY(corrupt.seek(60));
    QVERIFY(corrupt.write("x", 1) == 1);
    corrupt.close();
    pass = "isolated test password";
    QVERIFY_EXCEPTION_THROWN(openIsolated(file, pass), std::runtime_error);
    QVERIFY(pass.isEmpty());
  }
  void missingFileClearsPassword() {
    QByteArray pass("isolated test password");
    QVERIFY_EXCEPTION_THROWN(openIsolated("/nonexistent/wallet.aurum", pass),
                             std::runtime_error);
    QVERIFY(pass.isEmpty());
  }
};
QTEST_GUILESS_MAIN(SignerTests)
#include "signer_tests.moc"
