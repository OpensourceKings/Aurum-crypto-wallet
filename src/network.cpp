#include "network.h"
#include "evm.h"
#include "spl.h"
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QRegularExpression>
#include <cmath>
#include <limits>
#include <memory>
#include <set>

namespace aurum {
static QJsonValue decode(const QByteArray &bytes, QString &error) {
  QJsonParseError pe;
  auto doc = QJsonDocument::fromJson(bytes, &pe);
  if (pe.error != QJsonParseError::NoError) {
    error = "Node returned malformed JSON";
    return {};
  }
  return doc.isArray() ? QJsonValue(doc.array()) : QJsonValue(doc.object());
}
bool Network::validEndpoint(const QString &text, bool ws) {
  QUrl u(text);
  return u.isValid() && !u.host().isEmpty() && u.userInfo().isEmpty() &&
         u.fragment().isEmpty() &&
         (u.scheme() == (ws ? "wss" : "https") ||
          ((u.host() == "localhost" || u.host() == "127.0.0.1" ||
            u.host() == "::1") &&
           u.scheme() == (ws ? "ws" : "http")));
}
Network::Network(QObject *parent) : QObject(parent) {
  ltcElectrum_.setEndpoint(endpoints_.litecoin);
  refreshTimer_.setInterval(30000);
  connect(&refreshTimer_, &QTimer::timeout, this, &Network::refresh);
  pumpTimer_.setInterval(100);
  connect(&pumpTimer_, &QTimer::timeout, this, &Network::pump);
  pumpTimer_.start();
  reconnectTimer_.setSingleShot(true);
  reconnectTimer_.setInterval(30000);
  connect(&reconnectTimer_, &QTimer::timeout, this,
          &Network::connectSubscription);
  connect(&socket_, &QWebSocket::connected, this, [this] {
    if (!enabled_)
      return;
    socket_.sendTextMessage(QString::fromUtf8(
        QJsonDocument(QJsonObject{{"jsonrpc", "2.0"},
                                  {"id", 1},
                                  {"method", "getGenesisHash"}})
            .toJson(QJsonDocument::Compact)));
  });
  auto subscribe = [this] {
    QJsonObject msg{
        {"jsonrpc", "2.0"},
        {"id", 2},
        {"method", "accountSubscribe"},
        {"params", QJsonArray{states_[2].address,
                              QJsonObject{{"encoding", "base64"},
                                          {"commitment", "confirmed"}}}}};
    socket_.sendTextMessage(
        QString::fromUtf8(QJsonDocument(msg).toJson(QJsonDocument::Compact)));
  };
  socket_.setMaxAllowedIncomingMessageSize(1024 * 1024);
  connect(&socket_, &QWebSocket::disconnected, this, [this] {
    subscribed_ = false;
    if (enabled_)
      reconnectTimer_.start();
  });
  connect(&socket_, &QWebSocket::textMessageReceived, this,
          [this, subscribe](const QString &message) {
            QString error;
            auto root = decode(message.toUtf8(), error).toObject();
            if (!enabled_ || !error.isEmpty())
              return;
            if (root.value("id").toInt() == 1) {
              if (root.value("result").toString() ==
                  "5eykt4UsFv8P8NJdTREpY1vzqKqZKvdpKuc147dw2N9d")
                subscribe();
              else
                socket_.close();
              return;
            }
            if (root.value("id").toInt() == 2) {
              subscribed_ = root.value("result").isDouble();
              return;
            }
            if (root.value("method") == "accountNotification") {
              auto val = root.value("params")
                             .toObject()
                             .value("result")
                             .toObject()
                             .value("value")
                             .toObject()
                             .value("lamports");
              qint64 n = val.toInteger(-1);
              if (n >= 0) {
                auto &s = states_[2];
                s.balance = n;
                s.units = QString::number(n);
                s.known = true;
                s.updated = QDateTime::currentDateTimeUtc();
                s.status = "Live · WebSocket";
                emit changed();
              }
            }
          });
}
void Network::setEndpoints(const EndpointSettings &e) {
  if (!validEndpoint(e.bitcoin) ||
      (!validEndpoint(e.litecoin) &&
       !ElectrumClient::validEndpoint(e.litecoin)) ||
      !validEndpoint(e.solana) || !validEndpoint(e.solanaWs, true) ||
      !validEndpoint(e.ethereum) || !validEndpoint(e.polygon) ||
      !validEndpoint(e.bnb))
    throw std::runtime_error(
        "Use HTTPS/WSS, Litecoin Electrum SSL, or loopback endpoints");
  ++generation_;
  ltcElectrum_.cancel();
  endpoints_ = e;
  if (usesElectrum(Chain::Litecoin))
    ltcElectrum_.setEndpoint(e.litecoin);
  solanaVerified_ = false;
  utxoVerified_ = {};
  ++generation_;
  jobs_.clear();
  for (auto &s : states_) {
    s.refreshing = false;
    s.known = false;
    s.status = "Endpoint changed";
  }
  if (enabled_) {
    socket_.abort();
    connectSubscription();
    refresh();
  }
}
void Network::setAddresses(const std::array<QString, AssetCount> &addresses) {
  ++generation_;
  ltcElectrum_.cancel();
  jobs_.clear();
  for (int i = 0; i < AssetCount; ++i) {
    states_[i] = AccountState{};
    states_[i].address = addresses[i];
  }
  enabled_ = true;
  refreshTimer_.start();
  socket_.abort();
  connectSubscription();
  refresh();
}
void Network::setExternalState(Chain chain, const AccountState &state) {
  if (chain != Chain::Monero)
    return;
  states_[static_cast<int>(chain)] = state;
  emit changed();
}
void Network::pause() {
  enabled_ = false;
  ++generation_;
  ltcElectrum_.cancel();
  refreshTimer_.stop();
  reconnectTimer_.stop();
  socket_.abort();
  jobs_.clear();
  for (auto &s : states_)
    s.refreshing = false;
}
QString Network::connectionStatus(Chain chain) const {
  if (!enabled_)
    return "Wallet locked";
  const auto &s = state(chain);
  if (s.refreshing)
    return s.known ? "Refreshing…" : "Connecting…";
  return s.status;
}
QString Network::rest(Chain c) const {
  return c == Chain::Bitcoin ? endpoints_.bitcoin : endpoints_.litecoin;
}
void Network::request(QUrl url, QByteArray body, bool post, Callback cb) {
  jobs_.enqueue({url, body, post, std::move(cb)});
  pump();
}
void Network::pump() {
  if (jobs_.isEmpty() || active_ >= 4)
    return;
  qint64 now = QDateTime::currentMSecsSinceEpoch();
  for (int i = 0, n = jobs_.size(); i < n; ++i) {
    Job j = jobs_.dequeue();
    const QString host = j.url.host();
    if (now < nextRequest_.value(host) || now < cooldown_.value(host)) {
      jobs_.enqueue(j);
      continue;
    }
    nextRequest_[host] = now + 350;
    ++active_;
    QNetworkRequest r(j.url);
    r.setTransferTimeout(15000);
    r.setHeader(QNetworkRequest::UserAgentHeader, "AurumWallet/0.1");
    r.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                   QNetworkRequest::SameOriginRedirectPolicy);
    r.setHeader(QNetworkRequest::ContentTypeHeader,
                j.body.startsWith('{') ? "application/json" : "text/plain");
    auto *reply = j.post ? manager_.post(r, j.body) : manager_.get(r);
    auto buf = std::make_shared<QByteArray>();
    auto tooBig = std::make_shared<bool>(false);
    connect(reply, &QNetworkReply::readyRead, this, [reply, buf, tooBig] {
      buf->append(reply->readAll());
      if (buf->size() > 2 * 1024 * 1024) {
        *tooBig = true;
        reply->abort();
      }
    });
    connect(
        reply, &QNetworkReply::finished, this,
        [this, reply, j, host, buf, tooBig] {
          --active_;
          buf->append(reply->readAll());
          int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute)
                         .toInt();
          QString err;
          if (*tooBig)
            err = "Node response exceeded 2 MiB";
          else if (code == 429) {
            bool ok = false;
            auto raw = reply->rawHeader("Retry-After");
            qint64 delay = raw.toLongLong(&ok);
            if (!ok) {
              auto date = QDateTime::fromString(QString::fromLatin1(raw),
                                                Qt::RFC2822Date);
              delay = date.isValid()
                          ? QDateTime::currentDateTimeUtc().secsTo(date)
                          : 60;
            }
            delay = std::clamp<qint64>(delay, 1, 86400);
            cooldown_[host] =
                QDateTime::currentMSecsSinceEpoch() + delay * 1000;
            err = QString("Node rate limited; retry in %1 seconds").arg(delay);
          } else if (code != 0 && (code < 200 || code >= 300))
            err = QString("Node returned HTTP %1").arg(code);
          else if (reply->error() == QNetworkReply::TimeoutError)
            err = "Connection timed out";
          else if (reply->error() != QNetworkReply::NoError)
            err = reply->errorString();
          else if (code < 200 || code >= 300)
            err = QString("Node returned HTTP %1").arg(code);
          j.callback(*buf, err);
          reply->deleteLater();
          pump();
        });
    return;
  }
}
void Network::jsonGet(const QString &url, JsonCallback cb) {
  request(QUrl(url), {}, false, [cb](QByteArray b, QString e) {
    QJsonValue v;
    if (e.isEmpty())
      v = decode(b, e);
    cb(v, e);
  });
}
void Network::verifyUtxoNetwork(Chain chain, std::function<void(QString)> cb) {
  if (chain != Chain::Bitcoin && chain != Chain::Litecoin) {
    cb("Invalid UTXO network");
    return;
  }
  if (utxoVerified_[int(chain)]) {
    cb({});
    return;
  }
  auto gen = generation_;
  request(QUrl(rest(chain) + "/block-height/0"), {}, false,
          [this, chain, cb, gen](QByteArray bytes, QString e) {
            if (gen != generation_)
              e = "Endpoint changed during verification";
            const QByteArray expected =
                chain == Chain::Bitcoin ? "000000000019d6689c085ae165831e934ff7"
                                          "63ae46a2a6c172b3f1b60a8ce26f"
                                        : "12a765e31ffd4059bada1e25190f6e98c99d"
                                          "9714d334efa41a195a7e7e04bfe2";
            if (e.isEmpty() && bytes.trimmed() != expected)
              e = "Endpoint is not " + networkName(chain) + " mainnet";
            if (e.isEmpty())
              utxoVerified_[int(chain)] = true;
            cb(e);
          });
}
bool Network::usesElectrum(Chain c) const {
  return c == Chain::Litecoin &&
         ElectrumClient::validEndpoint(endpoints_.litecoin);
}
void Network::electrumGet(const QString &path, JsonCallback cb) {
  const auto gen = generation_;
  auto guarded = [this, gen, cb](QJsonValue v, QString e) {
    if (gen != generation_) {
      cb({}, "Wallet or endpoint changed");
      return;
    }
    cb(v, e);
  };
  if (path.startsWith("/tx/")) {
    const auto id = path.mid(4);
    ltcElectrum_.request("blockchain.transaction.get", {id, false},
                         [guarded, id](QJsonValue v, QString e) {
                           QJsonObject tx;
                           try {
                             if (e.isEmpty())
                               tx = verifiedElectrumTransaction(v.toString(),
                                                                id);
                           } catch (const std::exception &ex) {
                             e = ex.what();
                           }
                           guarded(tx, e);
                         });
    return;
  }
  const auto parts = path.split('/', Qt::SkipEmptyParts);
  if (parts.size() < 2 || parts[0] != "address") {
    guarded({}, "Unsupported Electrum request");
    return;
  }
  QString hash;
  try {
    hash = electrumScriptHash(parts[1]);
  } catch (const std::exception &e) {
    guarded({}, e.what());
    return;
  }
  const QString kind = parts.size() > 2 ? parts[2] : "balance";
  const QString method = kind == "balance" ? "get_balance"
                         : kind == "utxo"  ? "listunspent"
                         : kind == "txs"   ? "get_history"
                                           : "";
  if (method.isEmpty()) {
    guarded({}, "Unsupported Electrum request");
    return;
  }
  ltcElectrum_.request(
      "blockchain.scripthash." + method, {hash},
      [guarded, kind](QJsonValue v, QString e) {
        if (!e.isEmpty()) {
          guarded({}, e);
          return;
        }
        if (kind == "balance") {
          auto o = v.toObject();
          auto confirmed = o.value("confirmed").toInteger(-1);
          auto unconfirmed = o.value("unconfirmed")
                                 .toInteger(std::numeric_limits<qint64>::min());
          constexpr qint64 limit = 84000000LL * 100000000;
          if (!v.isObject() || confirmed < 0 || confirmed > limit ||
              unconfirmed < -limit || unconfirmed > limit) {
            guarded({}, "Invalid Electrum balance");
            return;
          }
          guarded(
              QJsonObject{
                  {"chain_stats", QJsonObject{{"funded_txo_sum", confirmed},
                                              {"spent_txo_sum", 0}}},
                  {"mempool_stats",
                   QJsonObject{
                       {"funded_txo_sum", std::max<qint64>(0, unconfirmed)},
                       {"spent_txo_sum", std::max<qint64>(0, -unconfirmed)}}}},
              {});
          return;
        }
        if (!v.isArray()) {
          guarded({}, "Invalid Electrum output list");
          return;
        }
        QJsonArray items;
        for (const auto &val : v.toArray()) {
          const auto o = val.toObject();
          const auto id = o.value("tx_hash").toString();
          const auto height = o.value("height").toInteger(-2);
          if (!QRegularExpression("^[0-9a-fA-F]{64}$").match(id).hasMatch() ||
              height < -1) {
            guarded({}, "Invalid Electrum transaction metadata");
            return;
          }
          if (kind == "utxo") {
            const auto amount = o.value("value").toInteger(-1),
                       index = o.value("tx_pos").toInteger(-1);
            if (amount < 0 || amount > 84000000LL * 100000000 || index < 0 ||
                index > 0xffffffffLL) {
              guarded({}, "Invalid Electrum output");
              return;
            }
            items.append(QJsonObject{
                {"txid", id.toLower()},
                {"vout", index},
                {"value", amount},
                {"status", QJsonObject{{"confirmed", height > 0}}}});
          } else
            items.append(QJsonObject{
                {"txid", id.toLower()},
                {"status", QJsonObject{{"confirmed", height > 0}}}});
        }
        if (kind == "txs") {
          QJsonArray recent;
          for (int i = items.size() - 1; i >= 0 && recent.size() < 20; --i)
            recent.append(items[i]);
          items = recent;
        }
        guarded(items, {});
      });
}

