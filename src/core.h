#pragma once
#include "assets.h"
#include "security.h"
#include <QByteArray>
#include <QString>
#include <QVector>
#include <memory>
#include <span>
#include <stdexcept>

namespace aurum {
quint64 parseAmount(const QString &text, int precision);
QString formatAmount(quint64 amount, int precision);
QByteArray sha256(const QByteArray &input);
QByteArray hash256(const QByteArray &input);
QByteArray hash160(const QByteArray &input);
QString base58(const QByteArray &input);
QByteArray unbase58(const QString &text);
QString segwitAddress(const QString &hrp, int version,
                      const QByteArray &program);
QByteArray addressScript(const QString &address, Chain chain);
QByteArray publicKey(const unsigned char *key, Chain chain);
QString address(const unsigned char *key, Chain chain);
QByteArray signDigest(const unsigned char *key, const QByteArray &hash);
QByteArray signEd25519(const unsigned char *seed, const QByteArray &message);

// Keys live in libsodium's guarded, locked allocation; never implicitly copied.
class Vault {
public:
  Vault();
  ~Vault();
  Vault(const Vault &) = delete;
  Vault &operator=(const Vault &) = delete;
  void create(const QString &file, QByteArray &password);
  void unlock(const QString &file, QByteArray &password);
  QByteArray createEncrypted(std::span<const unsigned char> password);
  void unlockEncrypted(const QByteArray &data,
                       std::span<const unsigned char> password);
  void lock();
  bool unlocked() const { return bool(keys_); }
  const unsigned char *key(Chain chain) const;

private:
  std::unique_ptr<SecretBuffer> keys_;
};
struct Utxo {
  QByteArray txid;
  quint32 index;
  quint64 amount;
  quint32 sequence = 0xfffffffd;
};
struct SignedTransaction {
  QByteArray wire;
  QString id;
  quint64 fee = 0;
  quint64 change = 0;
  int virtualBytes = 0;
};
QByteArray bip143Digest(const QVector<Utxo> &inputs, int index,
                        const QByteArray &scriptCode,
                        const QByteArray &serializedOutputs,
                        quint32 locktime = 0, quint32 version = 2);
struct UtxoPlan {
  QVector<Utxo> inputs;
  QByteArray outputs, publicKey;
  quint64 fee, change;
  int outputCount;
};
UtxoPlan planUtxo(const QByteArray &publicKey, Chain chain,
                  const QVector<Utxo> &available, const QString &destination,
                  quint64 amount, quint64 feeRate);
SignedTransaction signUtxo(const unsigned char *key, Chain chain,
                           const QVector<Utxo> &available,
                           const QString &destination, quint64 amount,
                           quint64 feeRate);
QByteArray solanaMessage(const QByteArray &sender, const QByteArray &recipient,
                         const QByteArray &blockhash, quint64 lamports);
SignedTransaction signSolana(const unsigned char *seed,
                             const QByteArray &message);
} // namespace aurum
