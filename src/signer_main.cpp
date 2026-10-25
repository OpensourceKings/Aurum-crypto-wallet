#include "core.h"
#include "evm.h"
#include "spl.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtEndian>
#include <cerrno>
#include <climits>
#include <csignal>
#include <sodium.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <process.h>
#include <windows.h>
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#else
#include <sys/syscall.h>
#include <unistd.h>
#endif

using namespace aurum;
namespace {
void require(bool ok, const char *error) {
  if (!ok)
    throw std::runtime_error(error);
}
void readExact(void *data, size_t size) {
  auto *p = static_cast<char *>(data);
  while (size) {
    auto n = ::read(STDIN_FILENO, p, unsigned(size));
    if (n < 0 && errno == EINTR)
      continue;
    require(n > 0, "Truncated signing request");
    p += n;
    size -= n;
  }
}
void reply(const QJsonObject &object) {
  auto data = QJsonDocument(object).toJson(QJsonDocument::Compact);
  const char *p = data.constData();
  size_t left = data.size();
  while (left) {
    auto n = ::write(STDOUT_FILENO, p, unsigned(left));
    if (n < 0 && errno == EINTR)
      continue;
    if (n <= 0)
      _exit(2);
    p += n;
    left -= n;
  }
}
quint64 number(const QJsonObject &o, const char *field) {
  bool ok = false;
  auto n = o[field].toString().toULongLong(&ok);
  require(ok, "Invalid integer in signing request");
  return n;
}
QJsonObject perform(const QJsonObject &r, std::span<unsigned char> password) {
  Vault vault;
  QByteArray encrypted;
  auto op = r["op"].toString();
  if (op == "create")
    encrypted = vault.createEncrypted(password);
  else
    vault.unlockEncrypted(
        QByteArray::fromBase64(r["encrypted"].toString().toLatin1()), password);
  // The caller owns this locked allocation; do not retain the password while
  // signing.
  sodium_memzero(password.data(), password.size());
  QJsonObject out{{"ok", true}};
  if (op == "create" || op == "inspect") {
    QJsonArray keys, addresses;
    for (int i = 0; i < AssetCount; ++i) {
      auto c = static_cast<Chain>(i);
      // Monero keys are generated and encrypted by monero-wallet-rpc. They
      // are deliberately outside Aurum's six-chain key vault.
      if (c == Chain::Monero) {
        keys.append(QString());
        addresses.append(QString());
      } else {
        keys.append(QString::fromLatin1(publicKey(vault.key(c), c).toHex()));
        addresses.append(address(vault.key(c), c));
      }
    }
    out["publicKeys"] = keys;
    out["addresses"] = addresses;
    if (op == "create")
      out["encrypted"] = QString::fromLatin1(encrypted.toBase64());
  } else {
    require(r["chain"].isDouble(), "Missing asset");
    auto id = r["chain"].toInt(-1);
    require(id >= 0 && id < AssetCount, "Invalid asset");
    auto c = static_cast<Chain>(id);
    auto pk = publicKey(vault.key(c), c);
    require(pk == QByteArray::fromHex(r["expected"].toString().toLatin1()),
            "Wallet changed during preparation");
    SignedTransaction tx;
    if (op == "utxo") {
      require(c == Chain::Bitcoin || c == Chain::Litecoin,
              "Invalid UTXO network");
      auto list = r["inputs"].toArray();
      require(!list.empty() && list.size() <= 100, "Invalid input count");
      QVector<Utxo> inputs;
      for (const auto &entry : list) {
        require(entry.isObject(), "Invalid input");
        auto o = entry.toObject();
        auto index = number(o, "index"), sequence = number(o, "sequence");
        require(index <= UINT_MAX && sequence <= UINT_MAX, "Input overflow");
        inputs.append({QByteArray::fromHex(o["txid"].toString().toLatin1()),
                       quint32(index), number(o, "amount"), quint32(sequence)});
      }
      tx = signUtxo(vault.key(c), c, inputs, r["to"].toString(),
                    number(r, "amount"), number(r, "rate"));
    } else if (op == "evm") {
      require(isEvm(c), "Invalid EVM network");
      EvmTransfer t;
      t.chain = number(r, "chainId");
      t.nonce = number(r, "nonce");
      t.gasLimit = number(r, "gasLimit");
      t.gasPrice = r["gasPrice"].toString();
      t.value = r["value"].toString();
      t.to = r["to"].toString();
      t.data = QByteArray::fromHex(r["data"].toString().toLatin1());
      require(t.chain == chainId(c), "Wrong EVM chain ID");
      if (isToken(c)) {
        require(evmAddressBytes(t.to) == evmAddressBytes(tokenAddress(c)) &&
                    t.value == "0" && t.data.size() == 68 &&
                    t.data.left(4) == QByteArray::fromHex("a9059cbb") &&
                    t.data.mid(4, 12) == QByteArray(12, 0) &&
                    t.data.mid(36) != QByteArray(32, 0),
                "Only this token's transfer is permitted");
      } else
        require(t.data.isEmpty(), "Only native transfers are permitted");
      tx = signEvm(vault.key(c), t);
    } else if (op == "solana") {
      require(c == Chain::Solana || isSpl(c), "Invalid Solana network");
      auto m = QByteArray::fromHex(r["message"].toString().toLatin1());
      QByteArray rebuilt;
      if (c == Chain::Solana) {
        require(m.size() == 150, "Invalid SOL message");
        auto amount = qFromLittleEndian<quint64>(
            reinterpret_cast<const uchar *>(m.constData() + 142));
        rebuilt = solanaMessage(pk, m.mid(36, 32), m.mid(100, 32), amount);
      } else {
        require(m.size() == 320, "Invalid token message");
        auto amount = qFromLittleEndian<quint64>(
            reinterpret_cast<const uchar *>(m.constData() + 311));
        rebuilt = splTransferMessage(pk, m.mid(100, 32), m.mid(36, 32),
                                     unbase58(tokenAddress(c)), m.mid(260, 32),
                                     amount, 6);
      }
      require(rebuilt == m, "Only the reviewed transfer format is permitted");
      tx = signSolana(vault.key(c), m);
    } else
      throw std::runtime_error("Unsupported signing operation");
    out["wire"] = QString::fromLatin1(tx.wire.toHex());
    out["id"] = tx.id;
    out["fee"] = QString::number(tx.fee);
    out["change"] = QString::number(tx.change);
    out["virtualBytes"] = tx.virtualBytes;
  }
  vault.lock();
  return out;
}
} // namespace
int main() {
  try {
    protectProcess();
#ifdef _WIN32
    _setmode(STDIN_FILENO, _O_BINARY);
    _setmode(STDOUT_FILENO, _O_BINARY);
    // Watchdog: a hung signer must not outlive its request.
    CreateThread(
        nullptr, 0,
        [](LPVOID) -> DWORD {
          Sleep(30000);
          ExitProcess(2);
        },
        nullptr, 0, nullptr);
#else
    alarm(30);
    // No inherited descriptors or long-lived request server.
    require(syscall(SYS_close_range, 3U, ~0U, 0U) == 0,
            "Cannot close inherited descriptors");
#endif
    require(sodium_init() >= 0, "Cannot initialize cryptography");
    // Load lazy crypto/provider state before revoking filesystem access.
    unsigned char fixture[32] = {};
    fixture[31] = 1;
    (void)publicKey(fixture, Chain::Bitcoin);
    (void)hash160(QByteArray("provider initialization"));
    (void)parseAmount("1", 8);
    (void)uint256("1");
    quint32 header[2];
    readExact(header, sizeof header);
    auto metaSize = qFromBigEndian(header[0]),
         passwordSize = qFromBigEndian(header[1]);
    require(metaSize > 0 && metaSize <= 65536 && passwordSize > 0 &&
                passwordSize <= 1024,
            "Invalid request size");
    QByteArray metadata(metaSize, 0);
    readExact(metadata.data(), metaSize);
    QJsonParseError error;
    auto doc = QJsonDocument::fromJson(metadata, &error);
    require(error.error == QJsonParseError::NoError && doc.isObject(),
            "Invalid request metadata");
    sandboxSigner();
    QJsonObject result;
    {
      SecretBuffer password(passwordSize);
      readExact(password.data(), passwordSize);
      result = perform(doc.object(), {password.data(), passwordSize});
    }
    reply(result);
    // Exit the process so stack/register copies held inside dependencies do not
    // survive in a reusable worker, even after successful buffer cleanup.
    _exit(0);
  } catch (const std::exception &e) {
    reply({{"ok", false}, {"error", QString::fromUtf8(e.what())}});
    _exit(0);
  }
}
