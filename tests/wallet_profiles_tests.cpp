#include "signer.h"
#include "wallet_profiles.h"

#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

using namespace aurum;

class WalletProfilesTests : public QObject {
  Q_OBJECT
private slots:
  void createsIndependentProfiles() {
    QTemporaryDir data;
    QVERIFY(data.isValid());
    const auto settings = data.filePath("settings.ini");
    WalletProfiles profiles(data.path(), settings);
    QCOMPARE(profiles.activeId(), QString("primary"));
    const auto primary = profiles.active();
    QCOMPARE(primary.id, QString("primary"));
    QCOMPARE(primary.vaultPath, data.filePath("wallet.aurum"));
    QCOMPARE(primary.moneroDirectory, data.filePath("monero"));
    const auto first = profiles.add("Trading wallet");
    const auto second = profiles.add("Savings wallet");
    QVERIFY(first.id != second.id);
    QVERIFY(first.vaultPath != second.vaultPath);
    QVERIFY(first.moneroDirectory != second.moneroDirectory);
    QVERIFY(QFileInfo(first.vaultPath).dir().isReadable());
    QCOMPARE(QFileInfo(first.vaultPath).dir().absolutePath(),
             QFileInfo(data.filePath("wallets/" + first.id)).absoluteFilePath());
    QVERIFY(!QFileInfo(first.vaultPath).exists());
    profiles.setActive(second.id);
    QCOMPARE(profiles.active().id, second.id);

    WalletProfiles reopened(data.path(), settings);
    QCOMPARE(reopened.all().size(), 3);
    QCOMPARE(reopened.active().id, second.id);
    QCOMPARE(reopened.get(first.id).name, QString("Trading wallet"));
    QVERIFY_EXCEPTION_THROWN(reopened.get("../../outside"), std::runtime_error);

    QByteArray firstPassword("profile one test password");
    const auto firstWallet = openIsolated(first.vaultPath, firstPassword, true);
    QVERIFY(firstPassword.isEmpty());
    QByteArray secondPassword("profile two test password");
    const auto secondWallet = openIsolated(second.vaultPath, secondPassword, true);
    QVERIFY(secondPassword.isEmpty());
    QVERIFY(firstWallet.addresses != secondWallet.addresses);
    const auto privateBits = QFileDevice::ReadGroup | QFileDevice::WriteGroup |
                             QFileDevice::ExeGroup | QFileDevice::ReadOther |
                             QFileDevice::WriteOther | QFileDevice::ExeOther;
    QVERIFY(!(QFileInfo(first.vaultPath).permissions() & privateBits));
    QVERIFY(!(QFileInfo(second.vaultPath).permissions() & privateBits));
  }
};

QTEST_MAIN(WalletProfilesTests)
#include "wallet_profiles_tests.moc"
