#pragma once
#include "evm.h"
#include <array>
namespace aurum {
struct PublicWallet {
  std::array<QByteArray, AssetCount> publicKeys;
  std::array<QString, AssetCount> addresses;
};
// Password bytes are erased even when launching the helper fails.
PublicWallet openIsolated(const QString &file, QByteArray &password,
                          bool create = false);
SignedTransaction signUtxoIsolated(const QString &file, QByteArray &password,
                                   const QByteArray &expected, Chain chain,
                                   const QVector<Utxo> &inputs,
                                   const QString &to, quint64 amount,
                                   quint64 rate);
SignedTransaction signEvmIsolated(const QString &file, QByteArray &password,
                                  const QByteArray &expected, Chain chain,
                                  const EvmTransfer &transfer);
SignedTransaction signSolanaIsolated(const QString &file, QByteArray &password,
                                     const QByteArray &expected, Chain chain,
                                     const QByteArray &message);
} // namespace aurum
