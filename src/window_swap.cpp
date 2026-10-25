#include "catalog.h"
#include "swap.h"
#include "window.h"
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
namespace aurum {
namespace {
struct Pair {
  QString ticker, network, label;
  int walletIndex; // -1 for catalog-only coins
};
QString trocadorNetwork(Chain c) {
  switch (baseChain(c)) {
  case Chain::Ethereum:
    return isToken(c) ? "ERC20" : "Mainnet";
  case Chain::Solana:
    return "Mainnet";
  case Chain::Polygon:
    return "Polygon";
  case Chain::Bnb:
    return "BEP20";
  default:
    return "Mainnet";
  }
}
QVector<Pair> pairs() {
  QVector<Pair> list;
  for (int i = 0; i < AssetCount; ++i) {
    const Chain c = Chain(i);
    QString t = symbol(c) == "POL" ? "MATIC" : symbol(c);
    list.push_back({t, isSpl(c) ? "Solana" : trocadorNetwork(c),
                    assetName(c), i});
  }
  for (const auto &coin : coinCatalog())
    list.push_back({coin.ticker, coin.network,
                    coin.name + " · " + coin.network, -1});
  return list;
}
QLabel *text(const QString &s, int size = 14) {
  auto *l = new QLabel(s);
  l->setTextFormat(Qt::PlainText);
  l->setWordWrap(true);
  l->setStyleSheet(QString("font-size:%1px;").arg(size));
  return l;
}
} // namespace
void WalletWindow::swap(int selected) {
  if (!demo_ && !sessionOpen_) {
    unlockWallet();
    if (!sessionOpen_)
      return;
  }
  const auto list = pairs();
  SwapService service;
  service.setApiKey(SwapService::envApiKey());
  SwapRate rate;
  bool haveRate = false;
  QDialog d(this);
  d.setWindowTitle("Swap");
  d.setMinimumWidth(580);
  d.setStyleSheet(uiStyle());
  QVBoxLayout v(&d);
  v.setContentsMargins(30, 26, 30, 26);
  v.setSpacing(14);
  v.addWidget(text("Swap", 26));
  v.addWidget(text("Routed through Trocador. Pick a pair, get a quote, then "
                   "send the deposit from this wallet.",
                   13));
  QComboBox from, to;
  for (const auto &p : list) {
    to.addItem(p.label + "  (" + p.ticker + ")");
    if (p.walletIndex >= 0)
      from.addItem(p.label + "  (" + p.ticker + ")", p.walletIndex);
  }
  from.setCurrentIndex(std::clamp(selected, 0, from.count() - 1));
  to.setCurrentIndex(std::min<int>(to.count() - 1, selected == int(Chain::Monero) ? 0 : int(Chain::Monero)));
  v.addWidget(text("From"));
  v.addWidget(&from);
  QLineEdit amountField;
  amountField.setPlaceholderText("Amount to swap");
  amountField.setAccessibleName("Swap amount");
  v.addWidget(&amountField);
  v.addWidget(text("To"));
  v.addWidget(&to);
  QLineEdit payout, refund;
  payout.setPlaceholderText("Payout address (filled for wallet assets)");
  payout.setAccessibleName("Payout address");
  refund.setPlaceholderText("Refund address (optional)");
  refund.setAccessibleName("Refund address");
  v.addWidget(&payout);
  v.addWidget(&refund);
  auto fillAddresses = [&] {
    const auto &p = list[to.currentIndex()];
    payout.setText(p.walletIndex >= 0 && !demo_ ? addresses_[p.walletIndex]
                                                : QString());
    payout.setReadOnly(p.walletIndex >= 0 && !demo_);
    const int fi = from.currentData().toInt();
    refund.setText(demo_ ? QString() : addresses_[fi]);
  };
  connect(&to, &QComboBox::currentIndexChanged, &d, fillAddresses);
  connect(&from, &QComboBox::currentIndexChanged, &d, fillAddresses);
  fillAddresses();
  QLabel status("Enter an amount and request a quote.");
  status.setWordWrap(true);
  QLabel depositInfo;
  depositInfo.setWordWrap(true);
  depositInfo.setTextInteractionFlags(Qt::TextSelectableByMouse);
  auto *quote = new QPushButton("Get quote");
  auto *confirm = new QPushButton("Create swap");
  auto *copy = new QPushButton("Copy deposit address");
  auto *sendNow = new QPushButton("Open Send");
  for (auto *b : {quote, confirm, copy, sendNow}) {
    b->setMinimumHeight(44);
    b->setCursor(Qt::PointingHandCursor);
  }
  confirm->setEnabled(false);
  copy->hide();
  sendNow->hide();
  v.addWidget(&status);
  v.addWidget(&depositInfo);
  v.addWidget(quote);
  v.addWidget(confirm);
  v.addWidget(copy);
  v.addWidget(sendNow);
  if (!service.hasApiKey())
    status.setText("No Trocador API key. Set AURUM_TROCADOR_API_KEY and "
                   "restart to enable quotes.");
  QString depositAddress;
  QString tradeId;
  QTimer poll;
  poll.setInterval(15000);
  connect(&poll, &QTimer::timeout, &d,
          [&] { service.pollTrade(tradeId); });
  connect(quote, &QPushButton::clicked, &d, [&] {
    haveRate = false;
    confirm->setEnabled(false);
    bool ok = false;
    const double amount = amountField.text().trimmed().toDouble(&ok);
    const auto &f = list[from.currentData().toInt()];
    const auto &t = list[to.currentIndex()];
    status.setText("Requesting quote…");
    service.requestRate(f.ticker, f.network, t.ticker, t.network,
                        ok ? amount : 0);
  });
  connect(&service, &SwapService::rateReady, &d, [&](SwapRate r) {
    rate = r;
    haveRate = true;
    confirm->setEnabled(true);
    status.setText(QString("Quote: %1 %2 → about %3 %4 via %5%6")
                       .arg(r.amountFrom, 0, 'f', 8)
                       .arg(list[from.currentData().toInt()].ticker)
                       .arg(r.amountTo, 0, 'f', 8)
                       .arg(list[to.currentIndex()].ticker, r.provider,
                            r.kycRating.isEmpty()
                                ? QString()
                                : " (KYC " + r.kycRating + ")"));
  });
  connect(confirm, &QPushButton::clicked, &d, [&] {
    if (!haveRate)
      return;
    confirm->setEnabled(false);
    status.setText("Creating swap…");
    service.createTrade(rate, payout.text(), refund.text());
  });
  connect(&service, &SwapService::tradeReady, &d, [&](SwapTrade t) {
    tradeId = t.id;
    if (!t.depositAddress.isEmpty())
      depositAddress = t.depositAddress;
    status.setText("Swap " + t.id + " · " +
                   (t.status.isEmpty() ? QString("created") : t.status));
    if (!depositAddress.isEmpty()) {
      depositInfo.setText(QString("Send %1 %2 to:\n%3%4")
                              .arg(t.amountFrom, 0, 'f', 8)
                              .arg(list[from.currentData().toInt()].ticker,
                                   depositAddress,
                                   t.memo.isEmpty() ? QString()
                                                    : "\nMemo/tag: " + t.memo));
      copy->show();
      sendNow->show();
      quote->setEnabled(false);
      poll.start();
    }
  });
  connect(&service, &SwapService::failed, &d, [&](QString msg) {
    status.setText(msg);
    confirm->setEnabled(haveRate && tradeId.isEmpty());
  });
  connect(copy, &QPushButton::clicked, &d, [&] {
    QApplication::clipboard()->setText(depositAddress);
    status.setText("Deposit address copied.");
  });
  connect(sendNow, &QPushButton::clicked, &d, [this, &d, &from] {
    const int i = from.currentData().toInt();
    d.accept();
    QTimer::singleShot(0, this, [this, i] { send(i); });
  });
  d.exec();
  service.cancel();
}
} // namespace aurum
