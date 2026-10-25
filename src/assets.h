#pragma once
#include <QString>
namespace aurum {
enum class Chain {
  Bitcoin,
  Litecoin,
  Solana,
  Ethereum,
  Polygon,
  Bnb,
  UsdtEthereum,
  UsdcEthereum,
  UsdtSolana,
  UsdcSolana,
  Monero
};
constexpr int AssetCount = 11;
QString symbol(Chain);
QString assetName(Chain);
QString networkName(Chain);
QString explorer(Chain);
int decimals(Chain);
bool isEvm(Chain);
bool isToken(Chain);
bool isSpl(Chain);
int keyIndex(Chain);
Chain baseChain(Chain);
quint64 chainId(Chain);
QString tokenAddress(Chain);
} // namespace aurum
