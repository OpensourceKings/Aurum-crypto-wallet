#include "spl.h"
#include <cryptopp/integer.h>
#include <cryptopp/nbtheory.h>
#include <set>
namespace aurum {
namespace {
void require(bool ok, const char *s) {
  if (!ok)
    throw std::runtime_error(s);
}
QByteArray tokenProgram() {
  return unbase58("TokenkegQfeZyiNwAJbNbGKPFXCWuBvf9Ss623VQ5DA");
}
QByteArray ataProgram() {
  return unbase58("ATokenGPvbdGVxr1b2hvZbsiqW5xWH25efTNsLJA8knL");
}
QByteArray little(quint64 n) {
  QByteArray b;
  for (int i = 0; i < 8; ++i)
    b.append(char(n >> (8 * i)));
  return b;
}
} // namespace
bool isEdwardsPoint(const QByteArray &compressed) {
  if (compressed.size() != 32)
    return false;
  using CryptoPP::Integer;
  QByteArray b = compressed;
  b[31] = char(static_cast<unsigned char>(b[31]) & 127);
  Integer p = Integer::Power2(255) - 19,
          y(reinterpret_cast<const unsigned char *>(b.constData()), 32,
            Integer::UNSIGNED, CryptoPP::LITTLE_ENDIAN_ORDER);
  y %= p;
  Integer d = ((p - 121665) * Integer(121666).InverseMod(p)) % p;
  Integer y2 = (y * y) % p, denom = (d * y2 + 1) % p;
  if (denom.IsZero())
    return false;
  Integer x2 = ((y2 + p - 1) * denom.InverseMod(p)) % p;
  return x2.IsZero() || a_exp_b_mod_c(x2, (p - 1) / 2, p) == Integer::One();
}
QByteArray associatedTokenAddress(const QByteArray &owner,
                                  const QByteArray &mint) {
  require(owner.size() == 32 && mint.size() == 32,
          "Invalid token account seeds");
  for (int bump = 255; bump >= 0; --bump) {
    auto hash =
        sha256(owner + tokenProgram() + mint + QByteArray(1, char(bump)) +
               ataProgram() + "ProgramDerivedAddress");
    if (!isEdwardsPoint(hash))
      return hash;
  }
  throw std::runtime_error("Unable to derive associated token account");
}
QByteArray splTransferMessage(const QByteArray &owner,
                              const QByteArray &recipient,
                              const QByteArray &source, const QByteArray &mint,
                              const QByteArray &blockhash, quint64 amount,
                              int precision) {
  require(owner.size() == 32 && recipient.size() == 32 && source.size() == 32 &&
              mint.size() == 32 && blockhash.size() == 32,
          "Invalid SPL account");
  require(amount > 0 && precision == 6, "Invalid token amount or decimals");
  require(owner != recipient, "Choose a recipient different from this wallet");
  auto dest = associatedTokenAddress(recipient, mint);
  QList<QByteArray> accounts{owner,          source,      dest,
                             recipient,      mint,        QByteArray(32, 0),
                             tokenProgram(), ataProgram()};
  std::set<QByteArray> unique(accounts.begin(), accounts.end());
  require(unique.size() == 8,
          "Recipient collides with a required program account");
  // Signer/payer, two writable token accounts, then five readonly accounts.
  QByteArray msg = QByteArray::fromHex("01000508");
  for (const auto &a : accounts)
    msg += a;
  msg += blockhash;
  msg += QByteArray::fromHex("02");
  // Idempotent ATA creation: payer, ATA, recipient owner, mint, system, token.
  msg += QByteArray::fromHex("07060002030405060101");
  // SPL TransferChecked: source, mint, destination, authority; opcode 12, u64,
  // decimals.
  msg += QByteArray::fromHex("0604010402000a0c") + little(amount) +
         QByteArray(1, char(precision));
  return msg;
}
} // namespace aurum