void Network::utxoGet(Chain chain, const QString &path, JsonCallback cb) {
  if (usesElectrum(chain)) {
    electrumGet(path, std::move(cb));
    return;
  }
  verifyUtxoNetwork(chain, [this, chain, path, cb](QString e) {
    if (!e.isEmpty()) {
      cb({}, e);
      return;
    }
    jsonGet(rest(chain) + path, cb);
  });
}
void Network::rpc(const QString &method, const QJsonArray &params,
                  JsonCallback cb) {
  if (method != "getGenesisHash" && !solanaVerified_) {
    auto gen = generation_;
    rpc("getGenesisHash", {},
        [this, gen, method, params, cb](QJsonValue v, QString e) {
          if (gen != generation_)
            e = "Endpoint changed during verification";
          if (e.isEmpty() &&
              v.toString() != "5eykt4UsFv8P8NJdTREpY1vzqKqZKvdpKuc147dw2N9d")
            e = "Endpoint is not Solana mainnet";
          if (!e.isEmpty()) {
            cb({}, e);
            return;
          }
          solanaVerified_ = true;
          rpc(method, params, cb);
        });
    return;
  }
  auto body = QJsonDocument(QJsonObject{{"jsonrpc", "2.0"},
                                        {"id", 1},
                                        {"method", method},
                                        {"params", params}})
                  .toJson(QJsonDocument::Compact);
  request(QUrl(endpoints_.solana), body, true, [cb](QByteArray b, QString e) {
    QJsonValue v;
    if (e.isEmpty()) {
      auto o = decode(b, e).toObject();
      if (o.contains("error"))
        e = o.value("error").toObject().value("message").toString("RPC failed");
      else if (!o.contains("result"))
        e = "Missing RPC result";
      else
        v = o.value("result");
    }
    cb(v, e);
  });
}
void Network::connectSubscription() {
  if (enabled_ && !states_[2].address.isEmpty())
    socket_.open(QUrl(endpoints_.solanaWs));
}
void Network::refresh() {
  if (!enabled_)
    return;
  for (int i = 0; i < AssetCount; ++i)
    refreshAccount(static_cast<Chain>(i));
}
void Network::refreshAccount(Chain chain) {
  if (chain == Chain::Monero)
    return;
  if (isEvm(chain)) {
    auto &s = states_[int(chain)];
    if (s.address.isEmpty() || s.refreshing)
      return;
    s.refreshing = true;
    auto gen = generation_;
    auto done = [this, chain, gen](QJsonValue v, QString error) {
      if (gen != generation_)
        return;
      auto &state = states_[int(chain)];
      state.refreshing = false;
      try {
        if (!error.isEmpty())
          throw std::runtime_error(error.toStdString());
        state.units = quantityDecimal(v.toString());
        state.known = true;
        state.updated = QDateTime::currentDateTimeUtc();
        state.status = "Synced · HTTPS";
      } catch (const std::exception &ex) {
        state.status = ex.what();
      }
      emit changed();
    };
    if (isToken(chain))
      evmRpc(
          chain, "eth_call",
          QJsonArray{QJsonObject{
                         {"to", tokenAddress(chain)},
                         {"data", "0x" + QString::fromLatin1(
                                             erc20Balance(s.address).toHex())}},
                     "latest"},
          done);
    else
      evmRpc(chain, "eth_getBalance", QJsonArray{s.address, "latest"}, done);
    return;
  }
  if (isSpl(chain)) {
    auto &s = states_[int(chain)];
    if (s.address.isEmpty() || s.refreshing)
      return;
    s.refreshing = true;
    auto gen = generation_;
    rpc("getTokenAccountsByOwner",
        QJsonArray{s.address, QJsonObject{{"mint", tokenAddress(chain)}},
                   QJsonObject{{"encoding", "jsonParsed"},
                               {"commitment", "confirmed"}}},
        [this, chain, gen](QJsonValue v, QString e) {
          if (gen != generation_)
            return;
          auto &s = states_[int(chain)];
          s.refreshing = false;
          try {
            if (!e.isEmpty())
              throw std::runtime_error(e.toStdString());
            auto arr = v.toObject().value("value");
            if (!arr.isArray())
              throw std::runtime_error("Invalid SPL response");
            quint64 sum = 0;
            for (const auto &a : arr.toArray()) {
              auto info = a.toObject()
                              .value("account")
                              .toObject()
                              .value("data")
                              .toObject()
                              .value("parsed")
                              .toObject()
                              .value("info")
                              .toObject();
              auto amount = info.value("tokenAmount").toObject();
              if (info.value("mint").toString() != tokenAddress(chain) ||
                  amount.value("decimals").toInt() != 6)
                throw std::runtime_error("Token metadata mismatch");
              quint64 n = parseAmount(amount.value("amount").toString(), 0);
              if (sum > std::numeric_limits<quint64>::max() - n)
                throw std::runtime_error("Token balance overflow");
              sum += n;
            }
            s.balance = sum;
            s.units = QString::number(sum);
            s.known = true;
            s.updated = QDateTime::currentDateTimeUtc();
            s.status = "Synced · SPL";
          } catch (const std::exception &ex) {
            s.status = ex.what();
          }
          emit changed();
        });
    return;
  }
  auto &s = states_[static_cast<int>(chain)];
  if (s.address.isEmpty() || s.refreshing)
    return;
  s.refreshing = true;
  quint64 gen = generation_;
  auto done = [this, chain, gen](QJsonValue v, QString e) {
    if (gen != generation_)
      return;
    auto &s = states_[static_cast<int>(chain)];
    s.refreshing = false;
    if (e.isEmpty()) {
      qint64 balance = -1, pending = 0;
      if (chain == Chain::Solana)
        balance = v.toObject().value("value").toInteger(-1);
      else {
        auto o = v.toObject(), c = o.value("chain_stats").toObject(),
             m = o.value("mempool_stats").toObject();
        auto f = c.value("funded_txo_sum").toInteger(-1),
             spent = c.value("spent_txo_sum").toInteger(-1),
             mf = m.value("funded_txo_sum").toInteger(-1),
             ms = m.value("spent_txo_sum").toInteger(-1);
        if (f >= 0 && spent >= 0 && mf >= 0 && ms >= 0) {
          balance = f - spent;
          pending = mf - ms;
        }
      }
      if (balance < 0)
        e = "Invalid balance response";
      else {
        s.balance = balance;
        s.units = QString::number(balance);
        s.pending = pending;
        s.known = true;
        s.updated = QDateTime::currentDateTimeUtc();
        s.status = chain == Chain::Solana && subscribed_ ? "Live · WebSocket"
                   : usesElectrum(chain) ? "Synced · Electrum TLS"
                                         : "Synced · HTTPS";
      }
    }
    if (!e.isEmpty())
      s.status = e;
    emit changed();
  };
  if (chain == Chain::Solana)
    rpc("getBalance",
        QJsonArray{s.address, QJsonObject{{"commitment", "confirmed"}}}, done);
  else
    utxoGet(chain, "/address/" + s.address, done);
}
void Network::verifiedUtxos(Chain chain,
                            std::function<void(QVector<Utxo>, QString)> cb) {
  const auto expectedAddress = state(chain).address;
  quint64 gen = generation_;
  utxoGet(
      chain, "/address/" + expectedAddress + "/utxo",
      [this, chain, cb, expectedAddress, gen](QJsonValue v, QString e) {
        if (gen != generation_) {
          cb({}, "Wallet changed during request");
          return;
        }
        if (!e.isEmpty() || !v.isArray()) {
          cb({}, e.isEmpty() ? "Invalid UTXO response" : e);
          return;
        }
        auto inputs = std::make_shared<QVector<Utxo>>();
        std::set<QByteArray> seen;
        for (const auto &item : v.toArray()) {
          auto o = item.toObject();
          if (!o.value("status").toObject().value("confirmed").toBool())
            continue;
          QString id = o.value("txid").toString();
          qint64 value = o.value("value").toInteger(-1),
                 index = o.value("vout").toInteger(-1);
          if (!QRegularExpression("^[0-9a-fA-F]{64}$").match(id).hasMatch() ||
              value <= 0 || index < 0 || index > 0xffffffffLL) {
            cb({}, "Invalid UTXO metadata");
            return;
          }
          QByteArray tag = id.toLatin1() + ":" + QByteArray::number(index);
          if (!seen.insert(tag).second) {
            cb({}, "Node returned duplicate inputs");
            return;
          }
          inputs->push_back({QByteArray::fromHex(id.toLatin1()), quint32(index),
                             quint64(value)});
        }
        if (inputs->isEmpty()) {
          cb({}, "No confirmed spendable outputs");
          return;
        }
        if (inputs->size() > 100) {
          cb({}, "Over 100 inputs; consolidation is required");
          return;
        }
        auto left = std::make_shared<int>(inputs->size());
        auto error = std::make_shared<QString>();
        for (const auto &u : *inputs)
          utxoGet(
              chain, "/tx/" + QString::fromLatin1(u.txid.toHex()),
              [this, u, inputs, left, error, cb, expectedAddress, chain,
               gen](QJsonValue tx, QString err) {
                if (gen != generation_)
                  err = "Wallet changed during verification";
                if (err.isEmpty()) {
                  auto o = tx.toObject();
                  auto outs = o.value("vout").toArray();
                  if (o.value("txid").toString() !=
                          QString::fromLatin1(u.txid.toHex()) ||
                      u.index >= quint32(outs.size()))
                    err = "Previous transaction does not match input";
                  else {
                    auto out = outs[u.index].toObject();
                    if (out.value("value").toInteger(-1) != qint64(u.amount) ||
                        out.value("scriptpubkey").toString() !=
                            QString::fromLatin1(
                                addressScript(expectedAddress, chain).toHex()))
                      err = "Input amount or ownership mismatch";
                  }
                }
                if (!err.isEmpty())
                  *error = err;
                if (--*left == 0)
                  cb(*inputs, *error);
              });
      });
}
void Network::latestBlockhash(JsonCallback cb) {
  rpc("getLatestBlockhash",
      QJsonArray{QJsonObject{{"commitment", "confirmed"}}}, cb);
}
void Network::feeForMessage(const QByteArray &message, JsonCallback cb) {
  rpc("getFeeForMessage",
      QJsonArray{QString::fromLatin1(message.toBase64()),
                 QJsonObject{{"commitment", "confirmed"}}},
      cb);
}
void Network::simulate(const SignedTransaction &tx, JsonCallback cb) {
  rpc("simulateTransaction",
      QJsonArray{QString::fromLatin1(tx.wire.toBase64()),
                 QJsonObject{{"encoding", "base64"},
                             {"sigVerify", true},
                             {"commitment", "confirmed"}}},
      [cb](QJsonValue v, QString e) {
        if (e.isEmpty() &&
            !v.toObject().value("value").toObject().value("err").isNull())
          e = "Transaction simulation failed: " +
              QString::fromUtf8(
                  QJsonDocument(v.toObject().value("value").toObject())
                      .toJson(QJsonDocument::Compact));
        cb(v, e);
      });
}
void Network::broadcast(Chain chain, const SignedTransaction &tx,
                        JsonCallback cb) {
  if (usesElectrum(chain)) {
    ltcElectrum_.request(
        "blockchain.transaction.broadcast",
        {QString::fromLatin1(tx.wire.toHex())},
        [tx, cb](QJsonValue v, QString e) {
          if (e.isEmpty() && v.toString() != tx.id)
            e = "Unexpected transaction ID; check explorer before retrying";
          cb(v, e);
        });
    return;
  }
  if (isEvm(chain)) {
    evmRpc(chain, "eth_sendRawTransaction",
           QJsonArray{"0x" + QString::fromLatin1(tx.wire.toHex())},
           [tx, cb](QJsonValue v, QString e) {
             if (e.isEmpty() && v.toString().toLower() != tx.id.toLower())
               e = "Unexpected transaction hash; check explorer";
             cb(v, e);
           });
    return;
  }
  if (chain == Chain::Solana || isSpl(chain)) {
    rpc("sendTransaction",
        QJsonArray{QString::fromLatin1(tx.wire.toBase64()),
                   QJsonObject{{"encoding", "base64"},
                               {"skipPreflight", false},
                               {"preflightCommitment", "confirmed"},
                               {"maxRetries", 3}}},
        [tx, cb](QJsonValue v, QString e) {
          if (e.isEmpty() && v.toString() != tx.id)
            e = "Node returned an unexpected transaction ID; check explorer "
                "before retrying";
          cb(v, e);
        });
  } else
    verifyUtxoNetwork(chain, [this, chain, tx, cb](QString e) {
      if (!e.isEmpty()) {
        cb({}, e);
        return;
      }
      request(QUrl(rest(chain) + "/tx"), tx.wire.toHex(), true,
              [tx, cb](QByteArray b, QString e) {
                QString id = QString::fromLatin1(b).trimmed();
                if (e.isEmpty() && id != tx.id)
                  e = "Node returned an unexpected transaction ID; check "
                      "explorer before retrying";
                cb(id, e);
              });
    });
}
void Network::history(Chain chain, JsonCallback cb) {
  if (isEvm(chain)) {
    cb({}, "Use the address explorer for full account history.");
    return;
  }
  if (baseChain(chain) == Chain::Solana) {
    QString target = state(chain).address;
    if (isSpl(chain)) {
      try {
        target = base58(associatedTokenAddress(unbase58(target),
                                               unbase58(tokenAddress(chain))));
      } catch (const std::exception &e) {
        cb({}, e.what());
        return;
      }
    }
    rpc("getSignaturesForAddress",
        QJsonArray{target,
                   QJsonObject{{"limit", 20}, {"commitment", "confirmed"}}},
        cb);
  } else
    utxoGet(chain, "/address/" + state(chain).address + "/txs", cb);
}
void Network::evmRpc(Chain chain, const QString &method,
                     const QJsonArray &params, JsonCallback callback) {
  QString url = baseChain(chain) == Chain::Ethereum  ? endpoints_.ethereum
                : baseChain(chain) == Chain::Polygon ? endpoints_.polygon
                                                     : endpoints_.bnb;
  auto call = [this, url](const QString &method, const QJsonArray &params,
                          JsonCallback cb) {
    auto body = QJsonDocument(QJsonObject{{"jsonrpc", "2.0"},
                                          {"id", 1},
                                          {"method", method},
                                          {"params", params}})
                    .toJson(QJsonDocument::Compact);
    request(QUrl(url), body, true, [cb](QByteArray b, QString e) {
      QJsonValue result;
      if (e.isEmpty()) {
        auto o = decode(b, e).toObject();
        if (o.contains("error"))
          e = o.value("error").toObject().value("message").toString(
              "RPC error");
        else if (!o.contains("result"))
          e = "Missing RPC result";
        else
          result = o.value("result");
      }
      cb(result, e);
    });
  };
  call("eth_chainId", {},
       [chain, method, params, callback, call](QJsonValue id, QString e) {
         try {
           if (!e.isEmpty())
             throw std::runtime_error(e.toStdString());
           if (quantityDecimal(id.toString()) !=
               QString::number(chainId(chain)))
             throw std::runtime_error(
                 "RPC chain ID does not match selected network");
           call(method, params, callback);
         } catch (const std::exception &ex) {
           callback({}, ex.what());
         }
       });
}
void Network::prepareAccountTransfer(
    Chain chain, const QString &recipient, const QString &units,
    std::function<void(AccountPlan, QString)> callback) {
  if (isEvm(chain)) {
    auto plan = std::make_shared<AccountPlan>();
    plan->evm.chain = chainId(chain);
    plan->evm.to = isToken(chain) ? tokenAddress(chain) : recipient;
    plan->evm.value = isToken(chain) ? "0" : units;
    try {
      evmAddressBytes(recipient);
      if (isToken(chain))
        plan->evm.data = erc20Transfer(recipient, units);
      if (compare256(units, state(chain).units) > 0)
        throw std::runtime_error("Insufficient asset balance");
    } catch (const std::exception &e) {
      callback({}, e.what());
      return;
    }
    evmRpc(
        chain, "eth_getTransactionCount",
        QJsonArray{state(chain).address, "pending"},
        [this, chain, plan, callback](QJsonValue nonce, QString error) {
          try {
            if (!error.isEmpty())
              throw std::runtime_error(error.toStdString());
            plan->evm.nonce = parseAmount(quantityDecimal(nonce.toString()), 0);
          } catch (const std::exception &e) {
            callback({}, e.what());
            return;
          }
          evmRpc(
              chain, "eth_gasPrice", {},
              [this, chain, plan, callback](QJsonValue price, QString error) {
                try {
                  if (!error.isEmpty())
                    throw std::runtime_error(error.toStdString());
                  plan->evm.gasPrice = quantityDecimal(price.toString());
                } catch (const std::exception &e) {
                  callback({}, e.what());
                  return;
                }
                QJsonObject tx{{"from", state(chain).address},
                               {"to", plan->evm.to},
                               {"value", hexQuantity(plan->evm.value)},
                               {"data", "0x" + QString::fromLatin1(
                                                   plan->evm.data.toHex())}};
                evmRpc(
                    chain, "eth_estimateGas", QJsonArray{tx},
                    [this, chain, plan, callback](QJsonValue gas,
                                                  QString error) {
                      try {
                        if (!error.isEmpty())
                          throw std::runtime_error(error.toStdString());
                        auto estimate =
                            parseAmount(quantityDecimal(gas.toString()), 0);
                        if (estimate < 21000 || estimate > 800000)
                          throw std::runtime_error(
                              "Gas estimate outside permitted bounds");
                        plan->evm.gasLimit = estimate == 21000
                                                 ? 21000
                                                 : (estimate * 12 + 9) / 10;
                        auto price = parseAmount(plan->evm.gasPrice, 0);
                        if (price > 1000000000000000000ULL / plan->evm.gasLimit)
                          throw std::runtime_error(
                              "Network fee exceeds safety cap");
                        plan->fee = price * plan->evm.gasLimit;
                        auto required =
                            add256(plan->evm.value, QString::number(plan->fee));
                        if (compare256(required,
                                       state(baseChain(chain)).units) > 0)
                          throw std::runtime_error(
                              "Insufficient native coin for the amount and "
                              "network fee");
                        callback(*plan, {});
                      } catch (const std::exception &e) {
                        callback({}, e.what());
                      }
                    });
              });
        });
    return;
  }
  if (!isSpl(chain)) {
    callback({}, "Unsupported account transfer");
    return;
  }
  QByteArray mint, owner, to;
  quint64 amount = 0;
  try {
    mint = unbase58(tokenAddress(chain));
    owner = unbase58(state(chain).address);
    to = unbase58(recipient);
    if (to.size() != 32)
      throw std::runtime_error("Invalid Solana recipient");
    amount = parseAmount(units, 0);
    if (amount == 0 || amount > state(chain).balance)
      throw std::runtime_error("Insufficient token balance");
  } catch (const std::exception &e) {
    callback({}, e.what());
    return;
  }
  rpc("getTokenAccountsByOwner",
      QJsonArray{
          state(chain).address, QJsonObject{{"mint", tokenAddress(chain)}},
          QJsonObject{{"encoding", "jsonParsed"}, {"commitment", "confirmed"}}},
      [this, chain, mint, owner, to, amount, callback](QJsonValue accounts,
                                                       QString error) {
        QByteArray source;
        try {
          if (!error.isEmpty())
            throw std::runtime_error(error.toStdString());
          for (const auto &a : accounts.toObject().value("value").toArray()) {
            auto o = a.toObject();
            auto acct = o.value("account").toObject();
            if (acct.value("owner").toString() !=
                "TokenkegQfeZyiNwAJbNbGKPFXCWuBvf9Ss623VQ5DA")
              continue;
            auto info = acct.value("data")
                            .toObject()
                            .value("parsed")
                            .toObject()
                            .value("info")
                            .toObject();
            auto balance = info.value("tokenAmount").toObject();
            if (info.value("mint").toString() == tokenAddress(chain) &&
                info.value("owner").toString() == base58(owner) &&
                info.value("state").toString() == "initialized" &&
                balance.value("decimals").toInt() == 6 &&
                parseAmount(balance.value("amount").toString(), 0) >= amount) {
              source = unbase58(o.value("pubkey").toString());
              break;
            }
          }
          if (source.isEmpty())
            throw std::runtime_error("No single initialized token account has "
                                     "enough balance; consolidate first");
        } catch (const std::exception &e) {
          callback({}, e.what());
          return;
        }
        auto plan = std::make_shared<AccountPlan>();
        QByteArray destinationAta;
        try {
          destinationAta = associatedTokenAddress(to, mint);
        } catch (const std::exception &e) {
          callback({}, e.what());
          return;
        }
        rpc("getAccountInfo",
            QJsonArray{base58(destinationAta),
                       QJsonObject{{"encoding", "jsonParsed"},
                                   {"commitment", "confirmed"}}},
            [this, chain, mint, owner, to, source, amount, plan,
             callback](QJsonValue root, QString error) {
              if (!error.isEmpty()) {
                callback({}, error);
                return;
              }
              auto existing = root.toObject().value("value");
              if (!existing.isNull()) {
                auto account = existing.toObject();
                auto info = account.value("data")
                                .toObject()
                                .value("parsed")
                                .toObject()
                                .value("info")
                                .toObject();
                if (account.value("owner").toString() !=
                        "TokenkegQfeZyiNwAJbNbGKPFXCWuBvf9Ss623VQ5DA" ||
                    info.value("owner").toString() != base58(to) ||
                    info.value("mint").toString() != base58(mint)) {
                  callback({}, "Recipient token account metadata mismatch");
                  return;
                }
              }
              auto build = [this, chain, mint, owner, to, source, amount, plan,
                            callback](QJsonValue rent, QString error) {
                try {
                  if (!error.isEmpty())
                    throw std::runtime_error(error.toStdString());
                  qint64 n = rent.toInteger(-1);
                  if (n < 0 || n > 100000000)
                    throw std::runtime_error("Unexpected token-account rent");
                  plan->rent = n;
                } catch (const std::exception &e) {
                  callback({}, e.what());
                  return;
                }
                latestBlockhash([this, mint, owner, to, source, amount, plan,
                                 callback](QJsonValue value, QString error) {
                  try {
                    if (!error.isEmpty())
                      throw std::runtime_error(error.toStdString());
                    auto hash = unbase58(value.toObject()
                                             .value("value")
                                             .toObject()
                                             .value("blockhash")
                                             .toString());
                    plan->message = splTransferMessage(owner, to, source, mint,
                                                       hash, amount);
                  } catch (const std::exception &e) {
                    callback({}, e.what());
                    return;
                  }
                  feeForMessage(plan->message, [this, plan,
                                                callback](QJsonValue fee,
                                                          QString error) {
                    try {
                      if (!error.isEmpty())
                        throw std::runtime_error(error.toStdString());
                      qint64 n = fee.toObject().value("value").toInteger(-1);
                      if (n < 0 || n > 10000000)
                        throw std::runtime_error("Unexpected Solana fee");
                      plan->fee = n;
                      if (plan->fee + plan->rent > state(Chain::Solana).balance)
                        throw std::runtime_error(
                            "Insufficient SOL for fee and token-account rent");
                      callback(*plan, {});
                    } catch (const std::exception &e) {
                      callback({}, e.what());
                    }
                  });
                });
              };
              if (existing.isNull())
                rpc("getMinimumBalanceForRentExemption", QJsonArray{165},
                    build);
              else
                build(QJsonValue(0), {});
            });
      });
}
} // namespace aurum
