#pragma once
#include "core.h"
namespace aurum {
bool isEdwardsPoint(const QByteArray &compressed);
QByteArray associatedTokenAddress(const QByteArray &owner,
                                  const QByteArray &mint);
QByteArray splTransferMessage(const QByteArray &owner,
                              const QByteArray &recipient,
                              const QByteArray &sourceTokenAccount,
                              const QByteArray &mint,
                              const QByteArray &blockhash, quint64 units,
                              int precision = 6);
} // namespace aurum
