#pragma once
#include "core.h"
namespace aurum {
QByteArray keccak(const QByteArray &);
QByteArray uint256(const QString &decimal);
QString decimal256(const QByteArray &);
QString parseUnits(const QString &, int decimals);
QString formatUnits(const QString &, int decimals);
QString hexQuantity(const QString &decimal);
QString quantityDecimal(const QString &hex);
QString add256(const QString &, const QString &);
int compare256(const QString &, const QString &);
QString checksumAddress(const QByteArray &);
QByteArray evmAddressBytes(const QString &);
QString evmAddress(const unsigned char *);
QByteArray erc20Transfer(const QString &recipient, const QString &amount);
QByteArray erc20Balance(const QString &owner);
struct EvmTransfer {
  quint64 chain = 1, nonce = 0, gasLimit = 21000;
  QString gasPrice = "0", value = "0", to;
  QByteArray data;
};
SignedTransaction signEvm(const unsigned char *, const EvmTransfer &);
} // namespace aurum
