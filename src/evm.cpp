#include "evm.h"
#include <QRegularExpression>
#include <cryptopp/keccak.h>
#include <memory>
#include <openssl/bn.h>
#include <secp256k1.h>
#include <secp256k1_recovery.h>
#include <sodium.h>
namespace aurum {
namespace {
void require(bool ok, const char *s) {
  if (!ok)
    throw std::runtime_error(s);
}
using Big = std::unique_ptr<BIGNUM, decltype(&BN_free)>;
Big number(const QString &s) {
  require(s.size() <= 78 && QRegularExpression("^[0-9]+$").match(s).hasMatch(),
          "Invalid uint256 decimal");
  BIGNUM *p = nullptr;
  BN_dec2bn(&p, s.toLatin1().constData());
  Big n(p, BN_free);
  require(n && BN_num_bits(n.get()) <= 256, "Amount exceeds uint256");
  return n;
}
QByteArray strip(QByteArray b) {
  while (!b.isEmpty() && b.front() == 0)
    b.remove(0, 1);
  return b;
}
QByteArray rlpBytes(const QByteArray &b) {
  if (b.size() == 1 && static_cast<unsigned char>(b[0]) < 128)
    return b;
  if (b.size() < 56)
    return QByteArray(1, char(128 + b.size())) + b;
  QByteArray n = strip(QByteArray::fromHex(
      QByteArray::number(b.size(), 16).rightJustified(2, '0')));
  return QByteArray(1, char(183 + n.size())) + n + b;
}
QByteArray rlpList(const QList<QByteArray> &fields) {
  QByteArray b;
  for (const auto &f : fields)
    b += rlpBytes(f);
  if (b.size() < 56)
    return QByteArray(1, char(192 + b.size())) + b;
  quint64 len = b.size();
  QByteArray n;
  while (len) {
    n.prepend(char(len & 255));
    len >>= 8;
  }
  return QByteArray(1, char(247 + n.size())) + n + b;
}
QByteArray scalar(quint64 n) {
  QByteArray b;
  while (n) {
    b.prepend(char(n & 255));
    n >>= 8;
  }
  return b;
}
} // namespace
QByteArray keccak(const QByteArray &b) {
  QByteArray h(32, 0);
  CryptoPP::Keccak_256 k;
  k.CalculateDigest(reinterpret_cast<unsigned char *>(h.data()),
                    reinterpret_cast<const unsigned char *>(b.constData()),
                    b.size());
  return h;
}
QByteArray uint256(const QString &s) {
  auto n = number(s);
  QByteArray b(32, 0);
  require(BN_bn2binpad(n.get(), reinterpret_cast<unsigned char *>(b.data()),
                       32) == 32,
          "Integer conversion failed");
  return b;
}
QString decimal256(const QByteArray &b) {
  require(b.size() <= 32, "Integer exceeds 256 bits");
  Big n(BN_bin2bn(reinterpret_cast<const unsigned char *>(b.constData()),
                  b.size(), nullptr),
        BN_free);
  require(bool(n), "Integer allocation failed");
  char *text = BN_bn2dec(n.get());
  QString s = QString::fromLatin1(text);
  OPENSSL_free(text);
  return s;
}
QString parseUnits(const QString &s, int precision) {
  require(precision >= 0 && precision <= 18, "Invalid precision");
  require(QRegularExpression("^[0-9]+(?:\\.[0-9]+)?$").match(s).hasMatch(),
          "Use an unsigned decimal amount");
  auto parts = s.split('.');
  QString fraction = parts.size() == 2 ? parts[1] : "";
  require(fraction.size() <= precision, "Too many decimal places");
  return decimal256(uint256(parts[0] + fraction.leftJustified(precision, '0')));
}
QString formatUnits(const QString &units, int precision) {
  QString s = decimal256(uint256(units));
  if (!precision)
    return s;
  s = s.rightJustified(precision + 1, '0');
  s.insert(s.size() - precision, '.');
  while (s.endsWith('0'))
    s.chop(1);
  if (s.endsWith('.'))
    s.chop(1);
  return s;
}
QString hexQuantity(const QString &decimal) {
  auto b = strip(uint256(decimal));
  auto h = b.toHex();
  while (h.startsWith('0'))
    h.remove(0, 1);
  return "0x" + (h.isEmpty() ? QByteArray("0") : h);
}
QString quantityDecimal(const QString &h) {
  require(QRegularExpression("^0x[0-9a-fA-F]{1,64}$").match(h).hasMatch(),
          "Invalid node quantity");
  QByteArray s = h.mid(2).toLatin1();
  if (s.size() % 2)
    s.prepend('0');
  return decimal256(QByteArray::fromHex(s));
}
QString add256(const QString &a, const QString &b) {
  auto x = number(a), y = number(b);
  require(BN_add(x.get(), x.get(), y.get()) == 1 && BN_num_bits(x.get()) <= 256,
          "Amount overflow");
  char *p = BN_bn2dec(x.get());
  QString result = QString::fromLatin1(p);
  OPENSSL_free(p);
  return result;
}
int compare256(const QString &a, const QString &b) {
  auto x = number(a), y = number(b);
  return BN_cmp(x.get(), y.get());
}
QString checksumAddress(const QByteArray &b) {
  require(b.size() == 20, "Invalid EVM address size");
  QString low = QString::fromLatin1(b.toHex()), out = low;
  auto h = keccak(low.toLatin1()).toHex();
  for (int i = 0; i < 40; ++i)
    if (h[i] >= '8' && low[i].isLetter())
      out[i] = low[i].toUpper();
  return "0x" + out;
}
QByteArray evmAddressBytes(const QString &s) {
  require(QRegularExpression("^0x[0-9a-fA-F]{40}$").match(s).hasMatch(),
          "Invalid EVM address");
  auto b = QByteArray::fromHex(s.mid(2).toLatin1());
  require(s == s.toLower() || s.mid(2) == s.mid(2).toUpper() ||
              s == checksumAddress(b),
          "EIP-55 address checksum mismatch");
  return b;
}
QString evmAddress(const unsigned char *key) {
  return checksumAddress(
      keccak(publicKey(key, Chain::Ethereum).mid(1)).right(20));
}
QByteArray erc20Transfer(const QString &to, const QString &units) {
  return QByteArray::fromHex("a9059cbb") + QByteArray(12, 0) +
         evmAddressBytes(to) + uint256(units);
}
QByteArray erc20Balance(const QString &owner) {
  return QByteArray::fromHex("70a08231") + QByteArray(12, 0) +
         evmAddressBytes(owner);
}
SignedTransaction signEvm(const unsigned char *key, const EvmTransfer &t) {
  require(t.chain > 0 && t.chain < 1000000000 && t.gasLimit >= 21000 &&
              t.gasLimit <= 1000000,
          "Invalid chain or gas limit");
  require(t.data.size() <= 68, "Only native and ERC-20 transfers supported");
  require(t.value != "0" || !t.data.isEmpty(), "Empty transfer");
  auto gas = number(t.gasPrice);
  require(BN_mul_word(gas.get(), t.gasLimit) == 1 &&
              BN_num_bits(gas.get()) <= 60,
          "Excessive maximum network fee");
  quint64 fee = BN_get_word(gas.get());
  QList<QByteArray> fields{scalar(t.nonce),         strip(uint256(t.gasPrice)),
                           scalar(t.gasLimit),      evmAddressBytes(t.to),
                           strip(uint256(t.value)), t.data};
  auto unsignedFields = fields;
  unsignedFields << scalar(t.chain) << QByteArray() << QByteArray();
  auto digest = keccak(rlpList(unsignedFields));
  auto *ctx = secp256k1_context_create(SECP256K1_CONTEXT_NONE);
  unsigned char random[32];
  randombytes_buf(random, 32);
  int ok = secp256k1_context_randomize(ctx, random);
  sodium_memzero(random, 32);
  secp256k1_ecdsa_recoverable_signature sig;
  if (ok)
    ok = secp256k1_ecdsa_sign_recoverable(
        ctx, &sig, reinterpret_cast<const unsigned char *>(digest.constData()),
        key, nullptr, nullptr);
  unsigned char compact[64];
  int recovery = 0;
  if (ok)
    secp256k1_ecdsa_recoverable_signature_serialize_compact(ctx, compact,
                                                            &recovery, &sig);
  secp256k1_context_destroy(ctx);
  require(ok && recovery < 2, "EVM signing failed");
  fields << scalar(t.chain * 2 + 35 + recovery)
         << strip(QByteArray(reinterpret_cast<char *>(compact), 32))
         << strip(QByteArray(reinterpret_cast<char *>(compact + 32), 32));
  auto wire = rlpList(fields);
  return {wire, "0x" + QString::fromLatin1(keccak(wire).toHex()), fee, 0, 0};
}
} // namespace aurum
