#include "window.h"
#include <QHelpEvent>
#include <QIcon>
#include <QLocale>
#include <QPainterPath>
#include <cmath>
namespace aurum {
namespace {
const double demoPrices[]{82924,  64.05,   110.23,  2507.03, .101726,
                          750.01, .999137, .999697, .999137, .999697, 310.42};
const double demoChanges[]{.10,  .39,   .42,  .59,   1.72,
                           1.22, -.012, .002, -.012, .002};
const QColor assetColors[]{"#f7a640", "#b7b9bc", "#32d69f", "#8799fa",
                           "#965bf1", "#f5c337", "#50b996", "#4b91eb",
                           "#50b996", "#4b91eb"};
} // namespace
double WalletWindow::holdingValue(int i) const {
  if (!demo_ && (!sessionOpen_ || !network_.state(Chain(i)).known))
    return -1;
  bool ok = false;
  double units = amount(i).toDouble(&ok);
  if (!ok || !std::isfinite(units) || units < 0)
    return -1;
  if (units == 0)
    return 0;
  const auto &q = market_.quote(Chain(i));
  if (!demo_ && !q.fresh())
    return -1;
  const double v = units * (demo_ ? demoPrices[i] : q.usd);
  return std::isfinite(v) ? v : -1;
}
QString WalletWindow::holdingUsd(int i) const {
  if (hidden_)
    return "••••••";
  const auto value = holdingValue(i);
  return value < 0 ? QString("—") : usdText(value);
}
QString WalletWindow::priceText(int i) const {
  const auto &q = market_.quote(Chain(i));
  if (!demo_ && !q.fresh())
    return "—";
  const double price = demo_ ? demoPrices[i] : q.usd;
  const int digits = price < 1 ? 6 : price < 10 ? 4 : 2;
  return "$" + QLocale(QLocale::English, QLocale::UnitedStates)
                   .toString(price, 'f', digits);
}
QString WalletWindow::totalUsd() const {
  if (hidden_)
    return "••••••";
  double total = 0;
  int count = 0;
  for (int i = 0; i < AssetCount; ++i) {
    const auto value = holdingValue(i);
    if (value >= 0) {
      total += value;
      ++count;
    }
  }
  if (!count || (count < AssetCount && total == 0))
    return "—";
  return (count < AssetCount ? QString("≈ ") : QString()) + usdText(total);
}
QString WalletWindow::quoteCaption() const {
  if (demo_)
    return "Design preview · sample prices";
  if (!sessionOpen_)
    return "Open wallet to view balances";
  int count = 0;
  for (int i = 0; i < AssetCount; ++i)
    if (holdingValue(i) >= 0)
      ++count;
  return QString("%1 / %2 balances valued · ").arg(count).arg(AssetCount) +
         market_.status();
}
QString WalletWindow::holdingTooltip(int i) const {
  if (hidden_)
    return "Balances hidden";
  const auto &q = market_.quote(Chain(i));
  return assetName(Chain(i)) + "\n" + holdingUsd(i) + " USD\n" + amount(i) +
         " " + symbol(Chain(i)) + "\n" + priceText(i) + " per " +
         symbol(Chain(i)) + "\n" +
         (demo_ ? "Sample data"
                : network_.connectionStatus(Chain(i)) + "\n" +
                      (q.fresh()
                           ? "Price updated " +
                                 q.updated.toLocalTime().toString("HH:mm:ss")
                           : "Price unavailable or stale"));
}
QRectF WalletWindow::assetBounds(int i) const {
  if (i < 0 || i >= AssetCount || assets_.isEmpty())
    return {};
  if (themes()[theme_].layout == 5) {
    constexpr qreal tabWidth = 100.0;
    return exodusRect({100.0 + i * tabWidth, 68, tabWidth, 52});
  }
  if (assets_.width() > 900) {
    if (i >= 3)
      return {};
    const qreal w = assets_.width() - 52, gap = 18, cw = (w - 2 * gap) / 3;
    return {assets_.x() + 26 + i * (cw + gap), assets_.y() + 84, cw,
            assets_.height() - 115};
  }
  if (i >= 6)
    return {};
  const qreal top = assets_.y() + 98, row = (assets_.bottom() - 18 - top) / 6;
  return {assets_.x() + 26, top + i * row, assets_.width() - 52, row};
}
} // namespace aurum
