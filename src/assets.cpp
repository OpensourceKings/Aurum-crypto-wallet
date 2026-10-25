#include "assets.h"
namespace aurum {
QString symbol(Chain c) {
  static const QString s[] = {"BTC", "LTC",  "SOL",  "ETH",  "POL",
                              "BNB", "USDT", "USDC", "USDT", "USDC",
                              "XMR"};
  return s[int(c)];
}
QString assetName(Chain c) {
  static const QString n[] = {
      "Bitcoin",       "Litecoin",     "Solana",          "Ethereum",
      "Polygon",       "BNB",          "USDT · Ethereum", "USDC · Ethereum",
      "USDT · Solana", "USDC · Solana", "Monero"};
  return n[int(c)];
}
bool isSpl(Chain c) { return c == Chain::UsdtSolana || c == Chain::UsdcSolana; }
bool isToken(Chain c) {
  return c == Chain::UsdtEthereum || c == Chain::UsdcEthereum || isSpl(c);
}
bool isEvm(Chain c) {
  return c == Chain::Ethereum || c == Chain::Polygon || c == Chain::Bnb ||
         c == Chain::UsdtEthereum || c == Chain::UsdcEthereum;
}
Chain baseChain(Chain c) {
  if (c == Chain::UsdtEthereum || c == Chain::UsdcEthereum)
    return Chain::Ethereum;
  if (isSpl(c))
    return Chain::Solana;
  return c;
}
int keyIndex(Chain c) {
  if (c == Chain::Monero)
    return -1;
  return int(baseChain(c));
}
QString networkName(Chain c) { return assetName(baseChain(c)); }
int decimals(Chain c) {
  return c == Chain::Monero ? 12
         : isToken(c)       ? 6
         : c == Chain::Solana ? 9
         : isEvm(c)           ? 18
                              : 8;
}
quint64 chainId(Chain c) {
  c = baseChain(c);
  return c == Chain::Ethereum  ? 1
         : c == Chain::Polygon ? 137
         : c == Chain::Bnb     ? 56
                               : 0;
}
QString tokenAddress(Chain c) {
  switch (c) {
  case Chain::UsdtEthereum:
    return "0xdAC17F958D2ee523a2206206994597C13D831ec7";
  case Chain::UsdcEthereum:
    return "0xA0b86991c6218b36c1d19D4a2e9Eb0cE3606eB48";
  case Chain::UsdtSolana:
    return "Es9vMFrzaCERmJfrF4H2FYD4KCoNkY11McCe8BenwNYB";
  case Chain::UsdcSolana:
    return "EPjFWdd5AufqSSqeM2qN1xzybapC8G4wEGGkZwyTDt1v";
  default:
    return {};
  }
}
QString explorer(Chain c) {
  c = baseChain(c);
  switch (c) {
  case Chain::Bitcoin:
    return "https://blockstream.info/tx/";
  case Chain::Litecoin:
    return "https://litecoinspace.org/tx/";
  case Chain::Solana:
    return "https://explorer.solana.com/tx/";
  case Chain::Ethereum:
    return "https://etherscan.io/tx/";
  case Chain::Polygon:
    return "https://polygonscan.com/tx/";
  case Chain::Bnb:
    return "https://bscscan.com/tx/";
  case Chain::Monero:
    return "https://xmrchain.net/tx/";
  default:
    return {};
  }
}
} // namespace aurum
