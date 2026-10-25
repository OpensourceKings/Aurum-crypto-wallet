#include "core.h"
#include "evm.h"
#include "spl.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <sodium.h>
using namespace aurum;
int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  if (sodium_init() < 0)
    return 1;
  auto a = app.arguments();
  unsigned char key[32] = {};
  key[31] = 1;
  auto mode = a.value(1);
  try {
    QJsonObject result;
    if (mode == "addresses") {
      for (int i = 0; i < AssetCount; ++i) {
        if (static_cast<Chain>(i) != Chain::Monero)
          result[QString::number(i)] = address(key, static_cast<Chain>(i));
      }
      result["bcrt"] =
          segwitAddress("bcrt", 0, hash160(publicKey(key, Chain::Bitcoin)));
      result["rltc"] =
          segwitAddress("rltc", 0, hash160(publicKey(key, Chain::Litecoin)));
    } else if (mode == "evm") {
      EvmTransfer t;
      t.chain = a[2].toULongLong();
      t.nonce = a[3].toULongLong();
      t.gasPrice = a[4];
      t.gasLimit = a[5].toULongLong();
      t.to = a[6];
      t.value = a[7];
      t.data = QByteArray::fromHex(a.value(8).toLatin1());
      auto tx = signEvm(key, t);
      result = {{"raw", "0x" + QString::fromLatin1(tx.wire.toHex())},
                {"id", tx.id},
                {"fee", QString::number(tx.fee)}};
    } else if (mode == "utxo") {
      auto c = a[2] == "BTC" ? Chain::Bitcoin : Chain::Litecoin;
      QVector<Utxo> in{{QByteArray::fromHex(a[3].toLatin1()), a[4].toUInt(),
                        a[5].toULongLong()}};
      auto tx =
          signUtxo(key, c, in, a[6], a[7].toULongLong(), a[8].toULongLong());
      result = {{"raw", QString::fromLatin1(tx.wire.toHex())},
                {"id", tx.id},
                {"fee", QString::number(tx.fee)}};
    } else if (mode == "spl") {
      auto msg = splTransferMessage(
          publicKey(key, Chain::Solana), unbase58(a[2]), unbase58(a[3]),
          unbase58(a[4]), unbase58(a[5]), a[6].toULongLong());
      auto signedTx = signSolana(key, msg);
      result = {{"raw", QString::fromLatin1(signedTx.wire.toBase64())},
                {"message", QString::fromLatin1(msg.toBase64())},
                {"ata", base58(associatedTokenAddress(unbase58(a[2]),
                                                      unbase58(a[4])))}};
    } else if (mode == "sol") {
      auto msg = solanaMessage(publicKey(key, Chain::Solana), unbase58(a[2]),
                               unbase58(a[3]), a[4].toULongLong());
      auto tx = signSolana(key, msg);
      result = {{"raw", QString::fromLatin1(tx.wire.toBase64())},
                {"id", tx.id}};
    } else
      throw std::runtime_error("Unknown probe");
    QTextStream(stdout) << QJsonDocument(result).toJson(QJsonDocument::Compact)
                        << '\n';
  } catch (const std::exception &e) {
    QTextStream(stderr) << e.what() << '\n';
    return 1;
  }
}
