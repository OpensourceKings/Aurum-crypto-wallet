#include "window.h"
#include "evm.h"
#include "mullvad.h"
#include "signer.h"
#include "spl.h"
#include <QApplication>
#include <QBoxLayout>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGridLayout>
#include <QHelpEvent>
#include <QIcon>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainterPath>
#include <QPointer>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScrollArea>
#include <QSettings>
#include <QStandardPaths>
#include <QToolTip>
#include <algorithm>
#include <cmath>
#include <qrencode.h>

namespace aurum {
const QVector<Theme> &themes() {
  static QVector<Theme> list{
      {"01 · Obsidian", "Dark · sidebar", "#101116", "#191a22", "#20212c",
       "#2c2d38", "#f3f1fa", "#8b8c9d", "#c1b2ff", "#191326", 0},
      {"02 · Atelier", "Light · cards", "#f1f0eb", "#fcfbf7", "#e7e7df",
       "#dcdcd2", "#252a25", "#7d8279", "#52674b", "#ffffff", 1},
      {"03 · Cobalt", "Blue · top navigation", "#071429", "#10223d", "#142d4e",
       "#203c5b", "#f1f6ff", "#87a0c0", "#6facff", "#071429", 2},
      {"04 · Forma", "Dark · wide portfolio", "#151719", "#1d2023", "#282c30",
       "#34393e", "#f7f8f4", "#939c9f", "#d6ec8d", "#1a2410", 3},
      {"05 · Evergreen", "Green · split layout", "#0b1e1a", "#142b24",
       "#1a362c", "#2a453b", "#edf5e8", "#91a89a", "#c5df95", "#18301c", 4},
      {"06 · Copper", "Warm · cards", "#231a18", "#302421", "#3b2c27",
       "#503b33", "#fff3e7", "#ad9486", "#eeae80", "#342014", 1},
      {"07 · Glacier", "Light · wide portfolio", "#eaf0f5", "#ffffff",
       "#e3ebf1", "#d5dfe8", "#152f46", "#748898", "#1f739c", "#ffffff", 3},
      {"08 · Nocturne", "Purple · split layout", "#171123", "#231c32",
       "#30253f", "#413150", "#f5edff", "#a494b8", "#d4a1f4", "#291334", 4},
      {"09 · Circuit", "Dark · compact", "#10191c", "#172327", "#203037",
       "#2d4147", "#e3f4f5", "#809ba3", "#70d7d1", "#102627", 2},
      {"10 · Dune", "Light · sidebar", "#eee7dc", "#faf6ee", "#e5ddcf",
       "#d6caba", "#3c352b", "#8b7d6b", "#986a40", "#fffaf3", 0},
      {"11 · Exodus", "Classic · asset view", "#191b1d", "#1d2023", "#272b2e",
       "#303538", "#f5f6f7", "#999ea3", "#8b80f8", "#141318", 5}};
  return list;
}
namespace {
const QStringList names{"Bitcoin",         "Litecoin",        "Solana",
                        "Ethereum",        "Polygon",         "BNB",
                        "USDT · Ethereum", "USDC · Ethereum", "USDT · Solana",
                        "USDC · Solana",    "Monero"};
const QStringList tickers{"BTC", "LTC",  "SOL",  "ETH",  "POL", "BNB",
                          "USDT", "USDC", "USDT", "USDC", "XMR"};
const QColor colors[]{"#eda45c", "#86abeb", "#ac93e8", "#869fe8", "#aa8be8",
                      "#e6c65c", "#58b6a1", "#5599cf", "#58b6a1", "#5599cf",
                      "#ff6600"};
QIcon assetIcon(int id) {
  static const char *paths[]{"btc", "ltc",  "sol",  "eth",  "matic",
                             "bnb", "usdt", "usdc", "usdt", "usdc", "xmr"};
  return QIcon(QString(":/assets/coins/%1.svg").arg(paths[id]));
}
QPixmap uiIcon(const QString &name, QColor color) {
  QPixmap image(64, 64);
  image.fill(Qt::transparent);
  QPainter painter(&image);
  QIcon(":/assets/ui/" + name + ".svg").paint(&painter, image.rect());
  painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
  painter.fillRect(image.rect(), color);
  painter.end();
  return image;
}
void drawIcon(QPainter &p, QRectF bounds, const QString &name, QColor color) {
  p.drawPixmap(bounds.toRect(), uiIcon(name, color));
}
QLabel *label(const QString &s, int size = 14) {
  auto *l = new QLabel(s);
  l->setTextFormat(Qt::PlainText);
  l->setWordWrap(true);
  l->setStyleSheet(QString("font-size:%1px;").arg(size));
  return l;
}
QPushButton *action(const QString &text, QWidget *parent = nullptr) {
  auto *b = new QPushButton(text, parent);
  b->setMinimumHeight(44);
  b->setCursor(Qt::PointingHandCursor);
  return b;
}
void error(QWidget *w, const QString &s) {
  QMessageBox::warning(w, "Aurum", s);
}
QImage qrImage(const QString &text) {
  QRcode *qr = QRcode_encodeString(text.toUtf8().constData(), 0, QR_ECLEVEL_M,
                                   QR_MODE_8, 1);
  if (!qr)
    return {};
  int scale = 7, n = (qr->width + 8) * scale;
  QImage im(n, n, QImage::Format_RGB32);
  im.fill(Qt::white);
  QPainter p(&im);
  p.setPen(Qt::NoPen);
  p.setBrush(QColor("#11131c"));
  for (int y = 0; y < qr->width; ++y)
    for (int x = 0; x < qr->width; ++x)
      if (qr->data[y * qr->width + x] & 1)
        p.drawRect((x + 4) * scale, (y + 4) * scale, scale, scale);
  QRcode_free(qr);
  return im;
}
} // namespace
WalletWindow::WalletWindow(bool demo, QWidget *parent)
    : QWidget(parent),
      profiles_(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)),
      network_(this), monero_(this), market_(this), demo_(demo) {
  static const bool fontsLoaded = [] {
    for (const auto *name :
         {"Roboto-Thin", "Roboto-Light", "Roboto-Regular", "Roboto-Medium",
          "Roboto-Bold", "Roboto-Black", "RobotoCondensed-Light",
          "RobotoCondensed-Regular", "RobotoCondensed-Bold"}) {
      if (QFontDatabase::addApplicationFont(
              QString(":/assets/fonts/%1-webfont.woff").arg(name)) < 0)
        qFatal("Could not load bundled wallet font");
    }
    return true;
  }();
  Q_UNUSED(fontsLoaded);
  setWindowTitle("Aurum — Self-custody wallet");
  setMinimumSize(1100, 900);
  resize(1440, 960);
  setAttribute(Qt::WA_OpaquePaintEvent);
  setFocusPolicy(Qt::StrongFocus);
  setMouseTracking(true);
  stage_ = demo_ ? Stage::Ready : Stage::Locked;
  QApplication::setFont(QFont("Inter", 10));
  const auto profile = profiles_.active();
  vaultPath_ = profile.vaultPath;
  monero_.setWalletDirectory(profile.moneroDirectory);
  QSettings s;
  int t = s.value("appearance", 5).toInt();
  theme_ = std::clamp(t, 0, int(themes().size()) - 1);
  setFont(QFont(theme_ == 10 ? "Roboto" : "Inter", 10));
  setMinimumSize(1100, theme_ == 10 ? 740 : 900);
  EndpointSettings e;
  e.bitcoin = s.value("endpoints/bitcoin", e.bitcoin).toString();
  e.litecoin = s.value("endpoints/litecoin", e.litecoin).toString();
  if (e.litecoin == "https://litecoinspace.org/api")
    e.litecoin = EndpointSettings{}.litecoin;
  e.solana = s.value("endpoints/solana", e.solana).toString();
  e.solanaWs = s.value("endpoints/solanaWs", e.solanaWs).toString();
  e.ethereum = s.value("endpoints/ethereum", e.ethereum).toString();
  e.polygon = s.value("endpoints/polygon", e.polygon).toString();
  e.bnb = s.value("endpoints/bnb", e.bnb).toString();
  try {
    network_.setEndpoints(e);
  } catch (...) {
  }
  try {
    const auto node = s.contains("endpoints/monero")
                          ? s.value("endpoints/monero").toString()
                          : monero_.node();
    const auto proxy = s.contains("endpoints/moneroProxy")
                           ? s.value("endpoints/moneroProxy").toString()
                           : monero_.proxy();
    monero_.setNode(node, proxy);
  } catch (...) {
  }
  connect(&network_, &Network::changed, this, [this] {
    if (!demo_)
      for (int i = 0; i < AssetCount; ++i)
        balances_[i] = network_.state(static_cast<Chain>(i)).balance;
    updateLoading();
    update();
  });
  connect(&monero_, &MoneroClient::changed, this, [this] {
    network_.setExternalState(Chain::Monero, monero_.state());
    addresses_[static_cast<int>(Chain::Monero)] = monero_.state().address;
    updateLoading();
    update();
  });
  connect(&market_, &MarketData::changed, this, [this] {
    chartHover_ = -1;
    updateLoading();
    update();
  });
  if (demo_)
    balances_ = {45280000ULL, 3245600000ULL, 124580000000ULL};
  if (!demo_)
    setupStartup();
  rebuild();
}
bool WalletWindow::event(QEvent *ev) {
  if (ev->type() == QEvent::ToolTip) {
    auto *help = static_cast<QHelpEvent *>(ev);
    if (stage_ != Stage::Ready)
      return true;
    if (themes()[theme_].layout == 5 && chartPointAt(help->pos()) >= 0) {
      chartHover_ = chartPointAt(help->pos());
      QToolTip::showText(help->globalPos(), chartHoverText(), this);
      return true;
    }
    if (themes()[theme_].layout == 5 && hero_.contains(help->pos())) {
      QToolTip::showText(help->globalPos(), holdingTooltip(selectedAsset_),
                         this);
      return true;
    }
    for (int i = 0; i < AssetCount; ++i)
      if (assetBounds(i).contains(help->pos())) {
        QToolTip::showText(help->globalPos(), holdingTooltip(i), this);
        return true;
      }
    QToolTip::hideText();
    ev->ignore();
    return true;
  }
  return QWidget::event(ev);
}
void WalletWindow::setTheme(int i) {
  theme_ = std::clamp(i, 0, int(themes().size()) - 1);
  setFont(QFont(theme_ == 10 ? "Roboto" : "Inter", 10));
  if (!demo_) {
    QSettings s;
    s.setValue("appearance", theme_);
  }
  setMinimumSize(1100, theme_ == 10 ? 740 : 900);
  rebuild();
  if (!demo_ && sessionOpen_ && theme_ == 10)
    market_.requestHistory(Chain(selectedAsset_), chartDays_);
  update();
}
void WalletWindow::resizeEvent(QResizeEvent *) {
  if (startupScreen_)
    startupScreen_->setGeometry(rect());
  rebuild();
}
void WalletWindow::button(QRectF r, const QString &text,
                          std::function<void()> fn) {
  auto *b = new QPushButton(this);
  b->setGeometry(r.toRect());
  b->setText("");
  b->setAccessibleName(text);
  b->setToolTip(text);
  b->setCursor(Qt::PointingHandCursor);
  b->setStyleSheet("QPushButton{background:transparent;border:1px solid "
                   "transparent;border-radius:9px;} "
                   "QPushButton:hover{background:rgba(145,145,170,18);} "
                   "QPushButton:focus{border:2px solid #8d91ba;}");
  connect(b, &QPushButton::clicked, this, std::move(fn));
  b->show();
  buttons_ << b;
}
void WalletWindow::rebuild() {
  for (auto *b : buttons_)
    b->deleteLater();
  buttons_.clear();
  chartHover_ = -1;
  if (!demo_ && stage_ != Stage::Ready) {
    hero_ = assets_ = main_ = activity_ = insight_ = {};
    if (startupScreen_) {
      startupScreen_->setGeometry(rect());
      startupScreen_->show();
      startupScreen_->raise();
    }
    return;
  }
  const auto &t = themes()[theme_];
  qreal w = width(), h = height();
  if (t.layout == 5) {
    rebuildExodus();
    return;
  }
  bool top = t.layout == 2 || t.layout == 3 || t.layout == 5;
  int nav = top ? 0 : (t.layout == 1 ? 94 : 224);
  main_ =
      QRectF(nav + 40, top ? 142 : 104, w - nav - 80, h - (top ? 176 : 140));
  qreal x = main_.x(), y = main_.y(), cw = main_.width();
  if (t.layout == 4) {
    hero_ = {x, y, cw * .58 - 12, main_.height() * .61};
    assets_ = {x + cw * .58 + 12, y, cw * .42 - 12, main_.height() * .61};
    activity_ = {x, y + main_.height() * .61 + 24, cw * .58 - 12,
                 main_.height() * .39 - 24};
    insight_ = {assets_.x(), activity_.y(), assets_.width(),
                activity_.height()};
  } else if (t.layout == 1) {
    hero_ = {x, y, cw * .63 - 12, 360};
    insight_ = {x + cw * .63 + 12, y, cw * .37 - 12, 360};
    assets_ = {x, y + 384, cw, main_.height() - 384};
    activity_ = {};
  } else if (t.layout == 3 || t.layout == 5) {
    hero_ = {x, y, cw, 285};
    assets_ = {x, y + 309, cw * .64 - 12, main_.height() - 309};
    activity_ = {x + cw * .64 + 12, y + 309, cw * .36 - 12,
                 main_.height() - 309};
    insight_ = {};
  } else {
    hero_ = {x, y, cw * .66 - 12, 340};
    insight_ = {x + cw * .66 + 12, y, cw * .34 - 12, 340};
    assets_ = {x, y + 364, cw * .66 - 12, main_.height() - 364};
    activity_ = {insight_.x(), y + 364, insight_.width(), main_.height() - 364};
  }
  if (top) {
    button({w - 280, 23, 115, 42}, "Choose appearance",
           [this] { themePicker(); });
    button({w - 153, 23, 112, 42}, "Wallet access", [this] { unlockWallet(); });
    button({42, 85, 110, 34}, "Overview", [this] { update(); });
    button({164, 85, 110, 34}, "Activity", [this] { activity(); });
    button({286, 85, 110, 34}, "Connection settings", [this] { settings(); });
    button({405, 85, 130, 34}, "Encrypted backup", [this] { backupWallet(); });
    button({548, 85, 105, 34}, "Wallets", [this] { walletManager(); });
  } else {
    button({qreal(nav + 40), 28, 120, 38}, "Overview", [this] { update(); });
    button({w - 288, 26, 123, 42}, "Choose appearance",
           [this] { themePicker(); });
    button({w - 151, 26, 110, 42}, "Wallet access", [this] { unlockWallet(); });
    int yy = 154;
    for (int i = 0; i < 4; ++i) {
      button({14., qreal(yy + i * 58), qreal(nav - 28), 44},
             QStringList{"Overview", "Activity", "Appearance", "Settings"}[i],
             [this, i] {
               if (i == 1)
                 activity();
               else if (i == 2)
                 themePicker();
               else if (i == 3)
                 settings();
             });
    }
    button({14., h - 125, qreal(nav - 28), 44}, "Back up encrypted wallet",
           [this] { backupWallet(); });
    button({14., h - 180, qreal(nav - 28), 44}, "Wallets",
           [this] { walletManager(); });
    button({14., h - 70, qreal(nav - 28), 44}, "Lock wallet",
           [this] { lockWallet(); });
  }
  button({hero_.x() + 28, hero_.bottom() - 71, 120, 43}, "Send cryptocurrency",
         [this] { send(); });
  button({hero_.x() + 160, hero_.bottom() - 71, 130, 43},
         "Receive cryptocurrency", [this] { receive(); });
  button({hero_.right() - 63, hero_.y() + 24, 36, 32}, "Hide balances", [this] {
    hidden_ = !hidden_;
    update();
  });
  if (!assets_.isEmpty())
    button({assets_.x() + 175, assets_.y() + 19, 94, 36}, "All assets",
           [this] { assetList(); });
  if (!assets_.isEmpty())
    button({assets_.right() - 106, assets_.y() + 19, 82, 36},
           "Refresh balances", [this] {
             network_.refresh();
             market_.refresh();
             notify(demo_ ? "Design preview · sample balances"
                          : "Refresh requested");
           });
  if (!activity_.isEmpty())
    button({activity_.right() - 96, activity_.y() + 19, 74, 36}, "All activity",
           [this] { activity(); });
  update();
}
void WalletWindow::drawText(QPainter &p, QRectF r, const QString &s, int size,
                            QColor color, int weight, Qt::Alignment align) {
  QFont f(theme_ == 10 ? "Roboto" : "Inter");
  f.setPixelSize(size);
  f.setWeight(QFont::Weight(weight));
  p.setFont(f);
  p.setPen(color);
  p.drawText(r, align, s);
}
void WalletWindow::panel(QPainter &p, QRectF r, QColor color, int radius) {
  p.setPen(QPen(themes()[theme_].line, 1));
  p.setBrush(color);
  p.drawRoundedRect(r, radius, radius);
}
void WalletWindow::coin(QPainter &p, QPointF pos, int id, int size) {
  assetIcon(id).paint(&p, QRect(pos.toPoint(), QSize(size, size)));
}
QString WalletWindow::amount(int c) const {
  if (hidden_)
    return "••••••";
  if (!demo_ && (!sessionOpen_ || !network_.state(static_cast<Chain>(c)).known))
    return "—";
  if (isEvm(static_cast<Chain>(c)) || isSpl(static_cast<Chain>(c))) {
    if (demo_) {
      static const QString demoUnits[] = {"0",
                                          "0",
                                          "0",
                                          "7400000000000000000",
                                          "2500000000000000000000",
                                          "1200000000000000000",
                                          "3500000000",
                                          "1250000000",
                                          "940000000",
                                          "2150000000",
                                          "250000000000"};
      return formatUnits(demoUnits[c], decimals(static_cast<Chain>(c)));
    }
    return formatUnits(network_.state(static_cast<Chain>(c)).units,
                       decimals(static_cast<Chain>(c)));
  }
  return formatAmount(balances_[c], decimals(static_cast<Chain>(c)));
}
void WalletWindow::drawNavigation(QPainter &p) {
  const auto &t = themes()[theme_];
  qreal w = width(), h = height();
  bool top = t.layout == 2 || t.layout == 3 || t.layout == 5;
  int nav = top ? 0 : t.layout == 1 ? 94 : 224;
  if (!top) {
    p.fillRect(QRect(0, 0, nav, height()), t.panel);
    p.setPen(t.line);
    p.drawLine(nav, 0, nav, h);
  }
  qreal logoX = top ? 42 : 26;
  QPainterPath diamond;
  diamond.moveTo(logoX + 15, 28);
  diamond.lineTo(logoX + 30, 46);
  diamond.lineTo(logoX + 15, 64);
  diamond.lineTo(logoX, 46);
  diamond.closeSubpath();
  p.fillPath(diamond, t.accent);
  QPainterPath cut;
  cut.moveTo(logoX + 15, 39);
  cut.lineTo(logoX + 21, 47);
  cut.lineTo(logoX + 15, 54);
  cut.lineTo(logoX + 9, 47);
  cut.closeSubpath();
  p.fillPath(cut, t.panel);
  if (top || nav > 100)
    drawText(p, {logoX + 43, 24, 140, 45}, "aurum", 29, t.text, 600);
  if (top) {
    p.setPen(t.line);
    p.drawLine(40, 74, w - 40, 74);
    QStringList tabs{"Overview", "Activity", "Connections", "Backup"};
    int x = 42;
    for (int i = 0; i < 4; ++i) {
      drawText(p, {qreal(x), 85, 110, 34}, tabs[i], 14,
               i == 0 ? t.text : t.muted, i == 0 ? 600 : 400);
      x += 122;
    }
    p.setPen(QPen(t.accent, 2));
    p.drawLine(42, 124, 117, 124);
  } else {
    if (nav > 100)
      drawText(p, {30, 105, 170, 25}, "MAINNET", 10, t.muted, 600);
    QStringList txt{"Overview", "Activity", "Appearance", "Settings"},
        icons{"layout-dashboard", "activity", "palette", "settings"};
    for (int i = 0; i < 4; ++i) {
      QRectF r(14, 154 + i * 58, nav - 28, 44);
      if (i == 0)
        panel(p, r, t.panel2, 10);
      drawIcon(p, {31., r.y() + 11, 22, 22}, icons[i],
               i == 0 ? t.accent : t.muted);
      if (nav > 100)
        drawText(p, {70., r.y(), 130, 44}, txt[i], 14,
                 i == 0 ? t.text : t.muted, i == 0 ? 600 : 400);
    }
    if (nav > 100) {
      drawText(p, {28, h - 187, 164, 24}, "SELF-CUSTODY", 10, t.muted, 600);
      drawText(p, {28, h - 160, 190, 20}, "7 networks · 11 assets", 12,
               t.muted);
      drawIcon(p, {32, h - 113, 20, 20}, "archive", t.muted);
      drawIcon(p, {32, h - 58, 20, 20}, "lock", t.muted);
      drawText(p, {30, h - 125, 170, 44}, "        Encrypted backup", 13,
               t.muted);
      drawText(p, {30, h - 70, 170, 44}, "        Lock wallet", 13, t.muted);
    } else {
      drawIcon(p, {37, h - 113, 20, 20}, "archive", t.muted);
      drawIcon(p, {37, h - 58, 20, 20}, "lock", t.muted);
    }
  }
  if (!top)
    drawText(p, {qreal(nav + 40), 28, 150, 38}, "Overview", 16, t.text, 500);
  panel(p, {w - 288, 26, 123, 42}, t.bg, 11);
  drawText(p, {w - 288, 26, 123, 42}, "     Appearance", 12, t.muted, 500,
           Qt::AlignCenter);
  drawIcon(p, {w - 275, 39, 16, 16}, "palette", t.muted);
  panel(p, {w - 151, 26, 110, 42}, t.panel2, 11);
  drawText(p, {w - 151, 26, 110, 42},
           demo_          ? "Preview"
           : sessionOpen_ ? "Unlocked"
                          : "Open wallet",
           12, t.text, 500, Qt::AlignCenter);
  drawText(p, {main_.x(), h - 28, main_.width(), 20},
           demo_ ? t.name + "   /   DESIGN PREVIEW · SAMPLE DATA"
                 : "AURUM  /  " +
                       (sessionOpen_
                            ? QString("Keys sealed · ") +
                                  (!notice_.isEmpty() ? notice_ : "Mainnet")
                            : "Wallet locked · Encrypted local storage"),
           10, t.muted, 500);
}
void WalletWindow::chart(QPainter &p, QRectF r, int style) {
  const auto &t = themes()[theme_];
  if (!demo_) {
    p.setPen(QPen(t.line, 1, Qt::DashLine));
    p.drawLine(r.left(), r.center().y(), r.right(), r.center().y());
    drawText(p, r, "Price history unavailable", 12, t.muted, 400,
             Qt::AlignCenter);
    return;
  }
  QVector<double> points{.75, .72, .76, .65, .68, .66, .60, .64, .55,
                         .58, .51, .57, .50, .47, .53, .40, .43, .38,
                         .46, .31, .34, .22, .29, .16, .19, .1};
  if (style == 1) {
    int n = 24;
    double bw = r.width() / n;
    for (int i = 0; i < n; ++i) {
      QColor c = t.accent;
      c.setAlpha(i > 18 ? 240 : 90);
      p.setPen(Qt::NoPen);
      p.setBrush(c);
      double height = (1 - points[i]) * r.height();
      p.drawRoundedRect(
          QRectF(r.x() + i * bw, r.bottom() - height, bw * .55, height), 3, 3);
    }
    return;
  }
  QPainterPath path;
  for (int i = 0; i < points.size(); ++i) {
    QPointF pt(r.x() + i * r.width() / (points.size() - 1),
               r.y() + points[i] * r.height());
    if (i == 0)
      path.moveTo(pt);
    else
      path.lineTo(pt);
  }
  QPainterPath fill = path;
  fill.lineTo(r.bottomRight());
  fill.lineTo(r.bottomLeft());
  fill.closeSubpath();
  QLinearGradient gradient(r.topLeft(), r.bottomLeft());
  QColor c = t.accent;
  c.setAlpha(46);
  gradient.setColorAt(0, c);
  c.setAlpha(0);
  gradient.setColorAt(1, c);
  p.fillPath(fill, gradient);
  p.setPen(QPen(t.accent, 2.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  p.drawPath(path);
  p.setBrush(t.accent);
  p.setPen(QPen(t.panel, 3));
  p.drawEllipse(path.currentPosition(), 4, 4);
}
void WalletWindow::drawHero(QPainter &p) {
  const auto &t = themes()[theme_];
  panel(p, hero_, t.panel, 19);
  if (t.layout == 5) {
    p.save();
    QPainterPath clip;
    clip.addRoundedRect(hero_.adjusted(1, 1, -1, -1), 19, 19);
    p.setClipPath(clip);
    QLinearGradient glow(hero_.topRight(), hero_.bottomLeft());
    glow.setColorAt(0, QColor("#34356a"));
    glow.setColorAt(.55, QColor("#242d49"));
    glow.setColorAt(1, t.panel);
    p.fillRect(hero_, glow);
    p.restore();
  }
  qreal x = hero_.x() + 28, y = hero_.y() + 25, w = hero_.width() - 56;
  drawText(p, {x, y, w, 24},
           t.layout == 1 ? "TOTAL HOLDINGS" : "Portfolio balance", 14, t.muted,
           500);
  drawIcon(p, {hero_.right() - 55, y + 5, 19, 19}, hidden_ ? "eye-off" : "eye",
           t.muted);
  const bool horizontal = t.layout == 3 || t.layout == 5;
  QString val = demo_ || sessionOpen_ ? totalUsd() : "Wallet";
  drawText(p, {x, y + 37, horizontal ? w * .5 : w, 65}, val,
           demo_          ? 49
           : sessionOpen_ ? 44
                          : 32,
           t.text, 500);
  drawText(p, {x, y + 108, w, 22},
           demo_          ? "↗  $2,164.70  (3.85%)  this month"
           : sessionOpen_ ? quoteCaption()
                          : "Create or restore a wallet",
           13, demo_ ? t.accent : t.muted, 500);
  QRectF cr = horizontal ? QRectF(hero_.x() + hero_.width() * .5,
                                  hero_.y() + 48, hero_.width() * .46, 137)
                         : QRectF(x, y + 155, w,
                                  std::max<qreal>(50, hero_.height() - 285));
  chart(p, cr, theme_ == 1 || theme_ == 8 ? 1 : 0);
  if (hero_.height() > 380)
    drawText(p, {x, hero_.bottom() - 123, w, 30},
             demo_ ? "01 OCT                                             10 OCT"
                   : quoteCaption(),
             11, t.muted);
  panel(p, {x, hero_.bottom() - 71, 120, 43}, t.accent, 10);
  drawText(p, {x, hero_.bottom() - 71, 120, 43}, "    Send", 14, t.accentText,
           600, Qt::AlignCenter);
  panel(p, {x + 132, hero_.bottom() - 71, 130, 43}, t.panel2, 10);
  drawText(p, {x + 132, hero_.bottom() - 71, 130, 43}, "    Receive", 14,
           t.text, 500, Qt::AlignCenter);
  drawIcon(p, {x + 23, hero_.bottom() - 58, 17, 17}, "arrow-up-right",
           t.accentText);
  drawIcon(p, {x + 151, hero_.bottom() - 58, 17, 17}, "arrow-down-left",
           t.text);
  if (hero_.width() > 780)
    drawText(p, {hero_.right() - 310, hero_.bottom() - 69, 280, 40},
             demo_ ? "Sample data · No transactions" : "Mainnet", 12, t.muted,
             400, Qt::AlignRight | Qt::AlignVCenter);
}
void WalletWindow::drawAssets(QPainter &p) {
  const auto &t = themes()[theme_];
  if (assets_.isEmpty())
    return;
  panel(p, assets_, t.panel);
  qreal x = assets_.x() + 26, y = assets_.y() + 22, w = assets_.width() - 52;
  drawText(p, {x, y, w, 30}, "Assets", 19, t.text, 500);
  drawText(p, {x + 151, y, 110, 30}, "All assets ↗", 11, t.muted, 500);
  drawText(p, {assets_.right() - 103, y, 77, 30}, "↻  Refresh", 12, t.muted,
           400, Qt::AlignRight | Qt::AlignVCenter);
  bool big = assets_.width() > 900;
  bool narrow = assets_.width() < 500;
  const qreal rowsTop = y + 76;
  const qreal rowH = (assets_.bottom() - 18 - rowsTop) / 6;
  if (big) {
    qreal gap = 18, cw = (w - 2 * gap) / 3;
    for (int i = 0; i < 3; ++i) {
      QRectF r(x + i * (cw + gap), y + 62, cw, assets_.height() - 115);
      panel(p, r, t.panel2, 14);
      coin(p, r.topLeft() + QPointF(22, 22), i, 40);
      drawText(p, {r.x() + 74, r.y() + 20, r.width() - 90, 26}, names[i], 17,
               t.text, 500);
      drawText(p, {r.x() + 74, r.y() + 47, r.width() - 90, 20}, tickers[i], 11,
               t.muted);
      drawText(p, {r.x() + 22, r.y() + 95, r.width() - 44, 45}, holdingUsd(i),
               29, t.text, 500);
      drawText(p, {r.x() + 22, r.y() + 138, r.width() - 44, 24},
               priceText(i) + " / " + tickers[i], 12, t.muted);
      QRectF cr(r.x() + 22, r.bottom() - 64, r.width() - 44, 44);
      chart(p, cr, 0);
    }
    return;
  }
  drawText(p, {x, y + 48, w, 22}, "ASSET", 10, t.muted, 600);
  drawText(p, {x, y + 48, w, 22}, "BALANCE", 10, t.muted, 600,
           Qt::AlignRight | Qt::AlignVCenter);
  for (int i = 0; i < 6; ++i) {
    const qreal ry = rowsTop + i * rowH;
    const qreal contentY = ry + (rowH - 38) / 2;
    const qreal iconSize = std::min<qreal>(36, rowH - 8);
    if (i) {
      p.setPen(t.line);
      p.drawLine(x, ry, x + w, ry);
    }
    coin(p, {x, ry + (rowH - iconSize) / 2}, i, iconSize);
    drawText(p, {x + 50, contentY, narrow ? 120. : 150., 21}, names[i], 15,
             t.text, 500);
    drawText(p, {x + 50, contentY + 22, 120, 16}, tickers[i], 10, t.muted);
    drawText(p, {x + w * .43, contentY, w * .57, 21}, holdingUsd(i),
             narrow ? 15 : 17, t.text, 500, Qt::AlignRight | Qt::AlignVCenter);
    QString status = priceText(i) + " / " + tickers[i];
    QFont statusFont("Inter");
    statusFont.setPixelSize(11);
    status = QFontMetrics(statusFont)
                 .elidedText(status, Qt::ElideRight, int(w * .56));
    drawText(p, {x + w * .44, contentY + 22, w * .56, 16},
             hidden_ ? "••••" : status, 11, t.muted, 400,
             Qt::AlignRight | Qt::AlignVCenter);
  }
}
void WalletWindow::drawActivity(QPainter &p) {
  const auto &t = themes()[theme_];
  if (activity_.isEmpty())
    return;
  panel(p, activity_, t.panel);
  qreal x = activity_.x() + 24, y = activity_.y() + 24,
        w = activity_.width() - 48;
  drawText(p, {x, y, w - 50, 28}, "Recent activity", 18, t.text, 500);
  drawText(p, {activity_.right() - 94, y, 70, 28}, "View all ↗", 11, t.muted,
           400, Qt::AlignRight | Qt::AlignVCenter);
  if (!demo_) {
    drawText(p, {x, y + 85, w, 75},
             sessionOpen_ ? "View transaction history" : "No activity", 17,
             t.muted, 400, Qt::AlignCenter);
    drawText(p, {x, y + 178, w, 24},
             sessionOpen_ ? "View all to fetch recent transactions"
                          : "Open your wallet to get started",
             11, t.muted, 400, Qt::AlignCenter);
    return;
  }
  for (int i = 0; i < 3; ++i) {
    qreal yy = y + 66 + i * 83;
    if (yy + 60 > activity_.bottom())
      break;
    coin(p, {x, yy + 5}, i, 32);
    drawText(p, {x + 46, yy, w - 80, 26},
             i == 1 ? "Sent Litecoin" : "Received " + names[i], 13, t.text,
             500);
    drawText(
        p, {x + 46, yy + 27, w - 65, 22},
        QStringList{"Today, 10:42", "Yesterday, 16:08", "08 Oct, 09:21"}[i], 11,
        t.muted);
    drawText(p, {x, yy + 50, w, 20},
             QStringList{"+0.025 BTC", "−2.500 LTC", "+12.000 SOL"}[i], 12,
             i == 1 ? t.muted : t.accent, 500,
             Qt::AlignRight | Qt::AlignVCenter);
  }
}
void WalletWindow::drawInsight(QPainter &p) {
  const auto &t = themes()[theme_];
  if (insight_.isEmpty())
    return;
  panel(p, insight_, t.panel);
  qreal x = insight_.x() + 26, y = insight_.y() + 24, w = insight_.width() - 52;
  if (t.layout == 4) {
    drawText(p, {x, y, w, 30}, "Networks", 18, t.text, 500);
    drawText(p, {x, y + 52, w, 64},
             "Bitcoin · Litecoin · Solana\nEthereum · Polygon · BNB", 16,
             t.muted);
    p.setPen(t.line);
    p.drawLine(x, y + 139, x + w, y + 139);
    drawText(p, {x, y + 154, w, 26}, "BTC   /   LTC   /   SOL", 12, t.accent,
             600);
    return;
  }
  drawText(p, {x, y, w, 28}, t.layout == 1 ? "Network overview" : "Allocation",
           18, t.text, 500);
  if (t.layout == 1) {
    QStringList subtitles{"Bitcoin network", "Litecoin network",
                          "Solana network"};
    for (int i = 0; i < 3; ++i) {
      qreal yy = y + 68 + i * 72;
      coin(p, {x, yy + 3}, i, 34);
      drawText(p, {x + 49, yy, w - 52, 25}, subtitles[i], 14, t.text, 500);
      drawText(p, {x + 49, yy + 26, w - 52, 20},
               demo_ ? "●  Connected · sample status"
                     : network_.connectionStatus(static_cast<Chain>(i)),
               11, demo_ ? t.accent : t.muted);
    }
    return;
  }
  qreal size = std::min<qreal>(145, w * .67), cx = insight_.center().x(),
        cy = y + 115;
  QRectF ring(cx - size / 2, cy - size / 2, size, size);
  p.setBrush(Qt::NoBrush);
  int start = 90 * 16;
  double shares[] = {.486, .051, .463};
  for (int i = 0; i < 3; ++i) {
    p.setPen(QPen(demo_ ? colors[i] : t.line, 18, Qt::SolidLine, Qt::FlatCap));
    int span = int(shares[i] * 360 * 16) - 100;
    p.drawArc(ring, start, -span);
    start -= int(shares[i] * 360 * 16);
  }
  drawText(p, {cx - size / 2, cy - 24, size, 30}, demo_ ? "3" : "—", 27, t.text,
           500, Qt::AlignCenter);
  drawText(p, {cx - size / 2, cy + 7, size, 22}, "assets", 11, t.muted, 400,
           Qt::AlignCenter);
  for (int i = 0; i < 3; ++i) {
    qreal yy = y + 222 + i * 25;
    if (yy + 22 > insight_.bottom() - 12)
      break;
    p.setPen(Qt::NoPen);
    p.setBrush(colors[i]);
    p.drawEllipse(QRectF(x, yy + 8, 6, 6));
    drawText(p, {x + 16, yy, w - 20, 22}, names[i], 12, t.muted);
    drawText(p, {x, yy, w, 22},
             demo_ ? QStringList{"48.6%", "5.1%", "46.3%"}[i] : "—", 12, t.text,
             500, Qt::AlignRight | Qt::AlignVCenter);
  }
}
void WalletWindow::paintEvent(QPaintEvent *) {
  QPainter p(this);
  if (!demo_ && stage_ != Stage::Ready) {
    p.fillRect(rect(), QColor("#1a1c24"));
    return;
  }
  if (themes()[theme_].layout == 5) {
    p.setRenderHint(QPainter::Antialiasing);
    drawExodus(p);
    return;
  }
  p.setRenderHint(QPainter::Antialiasing);
  p.fillRect(rect(), themes()[theme_].bg);
  drawNavigation(p);
  drawHero(p);
  drawAssets(p);
  drawActivity(p);
  drawInsight(p);
}
QString WalletWindow::uiStyle() const {
  const auto &t = themes()[theme_];
  return QString("QDialog,QMessageBox{background:%1;color:%2;} "
                 "QLabel,QCheckBox{color:%2;} "
                 "QLineEdit,QComboBox{background:%3;color:%2;border:1px solid "
                 "%4;border-radius:8px;padding:12px;selection-background-color:"
                 "%5;} QPushButton{background:%3;color:%2;border:1px solid "
                 "%4;border-radius:8px;padding:10px 18px;} "
                 "QPushButton:hover{border-color:%5;} "
                 "QPushButton:disabled{color:%6;} "
                 "QScrollArea{border:none;background:transparent;}")
      .arg(t.panel.name(), t.text.name(), t.panel2.name(), t.line.name(),
           t.accent.name(), t.muted.name());
}
void WalletWindow::notify(const QString &s) {
  notice_ = s;
  update();
}
void WalletWindow::lockWallet() {
  sessionOpen_ = false;
  addresses_ = {};
  publicKeys_ = {};
  market_.stop();
  network_.pause();
  monero_.stop();
  balances_ = {};
  hidden_ = true;
  showLocked();
}
void WalletWindow::switchWallet(const QString &id) {
  WalletProfile profile;
  try {
    profile = profiles_.get(id);
  } catch (const std::exception &e) {
    notify(QString::fromUtf8(e.what()));
    return;
  }
  if (profile.id == profiles_.activeId()) {
    if (sessionOpen_)
      lockWallet();
    else
      showLocked();
    return;
  }
  sessionOpen_ = false;
  addresses_ = {};
  publicKeys_ = {};
  balances_ = {};
  market_.stop();
  network_.pause();
  monero_.stop();
  try {
    profiles_.setActive(profile.id);
    vaultPath_ = profile.vaultPath;
    monero_.setWalletDirectory(profile.moneroDirectory);
  } catch (const std::exception &e) {
    notify(QString::fromUtf8(e.what()));
    return;
  }
  hidden_ = true;
  showLocked();
}
void WalletWindow::walletManager() {
  if (demo_) {
    notify("Wallet switching is available with a real wallet");
    return;
  }
  QDialog dialog(this);
  dialog.setWindowTitle("Wallets");
  dialog.setMinimumWidth(500);
  dialog.setStyleSheet(uiStyle());
  QVBoxLayout layout(&dialog);
  layout.setContentsMargins(26, 24, 26, 24);
  layout.setSpacing(14);
  layout.addWidget(label("Wallets", 25));
  layout.addWidget(label("Each wallet has its own encrypted vault and addresses."));
  QComboBox choice;
  for (const auto &profile : profiles_.all())
    choice.addItem(profile.name + (profile.id == profiles_.activeId()
                                       ? " · Active" : ""), profile.id);
  layout.addWidget(&choice);
  auto *add = action("Add wallet");
  auto *open = action("Open selected");
  auto *close = action("Close");
  auto *actions = new QHBoxLayout;
  actions->addWidget(add);
  actions->addStretch();
  actions->addWidget(close);
  actions->addWidget(open);
  layout.addLayout(actions);
  connect(close, &QPushButton::clicked, &dialog, &QDialog::reject);
  connect(add, &QPushButton::clicked, &dialog, [this, &dialog, &choice] {
    bool ok = false;
    const auto name = QInputDialog::getText(&dialog, "Add wallet", "Wallet name",
                                             QLineEdit::Normal,
                                             "Wallet " + QString::number(profiles_.all().size() + 1),
                                             &ok);
    if (!ok || name.trimmed().isEmpty())
      return;
    try {
      const auto profile = profiles_.add(name);
      choice.addItem(profile.name + " · Active", profile.id);
      choice.setCurrentIndex(choice.count() - 1);
      switchWallet(profile.id);
      dialog.accept();
    } catch (const std::exception &e) {
      QMessageBox::warning(&dialog, "Wallet", QString::fromUtf8(e.what()));
    }
  });
  connect(open, &QPushButton::clicked, &dialog, [this, &dialog, &choice] {
    switchWallet(choice.currentData().toString());
    dialog.accept();
  });
  dialog.exec();
}
void WalletWindow::unlockWallet() {
  if (demo_)
    return;
  if (sessionOpen_) {
    lockWallet();
    return;
  }
  showLocked();
}
void WalletWindow::backupWallet() {
  if (demo_) {
    notify("Backups are available with a real wallet");
    return;
  }
  if (!QFile::exists(vaultPath_)) {
    unlockWallet();
    return;
  }
  QString file = QFileDialog::getSaveFileName(
      this, "Save encrypted wallet backup", "aurum-backup.aurum",
      "Aurum wallet (*.aurum)");
  if (file.isEmpty())
    return;
  if (QFileInfo(file).absoluteFilePath() ==
      QFileInfo(vaultPath_).absoluteFilePath()) {
    error(this, "Choose a different path from the active wallet");
    return;
  }
  QFile source(vaultPath_);
  if (!source.open(QIODevice::ReadOnly)) {
    error(this, "Cannot read wallet");
    return;
  }
  QSaveFile dest(file);
  if (!dest.open(QIODevice::WriteOnly)) {
    error(this, "Cannot write backup");
    return;
  }
  if (!dest.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
    error(this, "Cannot restrict backup permissions");
    return;
  }
  auto b = source.readAll();
  if (dest.write(b) != b.size() || !dest.commit()) {
    error(this, "Backup write failed");
    return;
  }
  const QString sidecar = file + ".xmr";
  QDir(sidecar).removeRecursively();
  const QDir moneroDir(monero_.walletDirectory());
  const QStringList moneroFiles{"aurum-xmr", "aurum-xmr.keys"};
  bool haveMonero = false;
  if (moneroDir.exists() && QDir().mkpath(sidecar)) {
    for (const auto &name : moneroFiles) {
      const auto from = moneroDir.filePath(name);
      if (!QFile::exists(from))
        continue;
      haveMonero = true;
      const auto to = QDir(sidecar).filePath(name);
      if (!QFile::copy(from, to)) {
        QDir(sidecar).removeRecursively();
        error(this, "Monero backup write failed");
        return;
      }
      QFile(to).setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    }
    if (!haveMonero)
      QDir(sidecar).removeRecursively();
  }
  notify("Encrypted backup saved");
  QMessageBox::information(
      this, "Backup saved",
      "Store this encrypted file somewhere safe. Its password is required for "
      "recovery. If this wallet has XMR, keep the matching .xmr sidecar "
      "directory beside it. Aurum does not create a recovery phrase.");
}
void WalletWindow::receive(int selected) {
  if (!demo_ && !sessionOpen_) {
    unlockWallet();
    if (!sessionOpen_)
      return;
  }
  QDialog d(this);
  d.setWindowTitle("Receive crypto");
  d.setMinimumWidth(540);
  d.setStyleSheet(uiStyle());
  QVBoxLayout v(&d);
  v.setContentsMargins(30, 26, 30, 26);
  v.setSpacing(16);
  v.addWidget(label("Receive", 25));
  QComboBox chain;
  chain.addItems(names);
  for (int i = 0; i < AssetCount; ++i)
    chain.setItemIcon(i, assetIcon(i));
  chain.setCurrentIndex(selected);
  v.addWidget(&chain);
  QLabel qr;
  qr.setAlignment(Qt::AlignCenter);
  v.addWidget(&qr);
  QLineEdit addr;
  addr.setReadOnly(true);
  addr.setMinimumWidth(480);
  v.addWidget(&addr);
  QLabel network;
  network.setWordWrap(true);
  v.addWidget(&network);
  auto *copy = action("Copy address");
  v.addWidget(copy);
  auto display = [&] {
    int i = chain.currentIndex();
    unsigned char demoKey[32] = {};
    demoKey[31] = 1;
    QString a = demo_
                    ? (i == static_cast<int>(Chain::Monero)
                           ? QString("4AurumDemoMoneroAddress")
                           : address(demoKey, static_cast<Chain>(i)))
                    : addresses_[i];
    addr.setText(a);
    qr.setPixmap(
        QPixmap::fromImage(qrImage(a))
            .scaled(265, 265, Qt::KeepAspectRatio, Qt::FastTransformation));
    network.setText(
        demo_
            ? "Design preview only. Do not send funds to this example address."
            : QString("Send only %1 on %2 mainnet. Other networks are not "
                      "interchangeable.")
                  .arg(tickers[i], networkName(static_cast<Chain>(i))));
  };
  connect(&chain, qOverload<int>(&QComboBox::currentIndexChanged), &d, display);
  connect(copy, &QPushButton::clicked, &d, [&] {
    QApplication::clipboard()->setText(addr.text());
    copy->setText("Address copied");
  });
  display();
  d.exec();
}
void WalletWindow::themePicker() {
  QDialog d(this);
  d.setWindowTitle("Appearance");
  d.setMinimumWidth(870);
  d.setStyleSheet(uiStyle());
  QVBoxLayout v(&d);
  v.setContentsMargins(28, 28, 28, 28);
  v.setSpacing(18);
  v.addWidget(label("Appearance", 25));
  v.addWidget(label("Choose a color and layout."));
  QGridLayout grid;
  grid.setSpacing(12);
  for (int i = 0; i < themes().size(); ++i) {
    const auto &t = themes()[i];
    auto *b =
        action(t.name.mid(5) + (i == theme_ ? "  ✓" : "") + "\n" + t.caption);
    b->setObjectName(QString("theme-%1").arg(i));
    b->setAccessibleName(t.name.mid(5));
    b->setCheckable(true);
    b->setChecked(i == theme_);
    b->setMinimumHeight(86);
    b->setStyleSheet(
        QString(
            "QPushButton{background:%1;color:%2;text-align:left;padding:14px;"
            "border:2px solid %3;border-radius:12px;}"
            "QPushButton:hover,QPushButton:focus{border-color:%4;}")
            .arg(t.bg.name(), t.text.name(),
                 i == theme_ ? t.accent.name() : t.line.name(),
                 t.accent.name()));
    grid.addWidget(b, i / 3, i % 3);
    connect(b, &QPushButton::clicked, &d, [&, i] {
      setTheme(i);
      d.accept();
    });
  }
  v.addLayout(&grid);
  d.exec();
}
void WalletWindow::settings() {
  QDialog d(this);
  d.setWindowTitle("Connections");
  d.setMinimumWidth(650);
  d.setStyleSheet(uiStyle());
  QVBoxLayout v(&d);
  v.setContentsMargins(30, 28, 30, 28);
  v.setSpacing(15);
  v.addWidget(label("Connections", 25));
  v.addWidget(label(
      "Public endpoints share capacity and may rate limit requests. Solana "
      "uses a live account subscription; Bitcoin and Litecoin refresh every 30 "
      "seconds. Custom endpoints must serve the same mainnet and API."));
  auto e = network_.endpoints();
  QFormLayout form;
  QLineEdit btc(e.bitcoin), ltc(e.litecoin), sol(e.solana), ws(e.solanaWs),
      eth(e.ethereum), pol(e.polygon), bnb(e.bnb),
      xmr(monero_.node()), xmrProxy(monero_.proxy());
  form.addRow("Bitcoin · Esplora API", &btc);
  form.addRow("Litecoin · Electrum / Esplora", &ltc);
  form.addRow("Solana · JSON-RPC", &sol);
  form.addRow("Solana · WebSocket", &ws);
  form.addRow("Ethereum · RPC", &eth);
  form.addRow("Polygon · RPC", &pol);
  form.addRow("BNB · RPC", &bnb);
  form.addRow("Monero · wallet node", &xmr);
  form.addRow("Monero · Tor SOCKS proxy", &xmrProxy);
  v.addLayout(&form);
  v.addWidget(label("Live asset status", 18));
  auto *statusGrid = new QGridLayout;
  std::array<QLabel *, AssetCount> statusLabels{};
  for (int i = 0; i < AssetCount; ++i) {
    statusGrid->addWidget(label(names[i], 12), i / 3, (i % 3) * 2);
    statusLabels[i] = label({}, 12);
    statusLabels[i]->setWordWrap(true);
    statusGrid->addWidget(statusLabels[i], i / 3, (i % 3) * 2 + 1);
  }
  v.addLayout(statusGrid);
  auto updateStatus = [this, statusLabels] {
    for (int i = 0; i < AssetCount; ++i) {
      const auto c = static_cast<Chain>(i);
      statusLabels[i]->setText(network_.connectionStatus(c));
      statusLabels[i]->setToolTip(network_.state(c).address);
    }
  };
  connect(&network_, &Network::changed, &d, updateStatus);
  connect(&monero_, &MoneroClient::changed, &d, updateStatus);
  updateStatus();
  v.addWidget(label("Mullvad transport", 18));
  bool mullvadKnown = false;
  const bool mullvadIsConnected = MullvadClient::connected(&mullvadKnown);
  auto *mullvadStatus = label(
      !MullvadClient::available()
          ? "Official Mullvad client not detected"
          : !mullvadKnown
                ? "Mullvad status unavailable"
                : mullvadIsConnected ? "Mullvad connected"
                                     : "Mullvad disconnected",
      13);
  auto *mullvadAccount = new QLineEdit;
  mullvadAccount->setPlaceholderText("16-digit account number");
  mullvadAccount->setEchoMode(QLineEdit::Password);
  auto *mullvadConnect = action("Connect Mullvad");
  auto *mullvadDisconnect = action("Disconnect");
  auto *mullvadRow = new QHBoxLayout;
  mullvadRow->addWidget(mullvadAccount, 1);
  mullvadRow->addWidget(mullvadConnect);
  mullvadRow->addWidget(mullvadDisconnect);
  v.addLayout(mullvadRow);
  v.addWidget(mullvadStatus);
  connect(mullvadConnect, &QPushButton::clicked, &d, [&, mullvadAccount,
                                                       mullvadStatus] {
    QString errorMessage;
    const auto account = mullvadAccount->text();
    mullvadAccount->clear();
    if (MullvadClient::loginAndConnect(account, &errorMessage)) {
      mullvadStatus->setText("Mullvad connected · Tor remains the XMR hop");
      notify("Mullvad connected; Tor will carry Monero traffic");
    } else {
      mullvadStatus->setText("Mullvad connection failed");
      error(&d, errorMessage);
    }
  });
  connect(mullvadDisconnect, &QPushButton::clicked, &d,
          [&, mullvadStatus] {
            QString errorMessage;
            if (MullvadClient::disconnect(&errorMessage)) {
              mullvadStatus->setText("Mullvad disconnected");
              notify("Mullvad disconnected");
            } else {
              error(&d, errorMessage);
            }
          });
  v.addWidget(label(
      "Bitcoin uses Esplora HTTPS. Litecoin accepts Electrum ssl://host:port "
      "or Esplora HTTPS. TLS certificate checks and mainnet checks are "
      "required. "
      "Unencrypted connections are allowed only for localhost. Core RPC uses "
      "a different API. Monero defaults to a public onion node through the "
      "local Tor SOCKS proxy; use the loopback endpoint for the bundled local "
      "daemon or another .onion node. If Tor is not "
      "running, XMR stays unavailable instead of falling back to clearnet. "
      "Public remote nodes can see wallet queries and broadcasts. To use "
      "Mullvad → Tor → node, connect Mullvad here or in its official client, "
      "then keep Tor listening on the SOCKS proxy above. Aurum does not store "
      "your Mullvad account number."));
  auto *save = action("Save connections");
  v.addWidget(save);
  connect(save, &QPushButton::clicked, &d, [&] {
    EndpointSettings next{btc.text().trimmed(), ltc.text().trimmed(),
                          sol.text().trimmed(), ws.text().trimmed(),
                          eth.text().trimmed(), pol.text().trimmed(),
                          bnb.text().trimmed()};
    try {
      network_.setEndpoints(next);
      monero_.setNode(xmr.text().trimmed(), xmrProxy.text().trimmed());
      QSettings s;
      s.setValue("endpoints/bitcoin", next.bitcoin);
      s.setValue("endpoints/litecoin", next.litecoin);
      s.setValue("endpoints/solana", next.solana);
      s.setValue("endpoints/solanaWs", next.solanaWs);
      s.setValue("endpoints/ethereum", next.ethereum);
      s.setValue("endpoints/polygon", next.polygon);
      s.setValue("endpoints/bnb", next.bnb);
      s.setValue("endpoints/monero", xmr.text().trimmed());
      s.setValue("endpoints/moneroProxy", xmrProxy.text().trimmed());
      d.accept();
      notify("Connections saved");
    } catch (const std::exception &ex) {
      error(&d, ex.what());
    }
  });
  d.exec();
}
void WalletWindow::activity() {
  if (demo_) {
    notify("Design preview · activity is sample data");
    return;
  }
  if (!sessionOpen_) {
    unlockWallet();
    if (!sessionOpen_)
      return;
  }
  QDialog d(this);
  d.setWindowTitle("On-chain activity");
  d.resize(750, 650);
  d.setStyleSheet(uiStyle());
  QVBoxLayout v(&d);
  v.setContentsMargins(28, 26, 28, 26);
  v.addWidget(label("Activity", 25));
  QComboBox chain;
  chain.addItems(names);
  for (int i = 0; i < AssetCount; ++i)
    chain.setItemIcon(i, assetIcon(i));
  v.addWidget(&chain);
  auto *explore = action("View address in explorer");
  v.addWidget(explore);
  connect(explore, &QPushButton::clicked, &d, [this, &chain] {
    auto c = static_cast<Chain>(chain.currentIndex());
    QString url = explorer(c);
    url.replace("/tx/", "/address/");
    QDesktopServices::openUrl(QUrl(url + addresses_[int(c)]));
  });
  QLabel status;
  status.setWordWrap(true);
  v.addWidget(&status);
  QScrollArea scroll;
  scroll.setWidgetResizable(true);
  QWidget list;
  QVBoxLayout rows(&list);
  rows.setAlignment(Qt::AlignTop);
  scroll.setWidget(&list);
  v.addWidget(&scroll);
  QPointer<QDialog> guard(&d);
  auto serial = std::make_shared<int>(0);
  auto fetch = [&, guard, serial] {
    int request = ++*serial;
    status.setText("Fetching recent transactions…");
    while (auto *item = rows.takeAt(0)) {
      delete item->widget();
      delete item;
    }
    auto c = static_cast<Chain>(chain.currentIndex());
    network_.history(c, [&, guard, c, serial, request](QJsonValue val,
                                                       QString err) {
      if (!guard || request != *serial)
        return;
      if (!err.isEmpty()) {
        status.setText(err);
        return;
      }
      auto items = val.toArray();
      status.setText(items.isEmpty()
                         ? "No transactions found."
                         : QString("%1 recent transactions · %2 mainnet")
                               .arg(items.size())
                               .arg(symbol(c)));
      for (const auto &value : items) {
        auto o = value.toObject();
        QString id = baseChain(c) == Chain::Solana
                         ? o.value("signature").toString()
                         : o.value("txid").toString();
        bool confirmed =
            baseChain(c) == Chain::Solana
                ? o.value("confirmationStatus").toString() == "finalized"
                : o.value("status").toObject().value("confirmed").toBool();
        bool failed = baseChain(c) == Chain::Solana && !o.value("err").isNull();
        auto *b = action((failed      ? "Failed"
                          : confirmed ? "Confirmed"
                                      : "Pending") +
                         QString("  ·  ") + id.left(22) + "…" + id.right(10) +
                         "   ↗");
        b->setToolTip(id);
        rows.addWidget(b);
        connect(b, &QPushButton::clicked, guard, [id, c] {
          if (!QRegularExpression(baseChain(c) == Chain::Solana
                                      ? "^[1-9A-HJ-NP-Za-km-z]{60,100}$"
                                      : "^[0-9a-fA-F]{64}$")
                   .match(id)
                   .hasMatch())
            return;
          QString base = c == Chain::Bitcoin ? "https://blockstream.info/tx/"
                         : c == Chain::Litecoin
                             ? "https://litecoinspace.org/tx/"
                             : "https://explorer.solana.com/tx/";
          QDesktopServices::openUrl(QUrl(base + id));
        });
      }
    });
  };
  connect(&chain, qOverload<int>(&QComboBox::currentIndexChanged), &d, fetch);
  fetch();
  d.exec();
  scroll.takeWidget();
}
void WalletWindow::send(int selected) {
  if (demo_) {
    notify("Design preview · transactions are disabled");
    return;
  }
  if (!sessionOpen_) {
    unlockWallet();
    if (!sessionOpen_)
      return;
  }
  if (selected == static_cast<int>(Chain::Monero)) {
    sendMonero();
    return;
  }
  if (selected >= 3) {
    sendAccountAsset(static_cast<Chain>(selected));
    return;
  }
  QDialog dialog(this);
  dialog.setWindowTitle("Send crypto");
  dialog.setMinimumWidth(600);
  dialog.setStyleSheet(uiStyle());
  QVBoxLayout layout(&dialog);
  layout.setContentsMargins(30, 28, 30, 28);
  layout.setSpacing(14);
  layout.addWidget(label("Send", 26));
  layout.addWidget(label("Password required to confirm."));
  QComboBox chain;
  chain.addItems(names);
  for (int i = 0; i < AssetCount; ++i)
    chain.setItemIcon(i, assetIcon(i));
  chain.setCurrentIndex(selected);
  layout.addWidget(&chain);
  QLabel available;
  layout.addWidget(&available);
  QLineEdit destination, amountField, feeField("2");
  destination.setPlaceholderText("Recipient's mainnet address");
  destination.setAccessibleName("Recipient address");
  amountField.setPlaceholderText("Amount in coin units");
  amountField.setAccessibleName("Amount");
  feeField.setAccessibleName("Fee rate in base units per virtual byte");
  layout.addWidget(&destination);
  layout.addWidget(&amountField);
  QLabel feeLabel("Fee rate · sat/vB for BTC, litoshi/vB for LTC");
  layout.addWidget(&feeLabel);
  layout.addWidget(&feeField);
  QLabel status;
  status.setWordWrap(true);
  status.setTextFormat(Qt::PlainText);
  layout.addWidget(&status);
  auto *prepare = action("Review transfer");
  layout.addWidget(prepare);
  auto display = [&] {
    auto c = static_cast<Chain>(chain.currentIndex());
    const auto &state = network_.state(c);
    available.setText(
        state.known
            ? "Confirmed balance: " + formatUnits(state.units, decimals(c)) +
                  " " + symbol(c)
            : "Waiting for a confirmed balance. Refresh your wallet first.");
    feeField.setVisible(c == Chain::Bitcoin || c == Chain::Litecoin);
    feeLabel.setVisible(c == Chain::Bitcoin || c == Chain::Litecoin);
  };
  connect(&chain, qOverload<int>(&QComboBox::currentIndexChanged), &dialog,
          [&, this](int index) {
            if (index == static_cast<int>(Chain::Monero)) {
              dialog.accept();
              QTimer::singleShot(0, this, &WalletWindow::sendMonero);
            } else if (index >= 3) {
              dialog.accept();
              QTimer::singleShot(0, this, [this, index] {
                sendAccountAsset(static_cast<Chain>(index));
              });
            } else
              display();
          });
  display();
  QPointer<QDialog> guard(&dialog);
  using Signer = std::function<SignedTransaction(QByteArray &)>;
  auto reviewTransfer = [this, guard, &dialog, &status,
                         prepare](Chain c, const QString &to, quint64 amount,
                                  SignedTransaction preview, QElapsedTimer age,
                                  Signer signer) {
    if (!guard)
      return;
    if (!sessionOpen_) {
      status.setText("Account view locked. Unlock again.");
      prepare->setEnabled(true);
      return;
    }
    QDialog review(&dialog);
    review.setWindowTitle("Confirm transfer");
    review.setMinimumWidth(650);
    review.setStyleSheet(uiStyle());
    QVBoxLayout rows(&review);
    rows.setContentsMargins(30, 28, 30, 28);
    rows.setSpacing(15);
    rows.addWidget(label("Review transfer", 26));
    rows.addWidget(
        label(formatAmount(amount, decimals(c)) + " " + symbol(c), 32));
    rows.addWidget(
        label("Network: " + names[static_cast<int>(c)] + " mainnet"));
    auto *recipient = label("To: " + to);
    recipient->setTextFormat(Qt::PlainText);
    recipient->setTextInteractionFlags(Qt::TextSelectableByMouse);
    rows.addWidget(recipient);
    rows.addWidget(
        label("Network fee: " + formatAmount(preview.fee, decimals(c)) + " " +
              symbol(c)));
    rows.addWidget(label(
        "Total debit: " + formatAmount(amount + preview.fee, decimals(c)) +
        " " + symbol(c)));
    if (c != Chain::Solana)
      rows.addWidget(label("Change: " + formatAmount(preview.change, 8) + " " +
                           symbol(c)));
    QLineEdit password;
    password.setEchoMode(QLineEdit::Password);
    password.setInputMethodHints(Qt::ImhHiddenText | Qt::ImhSensitiveData |
                                 Qt::ImhNoPredictiveText);
    password.setPlaceholderText(
        "Wallet password · used only for this signature");
    rows.addWidget(&password);
    QCheckBox accepted("I verified the recipient, amount and network.");
    rows.addWidget(&accepted);
    auto *confirm = action("Sign and broadcast");
    confirm->setEnabled(false);
    rows.addWidget(confirm);
    connect(&accepted, &QCheckBox::toggled, confirm, &QPushButton::setEnabled);
    bool approved = false;
    SignedTransaction signedTx;
    connect(confirm, &QPushButton::clicked, &review, [&] {
      if (!accepted.isChecked() || !sessionOpen_)
        return;
      if (c == Chain::Solana && age.elapsed() > 60000) {
        error(&review, "Blockhash nearing expiry. Prepare a fresh transfer.");
        review.reject();
        return;
      }
      try {
        QByteArray pass = password.text().toUtf8();
        password.clear();
        signedTx = signer(pass);
        if (signedTx.fee != preview.fee || signedTx.change != preview.change)
          throw std::runtime_error("Transaction differs from reviewed details");
        approved = true;
        review.accept();
      } catch (const std::exception &ex) {
        password.clear();
        error(&review, ex.what());
      }
    });
    review.exec();
    if (!guard)
      return;
    if (!approved) {
      prepare->setEnabled(true);
      status.setText("Transfer was not broadcast.");
      return;
    }
    auto submit = [this, guard, &dialog, &status, prepare, signedTx, c] {
      if (!guard)
        return;
      status.setText("Broadcasting…");
      network_.broadcast(
          c, signedTx,
          [this, guard, &dialog, &status, prepare, signedTx, c](QJsonValue,
                                                                QString err) {
            if (!guard)
              return;
            prepare->setEnabled(false);
            QString base = c == Chain::Bitcoin ? "https://blockstream.info/tx/"
                           : c == Chain::Litecoin
                               ? "https://litecoinspace.org/tx/"
                               : "https://explorer.solana.com/tx/";
            QString result =
                err.isEmpty()
                    ? "Node accepted the transfer. Waiting for confirmation."
                    : "Broadcast result is uncertain: " + err +
                          ". Check this transaction before sending again.";
            status.setText(result + "\n\nTransaction: " + signedTx.id);
            auto *open = action("Track transaction in explorer", &dialog);
            dialog.layout()->addWidget(open);
            connect(open, &QPushButton::clicked, guard, [base, signedTx] {
              QDesktopServices::openUrl(QUrl(base + signedTx.id));
            });
            network_.refresh();
            notify(err.isEmpty() ? "Transaction submitted"
                                 : "Check transaction status");
          });
    };
    if (c == Chain::Solana) {
      status.setText("Simulating the signed transfer…");
      network_.simulate(
          signedTx, [guard, &status, prepare, submit](QJsonValue, QString err) {
            if (!guard)
              return;
            if (!err.isEmpty()) {
              status.setText(err);
              prepare->setEnabled(true);
              return;
            }
            submit();
          });
    } else
      submit();
  };
  connect(
      prepare, &QPushButton::clicked, &dialog,
      [this, guard, &dialog, &chain, &destination, &amountField, &feeField,
       &status, prepare, reviewTransfer] {
        if (!sessionOpen_) {
          status.setText("Account view locked. Unlock again.");
          return;
        }
        if (chain.currentIndex() >= 3 ||
            isSpl(static_cast<Chain>(chain.currentIndex()))) {
          auto selected = static_cast<Chain>(chain.currentIndex());
          dialog.accept();
          QTimer::singleShot(0, this,
                             [this, selected] { sendAccountAsset(selected); });
          return;
        }
        auto c = static_cast<Chain>(chain.currentIndex());
        QString to = destination.text().trimmed();
        quint64 value = 0, rate = 0;
        try {
          value = parseAmount(amountField.text().trimmed(), decimals(c));
          if (!value)
            throw std::runtime_error("Amount must be positive");
          if (c == Chain::Solana) {
            if (unbase58(to).size() != 32)
              throw std::runtime_error("Invalid Solana recipient");
          } else {
            addressScript(to, c);
            rate = parseAmount(feeField.text().trimmed(), 0);
            if (rate < 1 || rate > 10000)
              throw std::runtime_error(
                  "Fee rate must be 1–10000 base units/vB");
          }
          if (!network_.state(c).known)
            throw std::runtime_error("Wait for your balance to sync");
        } catch (const std::exception &ex) {
          status.setText(ex.what());
          return;
        }
        prepare->setEnabled(false);
        status.setText(c == Chain::Solana
                           ? "Preparing a recent blockhash and fee…"
                           : "Verifying confirmed inputs and ownership…");
        if (c != Chain::Solana) {
          network_.verifiedUtxos(
              c, [this, guard, c, to, value, rate, &status, prepare,
                  reviewTransfer](QVector<Utxo> inputs, QString err) {
                if (!guard)
                  return;
                try {
                  if (!err.isEmpty())
                    throw std::runtime_error(err.toStdString());
                  auto plan = planUtxo(publicKeys_[static_cast<int>(c)], c,
                                       inputs, to, value, rate);
                  SignedTransaction preview;
                  preview.fee = plan.fee;
                  preview.change = plan.change;
                  QElapsedTimer age;
                  age.start();
                  reviewTransfer(
                      c, to, value, preview, age,
                      [this, c, inputs, to, value, rate](QByteArray &pass) {
                        return signUtxoIsolated(vaultPath_, pass,
                                                publicKeys_[int(c)], c, inputs,
                                                to, value, rate);
                      });
                } catch (const std::exception &ex) {
                  status.setText(ex.what());
                  prepare->setEnabled(true);
                }
              });
          return;
        }
        network_.latestBlockhash([this, guard, c, to, value, &status, prepare,
                                  reviewTransfer](QJsonValue root,
                                                  QString err) {
          if (!guard)
            return;
          try {
            if (!err.isEmpty())
              throw std::runtime_error(err.toStdString());
            auto hash = unbase58(root.toObject()
                                     .value("value")
                                     .toObject()
                                     .value("blockhash")
                                     .toString());
            auto message =
                solanaMessage(publicKeys_[2], unbase58(to), hash, value);
            QElapsedTimer age;
            age.start();
            network_.feeForMessage(message, [this, guard, c, to, value, message,
                                             age, &status, prepare,
                                             reviewTransfer](QJsonValue result,
                                                             QString err) {
              if (!guard)
                return;
              try {
                if (!err.isEmpty())
                  throw std::runtime_error(err.toStdString());
                auto fee = result.toObject().value("value").toInteger(-1);
                if (fee < 0 || fee > 10000000)
                  throw std::runtime_error("Invalid or excessive Solana fee");
                auto balance = network_.state(c).balance;
                if (value > balance || quint64(fee) > balance - value)
                  throw std::runtime_error("Insufficient SOL including fee");
                SignedTransaction preview;
                preview.fee = fee;
                reviewTransfer(c, to, value, preview, age,
                               [this, message, fee](QByteArray &pass) {
                                 auto tx = signSolanaIsolated(
                                     vaultPath_, pass, publicKeys_[2],
                                     Chain::Solana, message);
                                 tx.fee = fee;
                                 return tx;
                               });
              } catch (const std::exception &ex) {
                status.setText(ex.what());
                prepare->setEnabled(true);
              }
            });
          } catch (const std::exception &ex) {
            status.setText(ex.what());
            prepare->setEnabled(true);
          }
        });
      });
  dialog.exec();
}
void WalletWindow::captureAll(const QString &directory) {
  QDir().mkpath(directory);
  int previous = theme_;
  resize(1440, 960);
  QVector<QImage> images;
  for (int i = 0; i < themes().size(); ++i) {
    setTheme(i);
    setFocus(Qt::OtherFocusReason);
    qApp->processEvents();
    QImage im = grab().toImage();
    QString name = themes()[i].name.mid(5).toLower();
    QString file = QString("%1/%2-%3.png")
                       .arg(directory)
                       .arg(i + 1, 2, 10, QChar('0'))
                       .arg(name);
    if (!im.save(file))
      throw std::runtime_error("Cannot save screenshot");
    images << im;
  }
  QImage sheet(1440, 140 + ((themes().size() + 1) / 2) * 482,
               QImage::Format_RGB32);
  sheet.fill(QColor("#101116"));
  QPainter p(&sheet);
  p.setRenderHint(QPainter::SmoothPixmapTransform);
  drawText(p, {40, 18, 1360, 55},
           QString("AURUM   /   %1 DESIGNS").arg(themes().size()), 29,
           Qt::white, 500);
  drawText(p, {40, 72, 1360, 26},
           "Native C++ / Qt 6     ·     Actual application captures     ·     "
           "Sample portfolio data",
           15, QColor("#a6a6b5"));
  for (int i = 0; i < themes().size(); ++i) {
    int x = (i % 2) * 720, y = 122 + (i / 2) * 482;
    p.drawImage(QRect(x + 12, y + 32, 696, 440), images[i]);
    drawText(p, {qreal(x + 20), qreal(y), 680, 26}, themes()[i].name, 17,
             Qt::white, 500);
  }
  p.end();
  sheet.save(directory + "/00-contact-sheet.png");
  for (int index : {8, 10}) {
    resize(1100, 900);
    setTheme(index);
    qApp->processEvents();
    grab().save(directory + (index == 8 ? "/09-circuit-compact.png"
                                        : "/11-exodus-compact.png"));
  }
  QIcon(":/assets/aurum.svg")
      .pixmap(256, 256)
      .save(directory + "/aurum-icon.png");
  resize(1440, 960);
  setTheme(previous);
}
void WalletWindow::assetList() {
  QDialog d(this);
  d.setWindowTitle("All assets");
  d.resize(740, 820);
  d.setStyleSheet(uiStyle());
  QVBoxLayout v(&d);
  v.addWidget(label("Assets", 26));
  v.addWidget(label("Select an asset."));
  QLineEdit search;
  search.setPlaceholderText("Search assets");
  search.setAccessibleName("Search assets");
  v.addWidget(&search);
  QScrollArea scroll;
  scroll.setWidgetResizable(true);
  auto *list = new QWidget;
  auto *rows = new QVBoxLayout(list);
  for (int i = 0; i < AssetCount; ++i) {
    auto *rowWidget = new QWidget;
    auto *r = new QHBoxLayout(rowWidget);
    auto *iconLabel = new QLabel;
    iconLabel->setPixmap(assetIcon(i).pixmap(32, 32));
    r->addWidget(iconLabel);
    r->addWidget(label(names[i], 16), 2);
    auto *value = label(holdingUsd(i));
    value->setToolTip(holdingTooltip(i));
    value->setObjectName(QString("holding-%1").arg(i));
    r->addWidget(value, 2);
    auto *price = label(priceText(i) + " / " + tickers[i]);
    r->addWidget(price, 2);
    auto refreshRow = [this, i, value, price] {
      value->setText(holdingUsd(i));
      value->setToolTip(holdingTooltip(i));
      price->setText(priceText(i) + " / " + tickers[i]);
    };
    connect(&market_, &MarketData::changed, &d, refreshRow);
    connect(&network_, &Network::changed, &d, refreshRow);
    auto *sendButton = action("Send");
    auto *receiveButton = action("Receive");
    r->addWidget(sendButton);
    r->addWidget(receiveButton);
    rows->addWidget(rowWidget);
    connect(&search, &QLineEdit::textChanged, rowWidget,
            [rowWidget, i](const QString &query) {
              rowWidget->setVisible(
                  (names[i] + " " + tickers[i])
                      .contains(query.trimmed(), Qt::CaseInsensitive));
            });
    connect(sendButton, &QPushButton::clicked, &d, [this, &d, i] {
      d.accept();
      QTimer::singleShot(0, this, [this, i] { send(i); });
    });
    connect(receiveButton, &QPushButton::clicked, &d, [this, &d, i] {
      d.accept();
      QTimer::singleShot(0, this, [this, i] { receive(i); });
    });
  }
  scroll.setWidget(list);
  v.addWidget(&scroll);
  d.exec();
}
void WalletWindow::sendMonero() {
  QDialog dialog(this);
  dialog.setWindowTitle("Send XMR");
  dialog.setMinimumWidth(610);
  dialog.setStyleSheet(uiStyle());
  QVBoxLayout layout(&dialog);
  layout.setContentsMargins(30, 28, 30, 28);
  layout.setSpacing(14);
  layout.addWidget(label("Send Monero", 26));
  layout.addWidget(label(
      "Monero builds the private transaction in the official wallet engine. "
      "The password is required again before signing."));
  QLineEdit destination, amountField, password;
  destination.setPlaceholderText("Recipient's Monero address");
  destination.setAccessibleName("Monero recipient address");
  amountField.setPlaceholderText("Amount in XMR");
  amountField.setAccessibleName("XMR amount");
  password.setPlaceholderText("Wallet password · sign this transfer only");
  password.setEchoMode(QLineEdit::Password);
  password.setInputMethodHints(Qt::ImhHiddenText | Qt::ImhSensitiveData |
                               Qt::ImhNoPredictiveText);
  layout.addWidget(&destination);
  layout.addWidget(&amountField);
  const auto &state = network_.state(Chain::Monero);
  layout.addWidget(label(state.known
                             ? "Confirmed balance: " +
                                   formatUnits(state.units, decimals(Chain::Monero)) +
                                   " XMR"
                             : "Monero balance is not synchronized yet."));
  QCheckBox reviewCheck(
      "I verified the Monero address and amount on a trusted screen.");
  layout.addWidget(&reviewCheck);
  layout.addWidget(&password);
  QLabel status;
  status.setWordWrap(true);
  layout.addWidget(&status);
  auto *send = action("Send XMR");
  send->setEnabled(false);
  layout.addWidget(send);
  connect(&reviewCheck, &QCheckBox::toggled, send, &QPushButton::setEnabled);
  QPointer<QDialog> guard(&dialog);
  connect(send, &QPushButton::clicked, &dialog, [this, guard, &destination,
                                                 &amountField, &password,
                                                 &status, send, &reviewCheck] {
    if (!guard || !reviewCheck.isChecked())
      return;
    const auto to = destination.text().trimmed();
    const auto amount = amountField.text().trimmed();
    QByteArray pass = password.text().toUtf8();
    password.clear();
    if (to.isEmpty() || amount.isEmpty() || pass.isEmpty()) {
      pass.fill('\0');
      status.setText("Recipient, amount and wallet password are required.");
      return;
    }
    send->setEnabled(false);
    status.setText("Opening the encrypted Monero wallet and preparing a "
                   "private transaction…");
    monero_.transfer(to, amount, std::move(pass),
                     [guard, &status, send](QString txid, QString error) {
                       if (!guard)
                         return;
                       if (!error.isEmpty()) {
                         status.setText(error);
                         send->setEnabled(true);
                         return;
                       }
                       status.setText("Transaction submitted. Verify it on a "
                                      "trusted Monero explorer before sending "
                                      "again.\n\n" +
                                      txid);
                     });
  });
  dialog.exec();
}

void WalletWindow::sendAccountAsset(Chain c) {
  if (demo_ || !sessionOpen_)
    return;
  QDialog d(this);
  d.setWindowTitle("Send " + assetName(c));
  d.setMinimumWidth(650);
  d.setStyleSheet(uiStyle());
  QVBoxLayout v(&d);
  v.setContentsMargins(30, 28, 30, 28);
  v.setSpacing(15);
  v.addWidget(label("Send " + assetName(c), 26));
  v.addWidget(label("Network: " + networkName(c) + " mainnet  ·  Balance: " +
                    amount(int(c)) + " " + symbol(c)));
  QLineEdit recipient, amountField;
  recipient.setPlaceholderText("Recipient wallet address");
  amountField.setPlaceholderText("Amount in " + symbol(c));
  v.addWidget(&recipient);
  v.addWidget(&amountField);
  QLabel status;
  status.setWordWrap(true);
  status.setTextFormat(Qt::PlainText);
  v.addWidget(&status);
  auto *prepare = action("Estimate fee and review");
  v.addWidget(prepare);
  QPointer<QDialog> guard(&d);
  connect(
      prepare, &QPushButton::clicked, &d,
      [this, guard, &d, &recipient, &amountField, &status, prepare, c] {
        QString to = recipient.text().trimmed(), units;
        try {
          if (!sessionOpen_)
            throw std::runtime_error("Account view is locked");
          if (!network_.state(c).known)
            throw std::runtime_error("Wait for the asset balance to sync");
          units = parseUnits(amountField.text().trimmed(), decimals(c));
          if (units == "0")
            throw std::runtime_error("Amount must be positive");
        } catch (const std::exception &e) {
          status.setText(e.what());
          return;
        }
        prepare->setEnabled(false);
        status.setText("Checking the network, balance, nonce and fees…");
        network_.prepareAccountTransfer(
            c, to, units,
            [this, guard, &d, &status, prepare, c, to,
             units](AccountPlan plan, QString errorText) {
              if (!guard)
                return;
              if (!errorText.isEmpty()) {
                status.setText(errorText);
                prepare->setEnabled(true);
                return;
              }
              QElapsedTimer age;
              age.start();
              QDialog review(&d);
              review.setWindowTitle("Review " + assetName(c));
              review.setMinimumWidth(650);
              review.setStyleSheet(uiStyle());
              QVBoxLayout rv(&review);
              rv.setContentsMargins(30, 28, 30, 28);
              rv.setSpacing(14);
              rv.addWidget(
                  label(formatUnits(units, decimals(c)) + " " + symbol(c), 30));
              rv.addWidget(label(networkName(c) + " mainnet"));
              auto *addr = label("To: " + to);
              addr->setTextFormat(Qt::PlainText);
              addr->setTextInteractionFlags(Qt::TextSelectableByMouse);
              rv.addWidget(addr);
              auto base = baseChain(c);
              rv.addWidget(
                  label("Maximum network fee: " +
                        formatUnits(QString::number(plan.fee), decimals(base)) +
                        " " + symbol(base)));
              if (plan.rent)
                rv.addWidget(label("New recipient token account: " +
                                   formatUnits(QString::number(plan.rent), 9) +
                                   " SOL rent deposit"));
              if (isToken(c))
                rv.addWidget(label("Token: " + tokenAddress(c), 12));
              else
                rv.addWidget(
                    label("Maximum total: " +
                          formatUnits(add256(units, QString::number(plan.fee)),
                                      decimals(c)) +
                          " " + symbol(c)));
              QLineEdit password;
              password.setEchoMode(QLineEdit::Password);
              password.setInputMethodHints(Qt::ImhHiddenText |
                                           Qt::ImhSensitiveData |
                                           Qt::ImhNoPredictiveText);
              password.setPlaceholderText(
                  "Wallet password · sign this transfer only");
              rv.addWidget(&password);
              QCheckBox check(
                  "I verified the address, network, amount and maximum fee.");
              rv.addWidget(&check);
              auto *confirm = action("Sign and broadcast");
              confirm->setEnabled(false);
              rv.addWidget(confirm);
              connect(&check, &QCheckBox::toggled, confirm,
                      &QPushButton::setEnabled);
              SignedTransaction tx;
              bool approved = false;
              connect(confirm, &QPushButton::clicked, &review, [&] {
                try {
                  if (!check.isChecked() || !sessionOpen_)
                    throw std::runtime_error(
                        "Unlock the account view and review all details");
                  if (age.elapsed() > (isSpl(c) ? 60000 : 120000))
                    throw std::runtime_error(
                        "Fee/nonce quote expired. Prepare a fresh transfer");
                  QByteArray pass = password.text().toUtf8();
                  password.clear();
                  tx = isEvm(c)
                           ? signEvmIsolated(vaultPath_, pass,
                                             publicKeys_[int(c)], c, plan.evm)
                           : signSolanaIsolated(vaultPath_, pass,
                                                publicKeys_[int(c)], c,
                                                plan.message);
                  if (isSpl(c))
                    tx.fee = plan.fee;
                  else if (tx.fee != plan.fee)
                    throw std::runtime_error(
                        "Fee differs from approved estimate");
                  approved = true;
                  review.accept();
                } catch (const std::exception &e) {
                  password.clear();
                  error(&review, e.what());
                }
              });
              review.exec();
              if (!guard)
                return;
              if (!approved) {
                prepare->setEnabled(true);
                status.setText("Transfer was not broadcast.");
                return;
              }
              auto submit = [this, guard, &d, &status, prepare, c, tx] {
                if (!guard)
                  return;
                status.setText("Broadcasting…");
                network_.broadcast(
                    c, tx,
                    [this, guard, &d, &status, prepare, c, tx](QJsonValue,
                                                               QString err) {
                      if (!guard)
                        return;
                      prepare->setEnabled(false);
                      status.setText(
                          (err.isEmpty()
                               ? "Node accepted the transfer. Check on-chain "
                                 "confirmation."
                               : "Broadcast result uncertain: " + err +
                                     ". Check this hash before sending "
                                     "again.") +
                          QString("\n\n") + tx.id);
                      auto *b = action("View transaction", &d);
                      d.layout()->addWidget(b);
                      connect(b, &QPushButton::clicked, guard, [c, tx] {
                        QDesktopServices::openUrl(QUrl(explorer(c) + tx.id));
                      });
                      network_.refresh();
                    });
              };
              if (isSpl(c)) {
                status.setText(
                    "Simulating token transfer and account creation…");
                network_.simulate(tx, [guard, &status, prepare,
                                       submit](QJsonValue, QString err) {
                  if (!guard)
                    return;
                  if (!err.isEmpty()) {
                    status.setText(err);
                    prepare->setEnabled(true);
                    return;
                  }
                  submit();
                });
              } else
                submit();
            });
      });
  d.exec();
}
} // namespace aurum
