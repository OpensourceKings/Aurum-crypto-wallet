#include "signer.h"
#include "window.h"
#include <QAction>
#include <QApplication>
#include <QBoxLayout>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainterPath>
#include <QProgressBar>
#include <QSaveFile>
#include <QThread>
#include <memory>
namespace aurum {
namespace {
class StartupBackdrop final : public QWidget {
public:
  using QWidget::QWidget;

protected:
  void paintEvent(QPaintEvent *) override {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QLinearGradient base(0, 0, width(), height());
    base.setColorAt(0, QColor("#202443"));
    base.setColorAt(.55, QColor("#262139"));
    base.setColorAt(1, QColor("#161c20"));
    p.fillRect(rect(), base);
    p.save();
    p.scale(width() / 1200.0,
            height() / (property("loading").toBool() ? 1300.0 : 800.0));
    const QVector<QPolygonF> facets{
        {{0, 0}, {210, 0}, {126, 184}, {0, 120}},
        {{0, 0}, {126, 184}, {90, 85}},
        {{210, 0}, {380, 0}, {309, 145}, {126, 184}},
        {{210, 0}, {309, 145}, {273, 0}},
        {{380, 0}, {505, 0}, {552, 166}, {431, 78}, {309, 145}},
        {{505, 0}, {700, 0}, {552, 166}},
        {{700, 0}, {887, 0}, {830, 157}, {725, 220}},
        {{700, 0}, {725, 220}, {552, 166}},
        {{887, 0}, {1048, 0}, {975, 126}, {830, 157}},
        {{1048, 0}, {1200, 0}, {1200, 154}, {1080, 109}},
        {{887, 0}, {975, 126}, {1048, 0}},
        {{1200, 0}, {1080, 109}, {1048, 0}}};
    const QColor colors[]{"#476df3", "#4d9efb", "#6669ec", "#6e83f4",
                          "#6976e9", "#9b6beb", "#4d74e8", "#6a69d3",
                          "#4386df", "#3aa9e2", "#5574de", "#408ccf"};
    p.setPen(Qt::NoPen);
    for (int i = 0; i < facets.size(); ++i) {
      p.setBrush(colors[i]);
      p.drawPolygon(facets[i]);
    }
    p.restore();
  }
};
QLabel *textLabel(const QString &text, int size, const QString &color) {
  auto *label = new QLabel(text);
  label->setAlignment(Qt::AlignCenter);
  label->setTextFormat(Qt::PlainText);
  label->setWordWrap(true);
  label->setStyleSheet(
      QString("color:%1;font-size:%2px;background:transparent;")
          .arg(color)
          .arg(size));
  return label;
}
} // namespace
WalletWindow::~WalletWindow() {
  if (unlockWorker_ && unlockWorker_->isRunning())
    unlockWorker_->wait();
}
void WalletWindow::setupStartup() {
  startupScreen_ = new StartupBackdrop(this);
  startupScreen_->setObjectName("startup-screen");
  startupScreen_->setFont(QFont("Roboto"));
  startupScreen_->setGeometry(rect());
  startupScreen_->setStyleSheet(
      "QWidget{background:transparent;} "
      "QLineEdit{color:#f4f2ff;border:0;border-bottom:1px solid "
      "#aaa9bf;border-radius:0;padding:12px;font-size:18px;selection-"
      "background-color:#695cbd;}"
      "QLineEdit:focus{border-bottom:2px solid #aaa3f2;} "
      "QPushButton{color:#aaa8bd;background:transparent;border:0;padding:10px;"
      "font-size:13px;}"
      "QPushButton:hover{color:white;} QPushButton:focus{border:1px solid "
      "#8b86c4;} QPushButton:disabled{color:#716d8d;}"
      "QProgressBar{border:0;background:#343247;border-radius:3px;min-height:"
      "5px;max-height:5px;}QProgressBar::chunk{background:#9688f4;border-"
      "radius:3px;}");
  auto *outer = new QVBoxLayout(startupScreen_);
  outer->setContentsMargins(20, 35, 20, 30);
  outer->setSpacing(0);
  outer->addStretch(1);
  auto *center = new QWidget;
  center->setFixedWidth(610);
  auto *layout = new QVBoxLayout(center);
  layout->setContentsMargins(24, 0, 24, 0);
  layout->setSpacing(15);
  auto *logo = new QLabel;
  logo->setAlignment(Qt::AlignCenter);
  QPixmap logoImage(86, 86);
  logoImage.fill(Qt::transparent);
  {
    QPainter painter(&logoImage);
    painter.setRenderHint(QPainter::Antialiasing);
    QPainterPath mark;
    mark.moveTo(43, 2);
    mark.lineTo(78, 43);
    mark.lineTo(43, 84);
    mark.lineTo(8, 43);
    mark.closeSubpath();
    mark.moveTo(43, 23);
    mark.lineTo(25, 43);
    mark.lineTo(43, 64);
    mark.lineTo(61, 43);
    mark.closeSubpath();
    mark.setFillRule(Qt::OddEvenFill);
    QLinearGradient color(8, 2, 78, 84);
    color.setColorAt(0, QColor("#ba8dff"));
    color.setColorAt(1, QColor("#58a6ef"));
    painter.fillPath(mark, color);
  }
  logo->setPixmap(logoImage);
  layout->addWidget(logo);
  startupTitle_ = textLabel("Unlock to Continue", 36, "#f2efff");
  QFont titleFont("Roboto Condensed");
  titleFont.setWeight(QFont::Light);
  titleFont.setPixelSize(36);
  startupTitle_->setFont(titleFont);
  layout->addWidget(startupTitle_);
  startupSubtitle_ = textLabel("", 15, "#a9a7be");
  layout->addWidget(startupSubtitle_);
  credentials_ = new QWidget;
  auto *form = new QVBoxLayout(credentials_);
  form->setContentsMargins(0, 8, 0, 0);
  form->setSpacing(14);
  startupPassword_ = new QLineEdit;
  startupPassword_->setObjectName("startup-password");
  startupPassword_->setAccessibleName("Wallet password");
  startupPassword_->setEchoMode(QLineEdit::Password);
  startupPassword_->setInputMethodHints(
      Qt::ImhHiddenText | Qt::ImhSensitiveData | Qt::ImhNoPredictiveText);
  startupPassword_->setPlaceholderText("Type your password…");
  startupPassword_->setAlignment(Qt::AlignLeft);
  QPixmap eye(24, 24);
  eye.fill(Qt::transparent);
  {
    QPainter painter(&eye);
    painter.drawPixmap(0, 0, QIcon(":/assets/ui/eye.svg").pixmap(24, 24));
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(eye.rect(), QColor("#aaa8bd"));
  }
  auto *reveal =
      startupPassword_->addAction(QIcon(eye), QLineEdit::LeadingPosition);
  reveal->setToolTip("Show password");
  connect(reveal, &QAction::triggered, this, [this, reveal] {
    const bool show = startupPassword_->echoMode() == QLineEdit::Password;
    startupPassword_->setEchoMode(show ? QLineEdit::Normal
                                       : QLineEdit::Password);
    reveal->setToolTip(show ? "Hide password" : "Show password");
  });
  startupPassword_->setMaxLength(1024);
  form->addWidget(startupPassword_);
  startupConfirm_ = new QLineEdit;
  startupConfirm_->setObjectName("startup-confirm");
  startupConfirm_->setEchoMode(QLineEdit::Password);
  startupConfirm_->setInputMethodHints(
      Qt::ImhHiddenText | Qt::ImhSensitiveData | Qt::ImhNoPredictiveText);
  startupConfirm_->setPlaceholderText("Confirm password");
  startupConfirm_->setAlignment(Qt::AlignCenter);
  startupConfirm_->setMaxLength(1024);
  form->addWidget(startupConfirm_);
  unlockError_ = textLabel("", 13, "#f3a0b3");
  unlockError_->setMinimumHeight(21);
  form->addWidget(unlockError_);
  startupSubmit_ = new QPushButton("Unlock");
  startupSubmit_->setObjectName("startup-submit");
  startupSubmit_->setCursor(Qt::PointingHandCursor);
  startupSubmit_->setMinimumHeight(44);
  startupSubmit_->setStyleSheet(
      "QPushButton{background:qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 "
      "#6950ca,stop:1 "
      "#3b68c5);color:#fff;border:0;border-radius:22px;padding:11px "
      "30px;font-size:14px;}QPushButton:hover{background:#7865da;}QPushButton:"
      "disabled{background:#393754;color:#9c97b3;}");
  form->addWidget(startupSubmit_, 0, Qt::AlignHCenter);
  connect(startupSubmit_, &QPushButton::clicked, this,
          &WalletWindow::submitUnlock);
  connect(startupPassword_, &QLineEdit::returnPressed, this,
          &WalletWindow::submitUnlock);
  connect(startupConfirm_, &QLineEdit::returnPressed, this,
          &WalletWindow::submitUnlock);
  layout->addWidget(credentials_);
  loading_ = new QWidget;
  auto *loadLayout = new QVBoxLayout(loading_);
  loadLayout->setContentsMargins(0, 8, 0, 0);
  loadLayout->setSpacing(16);
  loadCaption_ = textLabel("", 14, "#b7b3cf");
  loadLayout->addWidget(loadCaption_);
  loadProgress_ = new QProgressBar;
  loadProgress_->setTextVisible(false);
  loadProgress_->setRange(0, AssetCount);
  loadLayout->addWidget(loadProgress_);
  auto *grid = new QGridLayout;
  grid->setHorizontalSpacing(24);
  grid->setVerticalSpacing(10);
  for (int i = 0; i < AssetCount; ++i) {
    loadRows_[i] = textLabel(assetName(Chain(i)), 12, "#a8a5bb");
    loadRows_[i]->setMinimumHeight(40);
    loadRows_[i]->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    grid->addWidget(loadRows_[i], i % 5, i / 5);
  }
  loadLayout->addLayout(grid);
  loadStatus_ = textLabel("", 12, "#a9a5bd");
  loadStatus_->setObjectName("loading-status");
  loadLayout->addWidget(loadStatus_);
  auto *actions = new QHBoxLayout;
  auto *retry = new QPushButton("Retry connections");
  retry->setObjectName("loading-retry");
  loadContinue_ = new QPushButton("Open wallet");
  loadContinue_->setObjectName("loading-continue");
  loadContinue_->setEnabled(false);
  actions->addStretch();
  actions->addWidget(retry);
  actions->addWidget(loadContinue_);
  actions->addStretch();
  loadLayout->addLayout(actions);
  connect(retry, &QPushButton::clicked, this, [this] {
    if (stage_ != Stage::Loading)
      return;
    loadingAge_.restart();
    network_.refresh();
    market_.refresh();
    updateLoading();
  });
  connect(loadContinue_, &QPushButton::clicked, this,
          &WalletWindow::finishLoading);
  layout->addWidget(loading_);
  loading_->hide();
  auto *footer = new QHBoxLayout;
  footer->setContentsMargins(20, 18, 20, 0);
  footer->setSpacing(36);
  auto *help = new QPushButton("Help");
  auto *wallets = new QPushButton("Wallets");
  startupRestore_ = new QPushButton("Restore");
  auto *quit = new QPushButton("Quit");
  footer->addStretch();
  footer->addWidget(help);
  footer->addWidget(wallets);
  footer->addWidget(startupRestore_);
  footer->addWidget(quit);
  footer->addStretch();
  layout->addLayout(footer);
  connect(quit, &QPushButton::clicked, this, &QWidget::close);
  connect(help, &QPushButton::clicked, this, [this] {
    QMessageBox::information(
        this, "Wallet access",
        "Open your encrypted wallet with its password. A backup requires the "
        "same password. Aurum does not use a recovery phrase.");
  });
  connect(wallets, &QPushButton::clicked, this, &WalletWindow::walletManager);
  connect(startupRestore_, &QPushButton::clicked, this, [this] {
    if (stage_ != Stage::Locked || QFile::exists(vaultPath_))
      return;
    auto source = QFileDialog::getOpenFileName(this, "Restore encrypted backup",
                                               {}, "Aurum wallet (*.aurum)");
    if (source.isEmpty())
      return;
    restoreSource_ = source;
    startupTitle_->setText("Restore Your Wallet");
    startupSubtitle_->setText("Enter the backup password");
    startupConfirm_->hide();
    startupSubmit_->setText("Restore and unlock");
    startupPassword_->clear();
    startupPassword_->setFocus();
  });
  outer->addWidget(center, 0, Qt::AlignHCenter);
  outer->addStretch(1);
  auto *version = textLabel("AURUM  /  0.7.2", 10, "#656275");
  outer->addWidget(version);
  loadingTimer_.setInterval(100);
  connect(&loadingTimer_, &QTimer::timeout, this, &WalletWindow::updateLoading);
  showLocked();
}
void WalletWindow::showLocked() {
  if (demo_)
    return;
  stage_ = Stage::Locked;
  startupScreen_->setProperty("loading", false);
  loadingTimer_.stop();
  chartHover_ = -1;
  restoreSource_.clear();
  startupPassword_->clear();
  startupPassword_->setEchoMode(QLineEdit::Password);
  startupConfirm_->clear();
  unlockError_->clear();
  const bool exists = QFile::exists(vaultPath_);
  startupTitle_->setText(exists ? "Unlock to Continue" : "Create Your Wallet");
  startupSubtitle_->setText(
      exists ? "Enter your wallet password"
             : "Choose a password for your encrypted wallet");
  startupConfirm_->setVisible(!exists);
  startupRestore_->setVisible(!exists);
  startupSubmit_->setText(exists ? "Unlock" : "Create wallet");
  startupSubmit_->setEnabled(true);
  credentials_->show();
  loading_->hide();
  startupScreen_->show();
  startupScreen_->raise();
  startupPassword_->setFocus();
  rebuild();
  update();
}
void WalletWindow::submitUnlock() {
  if (stage_ != Stage::Locked)
    return;
  const bool create = !QFile::exists(vaultPath_) && restoreSource_.isEmpty();
  if (create && startupPassword_->text() != startupConfirm_->text()) {
    unlockError_->setText("Passwords do not match");
    return;
  }
  QByteArray password = startupPassword_->text().toUtf8();
  startupPassword_->clear();
  startupPassword_->setEchoMode(QLineEdit::Password);
  startupConfirm_->clear();
  if (password.isEmpty()) {
    unlockError_->setText("Enter your password");
    return;
  }
  stage_ = Stage::Opening;
  startupSubmit_->setEnabled(false);
  startupSubmit_->setText("Unlocking…");
  unlockError_->clear();
  const QString path = vaultPath_, source = restoreSource_,
                moneroDir = monero_.walletDirectory();
  QPointer<WalletWindow> guard(this);
  unlockWorker_ = QThread::create([guard, path, source, create, moneroDir,
                                   password = std::move(password)]() mutable {
    // The Monero engine uses the same startup password to open its own
    // encrypted wallet. Keep this transient copy only until the UI starts the
    // local RPC process; it is cleared on every path.
    QByteArray moneroPassword = password;
    PublicWallet result;
    QString failure;
    try {
      result = openIsolated(source.isEmpty() ? path : source, password, create);
      if (!source.isEmpty()) {
        QFile input(source);
        if (!input.open(QIODevice::ReadOnly) || input.size() != 160)
          throw std::runtime_error("Invalid backup");
        QSaveFile output(path);
        if (!output.open(QIODevice::WriteOnly) ||
            !output.setPermissions(QFileDevice::ReadOwner |
                                   QFileDevice::WriteOwner) ||
            output.write(input.readAll()) != 160 || !output.commit())
          throw std::runtime_error("Could not restore backup");
        const QDir sidecar(source + ".xmr");
        if (sidecar.exists()) {
          if (!QDir().mkpath(moneroDir))
            throw std::runtime_error("Could not restore the Monero wallet directory");
          for (const auto &name : {QString("aurum-xmr"),
                                   QString("aurum-xmr.keys")}) {
            const auto from = sidecar.filePath(name);
            if (!QFile::exists(from))
              continue;
            const auto to = QDir(moneroDir).filePath(name);
            QFile::remove(to);
            if (!QFile::copy(from, to) ||
                !QFile(to).setPermissions(QFileDevice::ReadOwner |
                                          QFileDevice::WriteOwner))
              throw std::runtime_error("Could not restore the Monero wallet");
          }
        }
      }
    } catch (const std::exception &e) {
      failure = QString::fromUtf8(e.what());
    }
    if (guard)
      QMetaObject::invokeMethod(
          guard.data(),
            [guard, result = std::move(result), failure, create, source,
             moneroPassword = std::move(moneroPassword)]() mutable {
            if (!guard || guard->stage_ != Stage::Opening) {
              moneroPassword.clear();
              return;
            }
            if (!failure.isEmpty()) {
              moneroPassword.clear();
              guard->stage_ = Stage::Locked;
              guard->startupSubmit_->setEnabled(true);
              guard->startupSubmit_->setText(create ? "Create wallet"
                                             : source.isEmpty() ? "Unlock"
                                                                : "Restore");
              guard->unlockError_->setText(failure);
              guard->startupPassword_->setFocus();
              return;
            }
            guard->publicKeys_ = result.publicKeys;
            guard->addresses_ = result.addresses;
            guard->sessionOpen_ = true;
            guard->hidden_ = false;
              guard->beginLoading(std::move(moneroPassword));
              moneroPassword.clear();
          },
          Qt::QueuedConnection);
  });
  connect(unlockWorker_, &QThread::finished, unlockWorker_,
          &QObject::deleteLater);
  unlockWorker_->start();
}
void WalletWindow::beginLoading(QByteArray moneroPassword) {
  stage_ = Stage::Loading;
  startupScreen_->setProperty("loading", true);
  credentials_->hide();
  loading_->show();
  startupRestore_->hide();
  startupTitle_->setText("Loading Your Wallet");
  startupSubtitle_->setText("Connecting to your assets");
  loadContinue_->setEnabled(false);
  loadingAge_.restart();
  network_.setAddresses(addresses_);
  monero_.start(std::move(moneroPassword), [this](QString error) {
    if (!error.isEmpty())
      notify("Monero: " + error);
  });
  market_.start();
  if (theme_ == 10)
    market_.requestHistory(Chain(selectedAsset_), chartDays_);
  loadingTimer_.start();
  updateLoading();
}
void WalletWindow::updateLoading() {
  if (stage_ != Stage::Loading)
    return;
  int ready = 0, finished = 0;
  for (int i = 0; i < AssetCount; ++i) {
    const auto &state = network_.state(Chain(i));
    const bool priced = market_.quote(Chain(i)).fresh();
    const bool loaded = state.known && priced;
    ready += loaded;
    const bool moneroAddressReady =
        i == static_cast<int>(Chain::Monero) && !state.address.isEmpty();
    const bool failed =
        !state.refreshing && !state.known && state.status != "Not connected";
    // XMR refresh can scan a large remote daemon over Tor. The address and
    // encrypted wallet are usable before that scan finishes, so do not hold
    // every other asset behind the privacy-chain sync.
    finished += loaded || failed || moneroAddressReady;
    QString suffix = loaded        ? "Ready"
                     : moneroAddressReady ? "Syncing · background"
                     : state.known ? "Getting price"
                     : failed && i == static_cast<int>(Chain::Monero)
                         ? "Unavailable · details below"
                     : failed ? "Connection unavailable"
                              : "Connecting…";
    loadRows_[i]->setText(assetName(Chain(i)) + "\n" + suffix);
    loadRows_[i]->setToolTip(state.status);
    loadRows_[i]->setStyleSheet(
        QString("color:%1;background:transparent;font-size:12px;")
            .arg(loaded   ? "#c6bffe"
                 : failed ? "#e4adba"
                          : "#a8a5bb"));
  }
  loadProgress_->setValue(ready);
  loadCaption_->setText(
      QString("%1 of %2 assets ready").arg(ready).arg(AssetCount));
  // A disconnected privacy node must not prevent the rest of the wallet from
  // opening. XMR remains clearly marked unavailable and reconnects in the
  // background once its local/Tor node is reachable.
  if ((ready == AssetCount || finished == AssetCount) &&
      loadingAge_.elapsed() >= 350) {
    finishLoading();
    return;
  }
  const bool waiting = true;
  loadContinue_->setEnabled(waiting);
  loadStatus_->setText(waiting ? "Some data is still syncing. You can open the "
                                 "wallet while it reconnects."
                               : "Fetching balances and market prices…");
}
void WalletWindow::finishLoading() {
  if (stage_ != Stage::Loading)
    return;
  stage_ = Stage::Ready;
  loadingTimer_.stop();
  startupScreen_->hide();
  rebuild();
  setFocus();
  update();
}
} // namespace aurum
