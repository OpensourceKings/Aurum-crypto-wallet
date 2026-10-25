#pragma once
#include <QString>
#include <QVector>
namespace aurum {
// Display/swap-only coins. No keys, addresses or signing are derived for these;
// they exist so the swap screen can pay out to an address the user supplies.
struct CatalogCoin {
  QString ticker, name, network;
};
const QVector<CatalogCoin> &coinCatalog();
} // namespace aurum
