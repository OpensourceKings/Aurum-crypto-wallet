#include "window.h"
#include <QFontMetricsF>
#include <QIcon>
#include <QMouseEvent>
#include <QPainterPath>
#include <algorithm>
#include <cmath>
namespace aurum {
namespace {
const QColor accents[]{"#ffb412", "#b9bdc4", "#8a7af8", "#8499fb", "#a36bfb",
                       "#f3bd30", "#4abb97", "#4d9bfa", "#4abb97", "#4d9bfa",
                       "#ff6600"};
const char *tabNames[]{"Bitcoin",    "Litecoin",  "Solana",     "Ethereum",
                       "Polygon",    "BNB",       "USDT · ETH", "USDC · ETH",
                       "USDT · SOL", "USDC · SOL", "Monero"};
const double changes[]{.10,  .39,   .42,  .59,   1.72,
                       1.22, -.012, .002, -.012, .002, .16};
} // namespace
QRectF WalletWindow::exodusRect(QRectF r) const {
  return {r.x() * width() / 1200, r.y() * height() / 760,
          r.width() * width() / 1200, r.height() * height() / 760};
}
void WalletWindow::selectAsset(int i) {
  selectedAsset_ = std::clamp(i, 0, AssetCount - 1);
  chartHover_ = -1;
  setAccessibleDescription(assetName(Chain(selectedAsset_)) + ": " +
                           amount(selectedAsset_) + " " +
                           symbol(Chain(selectedAsset_)) + "; " +
                           holdingUsd(selectedAsset_) + " USD");
  if (!demo_ && sessionOpen_)
    market_.requestHistory(Chain(selectedAsset_), chartDays_);
  update();
}
void WalletWindow::rebuildExodus() {
  main_ = exodusRect({0, 120, 1200, 640});
  hero_ = exodusRect({330, 150, 540, 155});
  assets_ = exodusRect({100, 68, 1100, 52});
  activity_ = {};
  insight_ = {};
  auto control = [&](QRectF r, const QString &name, std::function<void()> fn) {
    button(exodusRect(r), name, std::move(fn));
  };
  control({462, 12, 42, 42}, "All assets", [this] { assetList(); });
  control({512, 12, 42, 42}, "Wallets", [this] { walletManager(); });
  control({568, 12, 42, 42}, "Send cryptocurrency",
          [this] { send(selectedAsset_); });
  control({626, 12, 42, 42}, "Receive cryptocurrency",
          [this] { receive(selectedAsset_); });
  control({684, 12, 42, 42}, "Refresh balances", [this] {
    network_.refresh();
    market_.refresh();
    if (!demo_)
      market_.requestHistory(Chain(selectedAsset_), chartDays_);
  });
  control({974, 18, 32, 32}, "Encrypted backup", [this] { backupWallet(); });
  control({1012, 18, 32, 32}, "All activity", [this] { activity(); });
  control({1050, 18, 32, 32}, "Choose appearance", [this] { themePicker(); });
  control({1088, 18, 32, 32}, "Connection settings", [this] { settings(); });
  control({1134, 14, 48, 40}, "Wallet access", [this] { unlockWallet(); });
  control({0, 68, 50, 52}, "Choose asset", [this] { assetList(); });
  control({50, 68, 50, 52}, "Search assets", [this] { assetList(); });
  constexpr qreal tabWidth = 100.0;
  for (int i = 0; i < AssetCount; ++i)
    control({100.0 + i * tabWidth, 68, tabWidth, 52},
            QString("Select ") + assetName(Chain(i)),
            [this, i] { selectAsset(i); });
  control({392, 346, 200, 40}, "Send selected asset",
          [this] { send(selectedAsset_); });
  control({610, 346, 200, 40}, "Receive selected asset",
          [this] { receive(selectedAsset_); });
  control({335, 346, 40, 40}, "Selected asset activity",
          [this] { activity(); });
  control({827, 346, 40, 40}, "Refresh selected asset", [this] {
    network_.refresh();
    market_.refresh();
    if (!demo_)
      market_.requestHistory(Chain(selectedAsset_), chartDays_);
  });
  control({1130, 132, 44, 34}, "Asset details", [this] { assetList(); });
  control({1018, 434, 42, 34}, "Toggle price chart", [this] {
    chartCollapsed_ = !chartCollapsed_;
    chartHover_ = -1;
    rebuild();
    update();
  });
  const int days[]{1, 7, 30, 90, 180, 365};
  const QStringList names{"1D", "1W", "1M", "3M", "6M", "1Y"};
  for (int i = 0; !chartCollapsed_ && i < 6; ++i)
    control({817.0 + i * 40, 486, 36, 42}, "Price chart " + names[i],
            [this, d = days[i]] {
              chartDays_ = d;
              chartHover_ = -1;
              if (!demo_ && sessionOpen_)
                market_.requestHistory(Chain(selectedAsset_), d);
              update();
            });
}
void WalletWindow::drawExodus(QPainter &p) {
  for (auto *b : buttons_)
    for (int i = 0; i < AssetCount; ++i)
      if (b->accessibleName() == "Select " + assetName(Chain(i)))
        b->setToolTip(holdingTooltip(i));
  p.save();
  p.scale(width() / 1200.0, height() / 760.0);
  const QColor bg("#191b1d"), dim("#737579"), white("#f4f4f4");
  const QColor color = accents[selectedAsset_];
  p.fillRect(QRectF(0, 0, 1200, 760), bg);
  p.fillRect(QRectF(0, 0, 1200, 68), QColor("#0d0f10"));
  auto text = [&](QRectF r, const QString &s, int size, QColor c,
                  int weight = 400,
                  Qt::Alignment align = Qt::AlignLeft | Qt::AlignVCenter,
                  const QString &family = "Roboto", qreal spacing = 100) {
    QFont font(family);
    font.setPixelSize(size);
    font.setWeight(QFont::Weight(weight));
    font.setHintingPreference(QFont::PreferNoHinting);
    font.setLetterSpacing(QFont::PercentageSpacing, spacing);
    p.setFont(font);
    p.setPen(c);
    p.drawText(r, align, s);
  };
  auto icon = [&](const QString &name, QRectF r, QColor c) {
    QPixmap image(64, 64);
    image.fill(Qt::transparent);
    QPainter paint(&image);
    QIcon(":/assets/ui/" + name + ".svg").paint(&paint, image.rect());
    paint.setCompositionMode(QPainter::CompositionMode_SourceIn);
    paint.fillRect(image.rect(), c);
    paint.end();
    p.drawPixmap(r.toRect(), image);
  };
  // Original Aurum mark, within the reference application's header proportions.
  QPainterPath mark;
  mark.moveTo(39, 18);
  mark.lineTo(51, 34);
  mark.lineTo(39, 50);
  mark.lineTo(27, 34);
  mark.closeSubpath();
  p.setPen(QPen(QColor("#a493ff"), 3));
  p.setBrush(Qt::NoBrush);
  p.drawPath(mark);
  text({72, 16, 270, 35}, totalUsd(), 16, white);
  for (int i = 0; i < 5; ++i) {
    const qreal x = 462 + (i < 2 ? i * 50 : 106 + (i - 2) * 58);
    QLinearGradient shade(x, 12, x + 42, 54);
    shade.setColorAt(0, QColor("#24292c"));
    shade.setColorAt(1, QColor("#101213"));
    p.setPen(Qt::NoPen);
    p.setBrush(shade);
    p.drawRoundedRect(QRectF(x, 12, 42, 42), 4, 4);
    if (i == 0) {
      p.setBrush(Qt::NoBrush);
      p.setPen(QPen(QColor("#735bf2"), 4));
      p.drawEllipse(QRectF(x + 8, 20, 26, 26));
      p.setPen(QPen(QColor("#f3bb35"), 4));
      p.drawArc(QRectF(x + 8, 20, 26, 26), 45 * 16, 85 * 16);
      p.setPen(QPen(QColor("#559ae2"), 4));
      p.drawArc(QRectF(x + 8, 20, 26, 26), 200 * 16, 75 * 16);
    } else if (i == 1) {
      p.setPen(Qt::NoPen);
      p.setBrush(QColor("#46a5fa"));
      p.drawRoundedRect(QRectF(x + 9, 20, 17, 16), 2, 2);
      QLinearGradient purple(x + 8, 26, x + 33, 46);
      purple.setColorAt(0, QColor("#a85dfc"));
      purple.setColorAt(1, QColor("#5c39dd"));
      p.setBrush(purple);
      p.drawRoundedRect(QRectF(x + 8, 25, 26, 21), 3, 3);
      p.setBrush(QColor("#efbe35"));
      p.drawRoundedRect(QRectF(x + 26, 32, 10, 8), 2, 2);
      p.setBrush(QColor("#6658cf"));
      p.drawEllipse(QRectF(x + 29, 34, 3, 3));
    } else if (i == 2) {
      p.setPen(QPen(QColor("#ffc32b"), 4));
      p.drawLine(QPointF(x + 8, 26), QPointF(x + 31, 26));
      p.drawLine(QPointF(x + 26, 21), QPointF(x + 31, 26));
      p.drawLine(QPointF(x + 26, 31), QPointF(x + 31, 26));
      p.setPen(QPen(QColor("#339be4"), 4));
      p.drawLine(QPointF(x + 10, 38), QPointF(x + 33, 38));
      p.drawLine(QPointF(x + 15, 33), QPointF(x + 10, 38));
      p.drawLine(QPointF(x + 15, 43), QPointF(x + 10, 38));
    } else if (i == 3) {
      QPolygonF hex;
      for (int n = 0; n < 6; ++n) {
        double a = (30 + n * 60) * 3.141592653589793 / 180;
        hex << QPointF(x + 21 + 16 * std::cos(a), 33 + 16 * std::sin(a));
      }
      QLinearGradient blue(x + 8, 18, x + 34, 48);
      blue.setColorAt(0, QColor("#7bd7d1"));
      blue.setColorAt(1, QColor("#30375d"));
      p.setPen(Qt::NoPen);
      p.setBrush(blue);
      p.drawPolygon(hex);
      p.setBrush(QColor("#171e33"));
      p.drawRoundedRect(QRectF(x + 12, 23, 18, 20), 2, 2);
      p.setPen(QPen(Qt::white, 2));
      p.drawLine(QPointF(x + 21, 28), QPointF(x + 21, 38));
      p.drawLine(QPointF(x + 16, 33), QPointF(x + 26, 33));
    } else {
      QPainterPath graph;
      graph.moveTo(x + 7, 41);
      graph.lineTo(x + 15, 31);
      graph.lineTo(x + 21, 37);
      graph.lineTo(x + 33, 24);
      QLinearGradient gradient(x + 7, 41, x + 33, 24);
      gradient.setColorAt(0, QColor("#8360f5"));
      gradient.setColorAt(.55, QColor("#8867f9"));
      gradient.setColorAt(1, QColor("#ffd23b"));
      p.setPen(QPen(QBrush(gradient), 4));
      p.setBrush(Qt::NoBrush);
      p.drawPath(graph);
      p.setPen(QPen(QColor("#ffd23b"), 3));
      p.drawLine(QPointF(x + 26, 24), QPointF(x + 33, 24));
      p.drawLine(QPointF(x + 33, 24), QPointF(x + 33, 32));
    }
  }
  p.setPen(QPen(QColor("#5b9ddc"), 2));
  p.drawLine(QPointF(524, 67), QPointF(544, 67));
  icon("archive", {980, 25, 20, 20}, dim);
  icon("activity", {1018, 25, 20, 20}, dim);
  icon("palette", {1056, 25, 20, 20}, dim);
  icon("settings", {1094, 25, 20, 20}, dim);
  p.setPen(Qt::NoPen);
  p.setBrush(QColor("#ec3c9a"));
  p.drawRoundedRect(QRectF(1144, 20, 28, 28), 8, 8);
  icon("lock", {1150, 26, 16, 16}, white);
  p.fillRect(QRectF(0, 68, 1200, 52), QColor("#121517"));
  p.setPen(QPen(QColor("#282c30"), 1));
  p.drawLine(QPointF(50, 69), QPointF(50, 119));
  for (int n = 0; n < 3; ++n)
    p.drawLine(QPointF(17 + n * 3, 89 + n * 5),
               QPointF(34 - n * 3, 89 + n * 5));
  p.setBrush(Qt::NoBrush);
  p.setPen(QPen(dim, 1.3));
  p.drawEllipse(QRectF(68, 86, 11, 11));
  p.drawLine(QPointF(78, 96), QPointF(83, 101));
  constexpr qreal tabWidth = 100.0;
  for (int i = 0; i < AssetCount; ++i) {
    const qreal x = 100 + i * tabWidth;
    if (i == selectedAsset_) {
      p.fillRect(QRectF(x, 69, tabWidth, 48), QColor("#171a1c"));
      p.fillRect(QRectF(x, 117, tabWidth, 2), color);
    }
    text({x + 2, 76, tabWidth - 4, 19}, tabNames[i], 10,
         i == selectedAsset_ ? color : white, 300, Qt::AlignCenter,
         "Roboto Condensed");
    text({x + 2, 95, tabWidth - 4, 17},
         hidden_ ? "••••" : amount(i) + " " + symbol(Chain(i)), 10,
         i == selectedAsset_ ? white : dim, 300, Qt::AlignCenter,
         "Roboto Condensed");
    p.setPen(QColor("#292c2f"));
    p.drawLine(QPointF(x + tabWidth, 84), QPointF(x + tabWidth, 101));
  }
  text({1130, 133, 44, 34}, "⋮", 24, dim, 400, Qt::AlignCenter);
  // The hexagonal asset badge follows the supplied reference; coin art remains
  // official.
  QPainterPath badge;
  for (int i = 0; i < 6; ++i) {
    double a = (30 + i * 60) * 3.141592653589793 / 180;
    QPointF point(600 + 35 * std::cos(a), 190 + 35 * std::sin(a));
    if (i == 0)
      badge.moveTo(point);
    else
      badge.lineTo(point);
  }
  badge.closeSubpath();
  p.setPen(Qt::NoPen);
  p.setBrush(color);
  p.drawPath(badge);
  if (selectedAsset_ == 0)
    QIcon(":/assets/coins/btc-mark.svg").paint(&p, QRect(568, 158, 64, 64));
  else
    coin(p, {575, 165}, selectedAsset_, 50);
  const QString quantity = hidden_ ? "••••••" : amount(selectedAsset_);
  QFont numberFont("Roboto");
  numberFont.setPixelSize(52);
  numberFont.setWeight(QFont::Light);
  numberFont.setLetterSpacing(QFont::PercentageSpacing, 98);
  QFont unitFont("Roboto Condensed");
  unitFont.setPixelSize(24);
  unitFont.setWeight(QFont::Light);
  qreal numberWidth = QFontMetricsF(numberFont).horizontalAdvance(quantity);
  qreal unitWidth =
      QFontMetricsF(unitFont).horizontalAdvance(symbol(Chain(selectedAsset_)));
  qreal fontSize = 52;
  if (numberWidth + unitWidth + 15 > 900) {
    fontSize *= 900 / (numberWidth + unitWidth + 15);
    numberFont.setPixelSize(qRound(fontSize));
    numberWidth = QFontMetricsF(numberFont).horizontalAdvance(quantity);
  }
  const qreal left = (1200 - numberWidth - unitWidth - 15) / 2;
  text({left, 239, numberWidth + 3, 61}, quantity, qRound(fontSize), color, 300,
       Qt::AlignVCenter | Qt::AlignLeft, "Roboto", 98);
  text({left + numberWidth + 15, 256, unitWidth + 4, 40},
       symbol(Chain(selectedAsset_)), 24, color, 300,
       Qt::AlignVCenter | Qt::AlignLeft, "Roboto Condensed");
  const QString fiat = holdingUsd(selectedAsset_);
  QFont fiatFont("Roboto");
  fiatFont.setPixelSize(20);
  fiatFont.setWeight(QFont::Light);
  QFont fiatUnitFont("Roboto");
  fiatUnitFont.setPixelSize(12);
  const qreal fiatWidth = QFontMetricsF(fiatFont).horizontalAdvance(fiat);
  const qreal fiatUnitWidth =
      QFontMetricsF(fiatUnitFont).horizontalAdvance("USD");
  const qreal fiatLeft = (1200 - fiatWidth - fiatUnitWidth - 4) / 2;
  text({fiatLeft, 302, fiatWidth + 1, 24}, fiat, 20, QColor("#d5d5d5"), 300);
  text({fiatLeft + fiatWidth + 4, 306, fiatUnitWidth + 1, 20}, "USD", 12,
       QColor("#a9a9a9"));
  for (const auto &r : {QRectF(392, 346, 200, 40), QRectF(610, 346, 200, 40)}) {
    p.setPen(QPen(color, 1.6));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(r, 20, 20);
  }
  text({392, 346, 200, 40}, "Send", 13, white, 400, Qt::AlignCenter);
  text({610, 346, 200, 40}, "Receive", 13, white, 400, Qt::AlignCenter);
  p.setPen(Qt::NoPen);
  p.setBrush(color);
  p.drawEllipse(QRectF(335, 346, 40, 40));
  p.drawEllipse(QRectF(827, 346, 40, 40));
  icon("activity", {345, 356, 20, 20}, Qt::white);
  icon("refresh-cw", {837, 356, 20, 20}, Qt::white);
  text({145, 440, 400, 25}, "PRICE CHART", 11, dim, 500);
  icon("chevron-down", {1030, 444, 16, 16}, dim);
  if (chartCollapsed_) {
    p.restore();
    return;
  }
  p.setPen(QColor("#282a2c"));
  p.drawLine(QPointF(145, 474), QPointF(1055, 474));
  text({145, 489, 170, 35}, priceText(selectedAsset_), 22, white);
  text({145, 524, 170, 20}, "1 " + symbol(Chain(selectedAsset_)), 11, dim);
  const auto &q = market_.quote(Chain(selectedAsset_));
  const double change = demo_ ? changes[selectedAsset_] : q.change24h;
  const bool hasChange = demo_ || (q.fresh() && q.hasChange);
  text({315, 492, 180, 30},
       hasChange
           ? QString("%1%2%").arg(change >= 0 ? "+" : "").arg(change, 0, 'f', 2)
           : "—",
       14,
       hasChange ? (change >= 0 ? QColor("#74c53c") : QColor("#ef6979")) : dim);
  text({315, 524, 180, 20}, "24h change", 11, dim);
  const int days[]{1, 7, 30, 90, 180, 365};
  const QStringList ranges{"1D", "1W", "1M", "3M", "6M", "1Y"};
  for (int i = 0; i < 6; ++i) {
    text({817.0 + i * 40, 486, 36, 42}, ranges[i], 11,
         days[i] == chartDays_ ? white : dim, 400, Qt::AlignCenter);
    if (days[i] == chartDays_) {
      p.setPen(QPen(white, 1.5));
      p.drawLine(QPointF(823 + i * 40, 526), QPointF(847 + i * 40, 526));
    }
  }
  const auto points = chartPoints();
  const QRectF graph(145, 574, 910, 135);
  if (points.size() > 1) {
    double lo = points[0].y(), hi = lo;
    for (const auto &v : points) {
      lo = std::min(lo, v.y());
      hi = std::max(hi, v.y());
    }
    const double pad = std::max((hi - lo) * .10, hi * .0001);
    lo -= pad;
    hi += pad;
    QPainterPath line;
    for (int i = 0; i < points.size(); ++i) {
      QPointF v(graph.x() + (points[i].x() - points.front().x()) /
                                (points.back().x() - points.front().x()) *
                                graph.width(),
                graph.bottom() -
                    (points[i].y() - lo) / (hi - lo) * graph.height());
      if (i == 0)
        line.moveTo(v);
      else
        line.lineTo(v);
    }
    QPainterPath area = line;
    area.lineTo(graph.bottomRight());
    area.lineTo(graph.bottomLeft());
    area.closeSubpath();
    QColor top = color;
    top.setAlpha(24);
    QColor bottom = color;
    bottom.setAlpha(2);
    QLinearGradient fill(graph.topLeft(), graph.bottomLeft());
    fill.setColorAt(0, top);
    fill.setColorAt(1, bottom);
    p.fillPath(area, fill);
    p.setPen(QPen(color, 1.5));
    p.setBrush(Qt::NoBrush);
    p.drawPath(line);
    for (int i = 0; i < 3; ++i)
      text({145, 555 + i * 60.0, 150, 18}, usdText(hi - (hi - lo) * i / 2), 10,
           dim);
    if (chartHover_ >= 0 && chartHover_ < points.size()) {
      const auto &v = points[chartHover_];
      const QPointF cursor(
          graph.x() + (v.x() - points.front().x()) /
                          (points.back().x() - points.front().x()) *
                          graph.width(),
          graph.bottom() - (v.y() - lo) / (hi - lo) * graph.height());
      p.setPen(QPen(QColor("#777a7e"), 1, Qt::DashLine));
      p.drawLine(QPointF(cursor.x(), graph.top()),
                 QPointF(cursor.x(), graph.bottom()));
      p.setPen(QPen(bg, 3));
      p.setBrush(color);
      p.drawEllipse(cursor, 5, 5);
      const qreal tipWidth = 216,
                  tipX = std::clamp(cursor.x() - tipWidth / 2, graph.left(),
                                    graph.right() - tipWidth);
      const QRectF tip(tipX, graph.top() - 61, tipWidth, 52);
      p.setPen(QPen(QColor("#393c40"), 1));
      p.setBrush(QColor("#26292d"));
      p.drawRoundedRect(tip, 5, 5);
      text(tip.adjusted(10, 3, -10, -27),
           QDateTime::fromMSecsSinceEpoch(qint64(v.x()))
               .toLocalTime()
               .toString("dd MMM yyyy  HH:mm"),
           11, QColor("#b8bbbf"), 400, Qt::AlignCenter);
      text(tip.adjusted(10, 22, -10, -3), usdText(v.y()), 16, white, 500,
           Qt::AlignCenter);
    }
    text({145, 714, 400, 20},
         QDateTime::fromMSecsSinceEpoch(qint64(points.front().x()))
             .toLocalTime()
             .toString("dd MMM yyyy"),
         10, dim);
    text({850, 714, 205, 20},
         QDateTime::fromMSecsSinceEpoch(qint64(points.back().x()))
             .toLocalTime()
             .toString("dd MMM yyyy"),
         10, dim, 400, Qt::AlignRight | Qt::AlignVCenter);
  } else
    text(graph,
         sessionOpen_ ? market_.historyStatus()
                      : "Open wallet to view price history",
         13, dim, 400, Qt::AlignCenter);
  text({20, 738, 1160, 18}, quoteCaption(), 10, dim);
  p.restore();
}
QVector<QPointF> WalletWindow::chartPoints() const {
  if (!demo_)
    return market_.history(Chain(selectedAsset_), chartDays_);
  static const double prices[]{82924,  64.05,   110.23,  2507.03, .101726,
                               750.01, .999137, .999697, .999137, .999697};
  QVector<QPointF> points;
  const qint64 now = QDateTime::currentMSecsSinceEpoch(),
               span = qint64(chartDays_) * 86400000;
  for (int i = 0; i < 90; ++i)
    points.push_back({double(now - span + span * i / 89),
                      prices[selectedAsset_] *
                          (.75 + .25 * i / 89.0 + .035 * std::sin(i * .23) +
                           .014 * std::sin(i * 1.81))});
  return points;
}
int WalletWindow::chartPointAt(QPointF position) const {
  if (stage_ != Stage::Ready || theme_ != 10 || chartCollapsed_)
    return -1;
  QPointF local(position.x() * 1200 / width(), position.y() * 760 / height());
  const QRectF graph(145, 574, 910, 135);
  const auto points = chartPoints();
  if (!graph.contains(local) || points.size() < 2)
    return -1;
  const double stamp =
      points.front().x() + (local.x() - graph.x()) / graph.width() *
                               (points.back().x() - points.front().x());
  auto found = std::lower_bound(points.cbegin(), points.cend(), stamp,
                                [](QPointF p, double x) { return p.x() < x; });
  if (found == points.cend())
    return points.size() - 1;
  int i = int(found - points.cbegin());
  if (i > 0 && stamp - points[i - 1].x() < points[i].x() - stamp)
    --i;
  return i;
}
QString WalletWindow::chartHoverText() const {
  const auto points = chartPoints();
  if (chartHover_ < 0 || chartHover_ >= points.size())
    return {};
  const auto v = points[chartHover_];
  return QDateTime::fromMSecsSinceEpoch(qint64(v.x()))
             .toLocalTime()
             .toString("dd MMM yyyy  HH:mm") +
         "\n" + usdText(v.y()) + " USD";
}
void WalletWindow::mouseMoveEvent(QMouseEvent *event) {
  const int i = chartPointAt(event->position());
  chartPointer_ = event->position();
  if (i != chartHover_) {
    chartHover_ = i;
    update();
  }
  setCursor(i >= 0 ? Qt::CrossCursor : Qt::ArrowCursor);
  QWidget::mouseMoveEvent(event);
}
void WalletWindow::leaveEvent(QEvent *event) {
  chartHover_ = -1;
  setCursor(Qt::ArrowCursor);
  update();
  QWidget::leaveEvent(event);
}
} // namespace aurum
