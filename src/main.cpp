#include "security.h"
#include "window.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QIcon>
#include <QLockFile>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>
#include <memory>
#include <sodium.h>
int main(int argc, char **argv) {
  try {
    aurum::protectProcess();
  } catch (const std::exception &e) {
    QTextStream(stderr) << e.what() << '\n';
    return 2;
  }
  QApplication app(argc, argv);
  app.setOrganizationName("AurumWallet");
  app.setApplicationName("Aurum");
  app.setApplicationVersion("0.7.2");
  app.setWindowIcon(QIcon(":/assets/aurum.svg"));
  app.setDesktopFileName("aurum");
  QCommandLineParser parser;
  parser.setApplicationDescription("Aurum native multi-network wallet");
  parser.addHelpOption();
  parser.addVersionOption();
  parser.addOption({"demo", "Offline design preview with sample balances. "
                            "Never loads a real vault."});
  parser.addOption({"screenshots",
                    "Capture all designs and a contact sheet, then exit.",
                    "directory"});
  parser.addOption({"theme", "Appearance index (1–11)", "number", "6"});
  parser.process(app);
  if (sodium_init() < 0)
    return 1;
  bool capture = parser.isSet("screenshots");
  std::unique_ptr<QLockFile> instance;
  if (!capture && !parser.isSet("demo")) {
    auto directory =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!QDir().mkpath(directory) ||
        !QFile::setPermissions(directory, QFileDevice::ReadOwner |
                                              QFileDevice::WriteOwner |
                                              QFileDevice::ExeOwner)) {
      QTextStream(stderr) << "Cannot secure wallet directory\n";
      return 2;
    }
    instance = std::make_unique<QLockFile>(directory + "/wallet.lock");
    if (!instance->tryLock(0)) {
      QTextStream(stderr) << "Wallet is already open\n";
      return 2;
    }
  }
  aurum::WalletWindow window(parser.isSet("demo") || capture);
  if (parser.isSet("theme"))
    window.setTheme(parser.value("theme").toInt() - 1);
  window.show();
  if (capture)
    QTimer::singleShot(200, &window, [&] {
      try {
        window.captureAll(parser.value("screenshots"));
        app.exit(0);
      } catch (const std::exception &e) {
        QTextStream(stderr) << e.what() << '\n';
        app.exit(1);
      }
    });
  return app.exec();
}
