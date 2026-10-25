#include "signer.h"
#include "window.h"
#include <QApplication>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFontInfo>
#include <QHelpEvent>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QSettings>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QToolTip>
#include <QtTest>
using namespace aurum;

static QPushButton *button(QWidget *w, const QString &name) {
  for (auto *b : w->findChildren<QPushButton *>())
    if (b->accessibleName() == name || b->text() == name)
      return b;
  return nullptr;
}
class UiTests : public QObject {
  Q_OBJECT
private slots:
  void switchAllAppearances() {
    QSettings settings;
    settings.remove("appearance");
    WalletWindow w(true);
    w.show();
    QFont balanceFont("Roboto");
    balanceFont.setWeight(QFont::Light);
    QCOMPARE(QFontInfo(balanceFont).family(), QString("Roboto"));
    QFont condensedFont("Roboto Condensed");
    condensedFont.setWeight(QFont::Light);
    QCOMPARE(QFontInfo(condensedFont).family(), QString("Roboto Condensed"));
    QCOMPARE(w.themeIndex(), 5);
    for (int i = 0; i < themes().size(); ++i) {
      bool selected = false;
      QTimer::singleShot(20, [&] {
        auto *d = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!d)
          return;
        auto *choice = d->findChild<QPushButton *>(QString("theme-%1").arg(i));
        if (!choice) {
          d->reject();
          return;
        }
        QCOMPARE(d->findChildren<QPushButton *>().size(), themes().size());
        choice->click();
        selected = true;
      });
      QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
      button(&w, "Choose appearance")->click();
      QVERIFY(selected);
      QCOMPARE(w.themeIndex(), i);
      WalletWindow saved;
      saved.setTheme(i);
      QCOMPARE(settings.value("appearance").toInt(), i);
    }
    WalletWindow reopened;
    QCOMPARE(reopened.themeIndex(), int(themes().size()) - 1);
    settings.setValue("appearance", 5);
  }
  void allDesigns() {
    WalletWindow w(true);
    w.show();
    QSet<QByteArray> hashes;
    for (int i = 0; i < themes().size(); ++i) {
      w.setTheme(i);
      QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
      QCoreApplication::processEvents();
      auto im = w.grab().toImage();
      hashes.insert(QCryptographicHash::hash(
          QByteArray::fromRawData(
              reinterpret_cast<const char *>(im.constBits()), im.sizeInBytes()),
          QCryptographicHash::Sha256));
      QVERIFY(button(&w, "Receive cryptocurrency"));
      QVERIFY(button(&w, "Send cryptocurrency"));
      QVERIFY(button(&w, "All assets"));
    }
    QCOMPARE(hashes.size(), themes().size());
  }
  void usdValuesAndHover() {
    WalletWindow w(true);
    w.setTheme(10);
    w.show();
    QCoreApplication::processEvents();
    bool visible = false;
    QTimer::singleShot(25, [&] {
      auto *d = qobject_cast<QDialog *>(QApplication::activeModalWidget());
      if (!d)
        return;
      for (int i = 0; i < AssetCount; ++i) {
        auto *value = d->findChild<QLabel *>(QString("holding-%1").arg(i));
        QVERIFY(value);
        QVERIFY(value->text().startsWith("$"));
        QVERIFY(value->toolTip().contains(symbol(Chain(i))));
        QVERIFY(value->toolTip().contains("USD"));
      }
      visible = true;
      d->reject();
    });
    button(&w, "All assets")->click();
    QVERIFY(visible);
    QHelpEvent hover(QEvent::ToolTip, QPoint(720, 330),
                     w.mapToGlobal(QPoint(720, 330)));
    QApplication::sendEvent(&w, &hover);
    QVERIFY(QToolTip::text().contains("BTC"));
    QVERIFY(QToolTip::text().contains("USD"));
    QToolTip::hideText();
    button(&w, "Select Litecoin")->click();
    QVERIFY(w.accessibleDescription().contains("32.456 LTC"));
    bool selected = false;
    QTimer::singleShot(25, [&] {
      auto *d = qobject_cast<QDialog *>(QApplication::activeModalWidget());
      if (!d)
        return;
      auto *choice = d->findChild<QComboBox *>();
      selected = choice && choice->currentIndex() == 1;
      d->reject();
    });
    button(&w, "Receive selected asset")->click();
    QVERIFY(selected);
    for (const auto &range : {"1D", "1W", "1M", "3M", "6M", "1Y"}) {
      auto *control = button(&w, QString("Price chart ") + range);
      QVERIFY(control);
      control->click();
    }
  }
  void interactivePriceChart() {
    WalletWindow w(true);
    w.setTheme(10);
    w.resize(1200, 760);
    w.show();
    QTest::mouseMove(&w, QPoint(260, 630));
    QCOMPARE(w.cursor().shape(), Qt::CrossCursor);
    QHelpEvent left(QEvent::ToolTip, QPoint(260, 630),
                    w.mapToGlobal(QPoint(260, 630)));
    QApplication::sendEvent(&w, &left);
    const QString first = QToolTip::text();
    QVERIFY(first.contains("USD"));
    QTest::mouseMove(&w, QPoint(950, 630));
    QHelpEvent right(QEvent::ToolTip, QPoint(950, 630),
                     w.mapToGlobal(QPoint(950, 630)));
    QApplication::sendEvent(&w, &right);
    QVERIFY(QToolTip::text() != first);
    button(&w, "Toggle price chart")->click();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QTest::mouseMove(&w, QPoint(500, 650));
    QCOMPARE(w.cursor().shape(), Qt::ArrowCursor);
    QVERIFY(!button(&w, "Price chart 1Y"));
    button(&w, "Toggle price chart")->click();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(button(&w, "Price chart 1Y"));
    QToolTip::hideText();
  }
  void createReceiveAndLock() {
    QSettings settings;
    for (const auto &key :
         {"bitcoin", "litecoin", "solana", "ethereum", "polygon", "bnb"})
      settings.setValue(QString("endpoints/") + key, "http://127.0.0.1:1");
    settings.setValue("endpoints/solanaWs", "ws://127.0.0.1:1");
    WalletWindow w;
    w.findChild<MarketData *>()->setEndpoint(QUrl("http://127.0.0.1:1/price"));
    w.show();
    auto *screen = w.findChild<QWidget *>("startup-screen");
    QVERIFY(screen && screen->isVisible());
    QCOMPARE(screen->geometry(), w.rect());
    QVERIFY(!button(&w, "Receive cryptocurrency"));
    auto *password = w.findChild<QLineEdit *>("startup-password");
    auto *confirmation = w.findChild<QLineEdit *>("startup-confirm");
    QVERIFY(password && confirmation);
    QCOMPARE(password->echoMode(), QLineEdit::Password);
    password->setText("temporary UI test password");
    confirmation->setText("temporary UI test password");
    w.findChild<QPushButton *>("startup-submit")->click();
    QTRY_VERIFY_WITH_TIMEOUT(
        w.findChild<QPushButton *>("loading-continue")->isVisible(), 5000);
    QVERIFY(password->text().isEmpty());
    QVERIFY(confirmation->text().isEmpty());
    auto *continueButton = w.findChild<QPushButton *>("loading-continue");
    QTRY_VERIFY_WITH_TIMEOUT(continueButton->isEnabled(), 25000);
    QVERIFY(screen->isVisible());
    const auto loadingStatus = w.findChild<QLabel *>("loading-status")->text();
    QVERIFY(loadingStatus.contains("syncing") ||
            loadingStatus.contains("unavailable"));
    continueButton->click();
    QVERIFY(!screen->isVisible());
    auto path =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
        "/wallet.aurum";
    QVERIFY(QFile::exists(path));
    Vault vault;
    QByteArray pass("temporary UI test password");
    vault.unlock(path, pass);
    std::array<QString, AssetCount> expected;
    for (int i = 0; i < AssetCount; ++i) {
      if (static_cast<Chain>(i) == Chain::Monero)
        continue;
      expected[i] =
          address(vault.key(static_cast<Chain>(i)), static_cast<Chain>(i));
    }
    vault.lock();
    bool addressesCorrect = true, qrPresent = false;
    QTimer::singleShot(50, [&] {
      auto *d = qobject_cast<QDialog *>(QApplication::activeModalWidget());
      if (!d) {
        addressesCorrect = false;
        return;
      }
      auto *selector = d->findChild<QComboBox *>();
      auto *field = d->findChild<QLineEdit *>();
      if (!selector || !field) {
        addressesCorrect = false;
        d->reject();
        return;
      }
      for (int i = 0; i < AssetCount; ++i) {
        selector->setCurrentIndex(i);
        addressesCorrect &= i == static_cast<int>(Chain::Monero)
                                ? field->text().isEmpty() ||
                                      field->text().startsWith("4")
                                : field->text() == expected[i];
        addressesCorrect &= !selector->itemIcon(i).pixmap(32, 32).isNull();
        for (auto *l : d->findChildren<QLabel *>())
          qrPresent |= !l->pixmap().isNull();
      }
      d->reject();
    });
    button(&w, "Receive cryptocurrency")->click();
    QVERIFY(addressesCorrect);
    QVERIFY(qrPresent);
    QVERIFY(w.findChild<MarketData *>());
    QVERIFY(password->text().isEmpty());
    QVERIFY(confirmation->text().isEmpty());
    button(&w, "Lock wallet")->click();
    QVERIFY(screen->isVisible());
    QVERIFY(!confirmation->isVisible());
    QVERIFY(!QApplication::activeModalWidget());
    password->setText("an incorrect password");
    w.findChild<QPushButton *>("startup-submit")->click();
    QTRY_VERIFY_WITH_TIMEOUT(
        w.findChild<QPushButton *>("startup-submit")->isEnabled(), 5000);
    QVERIFY(screen->isVisible());
    QVERIFY(password->text().isEmpty());
    QVERIFY(!button(&w, "Receive cryptocurrency"));
    w.resize(1180, 940);
    QCOMPARE(screen->geometry(), w.rect());
  }
  void walletPickerOpensWhileLocked() {
    WalletWindow w;
    w.show();
    QVERIFY(button(&w, "Wallets"));
    bool opened = false;
    QTimer::singleShot(25, [&] {
      auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
      if (!dialog)
        return;
      opened = dialog->windowTitle() == "Wallets" &&
               button(dialog, "Add wallet") && button(dialog, "Open selected");
      dialog->reject();
    });
    button(&w, "Wallets")->click();
    QVERIFY(opened);
  }
  void loadAllAssetsBeforeShowingWallet() {
    QTemporaryDir data;
    const QByteArray old = qgetenv("XDG_DATA_HOME");
    qputenv("XDG_DATA_HOME", data.path().toUtf8());
    const QString path =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
        "/wallet.aurum";
    QByteArray password("isolated loading test password");
    (void)openIsolated(path, password, true);
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    connect(&server, &QTcpServer::newConnection, this, [&] {
      auto *socket = server.nextPendingConnection();
      auto input = std::make_shared<QByteArray>();
      connect(socket, &QTcpSocket::readyRead, socket, [&, socket, input] {
        *input += socket->readAll();
        const int split = input->indexOf("\r\n\r\n");
        if (split < 0)
          return;
        const QByteArray header = input->left(split),
                         payload = input->mid(split + 4);
        int length = 0;
        for (auto line : header.split('\n'))
          if (line.toLower().startsWith("content-length:"))
            length = line.mid(15).trimmed().toInt();
        if (payload.size() < length)
          return;
        const QByteArray url = header.split(' ').value(1);
        QByteArray body;
        if (url.startsWith("/current-price") || url.startsWith("/ticker")) {
          QJsonObject prices;
          for (int i = 0; i < AssetCount; ++i)
            prices[MarketData::priceSymbol(Chain(i))] =
                QJsonObject{{"USD", 64.05}};
          body = QJsonDocument(prices).toJson(QJsonDocument::Compact);
        } else if (url.contains("/block-height/0")) {
          body = url.startsWith("/btc/") ? "000000000019d6689c085ae165831e934ff"
                                           "763ae46a2a6c172b3f1b60a8ce26f"
                                         : "12a765e31ffd4059bada1e25190f6e98c99"
                                           "d9714d334efa41a195a7e7e04bfe2";
        } else if (url.contains("/address/")) {
          body =
              "{\"chain_stats\":{\"funded_txo_sum\":0,\"spent_txo_sum\":0},"
              "\"mempool_stats\":{\"funded_txo_sum\":0,\"spent_txo_sum\":0}}";
        } else {
          const auto request = QJsonDocument::fromJson(payload).object();
          const QString method = request["method"].toString();
          QJsonValue result;
          if (method == "getGenesisHash")
            result = "5eykt4UsFv8P8NJdTREpY1vzqKqZKvdpKuc147dw2N9d";
          else if (method == "getBalance")
            result = QJsonObject{{"value", 0}};
          else if (method == "getTokenAccountsByOwner")
            result = QJsonObject{{"value", QJsonArray{}}};
          else if (method == "eth_chainId")
            result = url == "/pol" ? "0x89" : url == "/bnb" ? "0x38" : "0x1";
          else
            result = "0x0";
          body = QJsonDocument(QJsonObject{{"jsonrpc", "2.0"},
                                           {"id", 1},
                                           {"result", result}})
                     .toJson(QJsonDocument::Compact);
        }
        socket->write(
            "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: " +
            QByteArray::number(body.size()) + "\r\n\r\n" + body);
        socket->disconnectFromHost();
      });
      connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    });
    QSettings settings;
    const QString base =
        "http://127.0.0.1:" + QString::number(server.serverPort());
    settings.setValue("endpoints/bitcoin", base + "/btc");
    settings.setValue("endpoints/litecoin", base + "/ltc");
    settings.setValue("endpoints/solana", base + "/sol");
    settings.setValue("endpoints/solanaWs", "ws://127.0.0.1:1");
    settings.setValue("endpoints/ethereum", base + "/eth");
    settings.setValue("endpoints/polygon", base + "/pol");
    settings.setValue("endpoints/bnb", base + "/bnb");
    WalletWindow w;
    w.show();
    w.findChild<MarketData *>()->setEndpoint(QUrl(base));
    auto *screen = w.findChild<QWidget *>("startup-screen");
    QVERIFY(screen->isVisible());
    w.findChild<QLineEdit *>("startup-password")
        ->setText("isolated loading test password");
    w.findChild<QPushButton *>("startup-submit")->click();
    QTRY_VERIFY_WITH_TIMEOUT(!screen->isVisible(), 15000);
    for (int i = 0; i < AssetCount; ++i) {
      if (static_cast<Chain>(i) == Chain::Monero)
        continue;
      QVERIFY(w.findChild<Network *>()->state(Chain(i)).known);
      QVERIFY(w.findChild<MarketData *>()->quote(Chain(i)).fresh());
    }
    QVERIFY(button(&w, "Receive cryptocurrency"));
    qputenv("XDG_DATA_HOME", old);
  }
};
int main(int argc, char **argv) {
  QTemporaryDir temp;
  qputenv("XDG_DATA_HOME", (temp.path() + "/data").toUtf8());
  qputenv("XDG_CONFIG_HOME", (temp.path() + "/config").toUtf8());
  QApplication app(argc, argv);
  app.setOrganizationName("AurumUiTests");
  app.setApplicationName("TestWallet");
  UiTests tests;
  return QTest::qExec(&tests, argc, argv);
}
#include "ui_tests.moc"
