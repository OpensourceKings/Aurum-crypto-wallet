#include "core.h"
#include "evm.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <algorithm>
#include <limits>
#include <openssl/evp.h>
#include <secp256k1.h>
#include <set>
#include <sodium.h>

namespace aurum {
namespace {
[[noreturn]] void fail(const char *message) {
  throw std::runtime_error(message);
}
void require(bool valid, const char *message) {
  if (!valid)
    fail(message);
}
struct Context {
  secp256k1_context *value = secp256k1_context_create(SECP256K1_CONTEXT_NONE);
  Context() {
    unsigned char seed[32];
    randombytes_buf(seed, 32);
    require(secp256k1_context_randomize(value, seed) == 1,
            "Context randomization failed");
    sodium_memzero(seed, 32);
  }
  ~Context() { secp256k1_context_destroy(value); }
};
Context &ctx() {
  static Context context;
  return context;
}
QByteArray le(quint64 value, int size) {
  QByteArray out;
  for (int i = 0; i < size; ++i)
    out.append(char(value >> (8 * i)));
  return out;
}
QByteArray compact(quint64 n) {
  if (n < 253)
    return QByteArray(1, char(n));
  if (n <= 65535)
    return QByteArray(1, char(253)) + le(n, 2);
  if (n <= 0xffffffff)
    return QByteArray(1, char(254)) + le(n, 4);
  return QByteArray(1, char(255)) + le(n, 8);
}
QByteArray varbytes(const QByteArray &v) { return compact(v.size()) + v; }
QByteArray reversed(QByteArray a) {
  std::reverse(a.begin(), a.end());
  return a;
}
QByteArray outpoint(const Utxo &u) {
  require(u.txid.size() == 32, "Invalid transaction identifier");
  return reversed(u.txid) + le(u.index, 4);
}
quint32 polymod(const QVector<int> &values) {
  quint32 chk = 1;
  const quint32 gen[] = {0x3b6a57b2, 0x26508e6d, 0x1ea119fa, 0x3d4233dd,
                         0x2a1462b3};
  for (int v : values) {
    quint32 top = chk >> 25;
    chk = ((chk & 0x1ffffff) << 5) ^ v;
    for (int i = 0; i < 5; ++i)
      if ((top >> i) & 1)
        chk ^= gen[i];
  }
  return chk;
}
QVector<int> expand(const QString &hrp) {
  QVector<int> v;
  for (QChar c : hrp)
    v << (c.unicode() >> 5);
  v << 0;
  for (QChar c : hrp)
    v << (c.unicode() & 31);
  return v;
}
QVector<int> convert(const QVector<int> &data, int from, int to, bool pad) {
  quint32 acc = 0;
  int bits = 0;
  QVector<int> result;
  int mask = (1 << to) - 1;
  for (int v : data) {
    require(v >= 0 && (v >> from) == 0, "Invalid address encoding");
    acc = ((acc << from) | v) & ((1 << (from + to - 1)) - 1);
    bits += from;
    while (bits >= to) {
      bits -= to;
      result << int((acc >> bits) & mask);
    }
  }
  if (pad) {
    if (bits)
      result << int((acc << (to - bits)) & mask);
  } else
    require(bits < from && ((acc << (to - bits)) & mask) == 0,
            "Invalid address padding");
  return result;
}
const QString alphabet =
    "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
const QString chars = "qpzry9x8gf2tvdw0s3jn54khce6mua7l";
QByteArray output(quint64 value, const QByteArray &script) {
  return le(value, 8) + varbytes(script);
}
QByteArray serializeBase(const QVector<Utxo> &inputs, const QByteArray &outs,
                         int count) {
  QByteArray tx = le(2, 4) + compact(inputs.size());
  for (const auto &u : inputs)
    tx += outpoint(u) + QByteArray(1, 0) + le(u.sequence, 4);
  return tx + compact(count) + outs + le(0, 4);
}
} // namespace
quint64 parseAmount(const QString &text, int precision) {
  require(precision >= 0 && precision <= 12, "Invalid precision");
  require(QRegularExpression("^[0-9]+(?:\\.[0-9]+)?$").match(text).hasMatch(),
          "Enter an unsigned decimal amount (no exponent or separators)");
  const auto parts = text.split('.');
  QString fraction = parts.size() == 2 ? parts[1] : "";
  require(fraction.size() <= precision, "Too many decimal places");
  bool ok = false;
  quint64 whole = parts[0].toULongLong(&ok);
  require(ok, "Amount overflow");
  quint64 scale = 1;
  for (int i = 0; i < precision; ++i)
    scale *= 10;
  fraction = fraction.leftJustified(precision, '0');
  quint64 minor = precision ? fraction.toULongLong() : 0;
  require(whole <= (std::numeric_limits<quint64>::max() - minor) / scale,
          "Amount overflow");
  return whole * scale + minor;
}
QString formatAmount(quint64 amount, int precision) {
  quint64 scale = 1;
  for (int i = 0; i < precision; ++i)
    scale *= 10;
  QString s = QString::number(amount / scale);
  if (precision) {
    QString f = QString::number(amount % scale).rightJustified(precision, '0');
    while (f.endsWith('0'))
      f.chop(1);
    if (!f.isEmpty())
      s += '.' + f;
  }
  return s;
}
QByteArray sha256(const QByteArray &input) {
  return QCryptographicHash::hash(input, QCryptographicHash::Sha256);
}
QByteArray hash256(const QByteArray &input) { return sha256(sha256(input)); }
QByteArray hash160(const QByteArray &input) {
  QByteArray s = sha256(input), out(20, 0);
  unsigned int n = 0;
  require(EVP_Digest(s.constData(), s.size(),
                     reinterpret_cast<unsigned char *>(out.data()), &n,
                     EVP_ripemd160(), nullptr) == 1 &&
              n == 20,
          "RIPEMD160 unavailable");
  return out;
}
QString base58(const QByteArray &input) {
  int zero = 0;
  while (zero < input.size() && input[zero] == 0)
    ++zero;
  QVector<int> b((input.size() - zero) * 138 / 100 + 1);
  int len = 0;
  for (int i = zero; i < input.size(); ++i) {
    int carry = static_cast<unsigned char>(input[i]), j = 0;
    for (auto it = b.rbegin(); (carry || j < len) && it != b.rend();
         ++it, ++j) {
      carry += 256 * (*it);
      *it = carry % 58;
      carry /= 58;
    }
    len = j;
  }
  QString out(zero, '1');
  for (auto it = b.cend() - len; it != b.cend(); ++it)
    out += alphabet[*it];
  return out;
}
QByteArray unbase58(const QString &text) {
  require(!text.isEmpty() && text.size() <= 128, "Invalid base58 length");
  int zero = 0;
  while (zero < text.size() && text[zero] == '1')
    ++zero;
  QVector<int> b((text.size() - zero) * 733 / 1000 + 1);
  int len = 0;
  for (int i = zero; i < text.size(); ++i) {
    int carry = alphabet.indexOf(text[i]);
    require(carry >= 0, "Invalid base58 character");
    int j = 0;
    for (auto it = b.rbegin(); (carry || j < len) && it != b.rend();
         ++it, ++j) {
      carry += 58 * (*it);
      *it = carry % 256;
      carry /= 256;
    }
    require(!carry, "Invalid base58 encoding");
    len = j;
  }
  QByteArray out(zero, 0);
  for (auto it = b.cend() - len; it != b.cend(); ++it)
    out.append(char(*it));
  return out;
}
QString segwitAddress(const QString &hrp, int version,
                      const QByteArray &program) {
  require(version >= 0 && version <= 16 && program.size() >= 2 &&
              program.size() <= 40,
          "Invalid witness program");
  QVector<int> bytes;
  for (unsigned char c : program)
    bytes << c;
  QVector<int> d{version};
  d += convert(bytes, 8, 5, true);
  auto v = expand(hrp) + d;
  v += QVector<int>(6, 0);
  quint32 mod = polymod(v) ^ (version == 0 ? 1 : 0x2bc830a3);
  QString result = hrp + '1';
  for (int x : d)
    result += chars[x];
  for (int i = 0; i < 6; ++i)
    result += chars[(mod >> (5 * (5 - i))) & 31];
  return result;
}
QByteArray addressScript(const QString &address, Chain chain) {
  require(chain != Chain::Solana, "Solana does not use output scripts");
  QString low = address.toLower(), hrp = chain == Chain::Bitcoin ? "bc" : "ltc";
  if (low.startsWith(hrp + '1')) {
    require(address == low || address == address.toUpper(),
            "Mixed-case address");
    require(low.size() <= 90, "Address too long");
    int sep = low.lastIndexOf('1');
    require(low.left(sep) == hrp, "Wrong address network");
    QVector<int> d;
    for (QChar c : low.mid(sep + 1)) {
      int x = chars.indexOf(c);
      require(x >= 0, "Invalid bech32 character");
      d << x;
    }
    require(d.size() >= 7, "Short witness address");
    int ver = d[0];
    require(ver <= 16, "Invalid witness version");
    require(polymod(expand(hrp) + d) == (ver == 0 ? 1 : 0x2bc830a3),
            "Address checksum mismatch");
    d = d.mid(1, d.size() - 7);
    QVector<int> b = convert(d, 5, 8, false);
    require(b.size() >= 2 && b.size() <= 40, "Invalid witness length");
    require(ver != 0 || b.size() == 20 || b.size() == 32,
            "Invalid SegWit v0 length");
    QByteArray script;
    script.append(char(ver ? 0x50 + ver : 0));
    script.append(char(b.size()));
    for (int x : b)
      script.append(char(x));
    return script;
  }
  auto decoded = unbase58(address);
  require(decoded.size() == 25, "Invalid legacy address length");
  require(hash256(decoded.left(21)).left(4) == decoded.right(4),
          "Address checksum mismatch");
  int prefix = static_cast<unsigned char>(decoded[0]);
  auto h = decoded.mid(1, 20);
  if (prefix == (chain == Chain::Bitcoin ? 0 : 48))
    return QByteArray::fromHex("76a914") + h + QByteArray::fromHex("88ac");
  if (prefix == (chain == Chain::Bitcoin ? 5 : 50))
    return QByteArray::fromHex("a914") + h + QByteArray::fromHex("87");
  fail("Address belongs to another network");
}
QByteArray publicKey(const unsigned char *key, Chain chain) {
  if ((chain == Chain::Solana || isSpl(chain))) {
    QByteArray pk(32, 0);
    SecretBuffer secret(64);
    auto *sk = secret.data();
    crypto_sign_seed_keypair(reinterpret_cast<unsigned char *>(pk.data()), sk,
                             key);
    sodium_memzero(sk, 64);
    return pk;
  }
  secp256k1_pubkey pk;
  require(secp256k1_ec_pubkey_create(ctx().value, &pk, key) == 1,
          "Invalid private key");
  QByteArray out(isEvm(chain) ? 65 : 33, 0);
  size_t n = out.size();
  secp256k1_ec_pubkey_serialize(
      ctx().value, reinterpret_cast<unsigned char *>(out.data()), &n, &pk,
      isEvm(chain) ? SECP256K1_EC_UNCOMPRESSED : SECP256K1_EC_COMPRESSED);
  return out;
}
QString address(const unsigned char *key, Chain chain) {
  if (isEvm(chain))
    return evmAddress(key);
  auto pk = publicKey(key, chain);
  return (chain == Chain::Solana || isSpl(chain))
             ? base58(pk)
             : segwitAddress(chain == Chain::Bitcoin ? "bc" : "ltc", 0,
                             hash160(pk));
}
QByteArray signDigest(const unsigned char *key, const QByteArray &hash) {
  require(hash.size() == 32, "Digest must contain 32 bytes");
  secp256k1_ecdsa_signature sig;
  require(secp256k1_ecdsa_sign(
              ctx().value, &sig,
              reinterpret_cast<const unsigned char *>(hash.constData()), key,
              nullptr, nullptr) == 1,
          "ECDSA signing failed");
  QByteArray der(72, 0);
  size_t n = 72;
  secp256k1_ecdsa_signature_serialize_der(
      ctx().value, reinterpret_cast<unsigned char *>(der.data()), &n, &sig);
  der.resize(n);
  return der;
}
QByteArray signEd25519(const unsigned char *seed, const QByteArray &message) {
  unsigned char pk[32];
  SecretBuffer secret(64);
  auto *sk = secret.data();
  crypto_sign_seed_keypair(pk, sk, seed);
  QByteArray sig(64, 0);
  crypto_sign_detached(
      reinterpret_cast<unsigned char *>(sig.data()), nullptr,
      reinterpret_cast<const unsigned char *>(message.constData()),
      message.size(), sk);
  sodium_memzero(sk, 64);
  return sig;
}
Vault::Vault() {
  require(sodium_init() >= 0, "Cryptography initialization failed");
}
Vault::~Vault() { lock(); }
void Vault::lock() { keys_.reset(); }
const unsigned char *Vault::key(Chain chain) const {
  require(keys_ != nullptr, "Unlock the wallet first");
  require(chain != Chain::Monero, "Monero keys are owned by the Monero wallet");
  return keys_->data() + keyIndex(chain) * 32;
}
void Vault::create(const QString &file, QByteArray &password) {
  WipePassword wipe(password);
  require(!QFile::exists(file), "Wallet already exists");
  auto encrypted = createEncrypted(
      {reinterpret_cast<const unsigned char *>(password.constData()),
       std::size_t(password.size())});
  wipe.clear();
  QDir().mkpath(QFileInfo(file).absolutePath());
  QSaveFile f(file);
  if (!f.open(QIODevice::WriteOnly) ||
      !f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner) ||
      f.write(encrypted) != encrypted.size() || !f.commit()) {
    lock();
    fail("Cannot save encrypted wallet");
  }
}
QByteArray Vault::createEncrypted(std::span<const unsigned char> password) {
  struct OnFailure {
    Vault *v;
    bool done = false;
    ~OnFailure() {
      if (!done)
        v->lock();
    }
  } cleanup{this};
  require(password.size() >= 12 && password.size() <= 1024,
          "Use a password of 12–1024 bytes");
  lock();
  QByteArray header("AURUM001", 8);
  header.resize(8 + crypto_pwhash_SALTBYTES +
                crypto_aead_xchacha20poly1305_ietf_NPUBBYTES);
  randombytes_buf(header.data() + 8, header.size() - 8);
  SecretBuffer derivedKey(32);
  auto *k = derivedKey.data();
  int derived = crypto_pwhash(
      k, 32, reinterpret_cast<const char *>(password.data()), password.size(),
      reinterpret_cast<unsigned char *>(header.data() + 8),
      crypto_pwhash_OPSLIMIT_MODERATE, crypto_pwhash_MEMLIMIT_MODERATE,
      crypto_pwhash_ALG_ARGON2ID13);
  require(derived == 0, "Password derivation failed");
  // Generate roots only after the costly password derivation has finished.
  keys_ = std::make_unique<SecretBuffer>(192);
  randombytes_buf(keys_->data(), 96);
  for (int i = 0; i < 2; ++i)
    while (!secp256k1_ec_seckey_verify(ctx().value, keys_->data() + i * 32))
      randombytes_buf(keys_->data() + i * 32, 32);
  QByteArray ciphertext(96 + crypto_aead_xchacha20poly1305_ietf_ABYTES, 0);
  crypto_aead_xchacha20poly1305_ietf_encrypt(
      reinterpret_cast<unsigned char *>(ciphertext.data()), nullptr,
      keys_->data(), 96,
      reinterpret_cast<const unsigned char *>(header.constData()),
      header.size(), nullptr,
      reinterpret_cast<const unsigned char *>(header.constData() + 24), k);
  sodium_memzero(k, 32);
  for (int i = 3; i < 6; ++i) {
    crypto_kdf_derive_from_key(keys_->data() + i * 32, 32, i, "AURUMEVM",
                               keys_->data());
    require(secp256k1_ec_seckey_verify(ctx().value, keys_->data() + i * 32) ==
                1,
            "Derived key invalid");
  }
  keys_->readOnly();
  cleanup.done = true;
  return header + ciphertext;
}
void Vault::unlock(const QString &file, QByteArray &password) {
  WipePassword wipe(password);
  QFile f(file);
  require(f.open(QIODevice::ReadOnly), "Cannot open wallet");
  require(f.size() == 160, "Invalid wallet size");
  unlockEncrypted(f.readAll(), {reinterpret_cast<const unsigned char *>(
                                    password.constData()),
                                std::size_t(password.size())});
}
void Vault::unlockEncrypted(const QByteArray &data,
                            std::span<const unsigned char> password) {
  struct OnFailure {
    Vault *v;
    bool done = false;
    ~OnFailure() {
      if (!done)
        v->lock();
    }
  } cleanup{this};
  lock();
  require(data.size() == 160, "Invalid wallet size");
  require(data.left(8) == "AURUM001", "Unsupported wallet format");
  require(password.size() <= 1024, "Password too long");
  SecretBuffer derivedKey(32);
  auto *k = derivedKey.data();
  int derived = crypto_pwhash(
      k, 32, reinterpret_cast<const char *>(password.data()), password.size(),
      reinterpret_cast<const unsigned char *>(data.constData() + 8),
      crypto_pwhash_OPSLIMIT_MODERATE, crypto_pwhash_MEMLIMIT_MODERATE,
      crypto_pwhash_ALG_ARGON2ID13);
  require(derived == 0, "Password derivation failed");
  keys_ = std::make_unique<SecretBuffer>(192);
  int ok = crypto_aead_xchacha20poly1305_ietf_decrypt(
      keys_->data(), nullptr, nullptr,
      reinterpret_cast<const unsigned char *>(data.constData() + 48), 112,
      reinterpret_cast<const unsigned char *>(data.constData()), 48,
      reinterpret_cast<const unsigned char *>(data.constData() + 24), k);
  sodium_memzero(k, 32);
  if (ok != 0) {
    lock();
    fail("Incorrect password or damaged wallet");
  }
  for (int i = 0; i < 2; ++i)
    if (!secp256k1_ec_seckey_verify(ctx().value, keys_->data() + i * 32)) {
      lock();
      fail("Wallet contains an invalid private key");
    }
  for (int i = 3; i < 6; ++i) {
    crypto_kdf_derive_from_key(keys_->data() + i * 32, 32, i, "AURUMEVM",
                               keys_->data());
    if (!secp256k1_ec_seckey_verify(ctx().value, keys_->data() + i * 32)) {
      lock();
      fail("Derived key invalid");
    }
  }
  keys_->readOnly();
  cleanup.done = true;
}
QByteArray bip143Digest(const QVector<Utxo> &inputs, int index,
                        const QByteArray &scriptCode, const QByteArray &outs,
                        quint32 locktime, quint32 version) {
  require(index >= 0 && index < inputs.size(), "Invalid input index");
  QByteArray prev, seq;
  for (const auto &u : inputs) {
    prev += outpoint(u);
    seq += le(u.sequence, 4);
  }
  return hash256(le(version, 4) + hash256(prev) + hash256(seq) +
                 outpoint(inputs[index]) + varbytes(scriptCode) +
                 le(inputs[index].amount, 8) + le(inputs[index].sequence, 4) +
                 hash256(outs) + le(locktime, 4) + le(1, 4));
}
UtxoPlan planUtxo(const QByteArray &pk, Chain chain,
                  const QVector<Utxo> &available, const QString &destination,
                  quint64 amount, quint64 rate) {
  require(chain != Chain::Solana, "Incorrect signing method");
  require(rate >= 1 && rate <= 10000, "Fee rate must be 1–10000 base units/vB");
  require(amount >= 546, "Amount is below the conservative dust floor");
  require(amount <=
              (chain == Chain::Bitcoin ? 21000000ULL : 84000000ULL) * 100000000,
          "Amount exceeds supply");
  require(pk.size() == 33, "Invalid sender public key");
  QByteArray dest = addressScript(destination, chain), h = hash160(pk),
             changeScript = QByteArray::fromHex("0014") + h;
  QVector<Utxo> sorted = available, inputs;
  std::sort(sorted.begin(), sorted.end(),
            [](const auto &a, const auto &b) { return a.amount > b.amount; });
  quint64 total = 0, fee = 0;
  std::set<QByteArray> seen;
  for (const auto &u : sorted) {
    require(u.amount > 0 && u.amount <= 84000000ULL * 100000000,
            "Invalid UTXO amount");
    require(seen.insert(outpoint(u)).second, "Duplicate UTXO");
    require(total <= std::numeric_limits<quint64>::max() - u.amount,
            "Input overflow");
    inputs << u;
    total += u.amount;
    require(inputs.size() <= 100, "Too many inputs");
    fee = (11 + 68 * inputs.size() + 8 + compact(dest.size()).size() +
           dest.size() + 31) *
          rate;
    if (total >= amount && total - amount >= fee)
      break;
  }
  require(total >= amount && total - amount >= fee,
          "Insufficient confirmed balance including fee");
  quint64 change = total - amount - fee;
  int count = change >= 546 ? 2 : 1;
  QByteArray outs = output(amount, dest);
  if (count == 2)
    outs += output(change, changeScript);
  else {
    fee += change;
    change = 0;
  }
  require(fee <= 10000000, "Fee exceeds 0.1 coin safety cap");
  return {inputs, outs, pk, fee, change, count};
}
SignedTransaction signUtxo(const unsigned char *key, Chain chain,
                           const QVector<Utxo> &available,
                           const QString &destination, quint64 amount,
                           quint64 rate) {
  const auto plan = planUtxo(publicKey(key, chain), chain, available,
                             destination, amount, rate);
  const auto &inputs = plan.inputs;
  const auto &outs = plan.outputs;
  const auto &pk = plan.publicKey;
  auto h = hash160(pk);
  int count = plan.outputCount;
  auto fee = plan.fee;
  auto change = plan.change;
  auto base = serializeBase(inputs, outs, count);
  QByteArray tx =
      le(2, 4) + QByteArray::fromHex("0001") + compact(inputs.size());
  for (const auto &u : inputs)
    tx += outpoint(u) + QByteArray(1, 0) + le(u.sequence, 4);
  tx += compact(count) + outs;
  auto script = QByteArray::fromHex("76a914") + h + QByteArray::fromHex("88ac");
  for (int i = 0; i < inputs.size(); ++i) {
    auto sig = signDigest(key, bip143Digest(inputs, i, script, outs)) +
               QByteArray(1, 1);
    tx += QByteArray(1, 2) + varbytes(sig) + varbytes(pk);
  }
  tx += le(0, 4);
  int vb = (base.size() * 3 + tx.size() + 3) / 4;
  require(fee >= quint64(vb) * rate, "Internal fee estimate was too low");
  return {tx, QString::fromLatin1(reversed(hash256(base)).toHex()), fee, change,
          vb};
}
QByteArray solanaMessage(const QByteArray &sender, const QByteArray &recipient,
                         const QByteArray &blockhash, quint64 lamports) {
  require(sender.size() == 32 && recipient.size() == 32 &&
              blockhash.size() == 32,
          "Invalid Solana address or blockhash");
  require(sender != recipient, "Choose a recipient different from this wallet");
  require(recipient != QByteArray(32, 0), "Cannot send to the system program");
  require(lamports > 0, "Amount must be positive");
  // Legacy message: payer, writable recipient, readonly System Program.
  // Transfer discriminator = 2.
  return QByteArray::fromHex("01000103") + sender + recipient +
         QByteArray(32, 0) + blockhash + QByteArray::fromHex("01020200010c") +
         le(2, 4) + le(lamports, 8);
}
SignedTransaction signSolana(const unsigned char *seed,
                             const QByteArray &message) {
  require(message.size() >= 150 && message.size() <= 1167,
          "Unexpected Solana transfer message");
  auto sig = signEd25519(seed, message);
  return {QByteArray(1, 1) + sig + message, base58(sig), 0, 0, 0};
}
} // namespace aurum
