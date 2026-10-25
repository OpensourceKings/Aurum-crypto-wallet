#include "electrum.h"
#include "core.h"
#include <QJsonDocument>
#include <QRegularExpression>
#include <algorithm>
#include <limits>
namespace aurum {
namespace {
const QString genesis =
    "12a765e31ffd4059bada1e25190f6e98c99d9714d334efa41a195a7e7e04bfe2";
constexpr qsizetype maxResponse = 2 * 1024 * 1024;
struct Reader {
  const QByteArray &data;
  qsizetype pos = 0;
  QByteArray take(quint64 n) {
    if (n > quint64(data.size() - pos))
      throw std::runtime_error("Truncated previous transaction");
    auto result = data.mid(pos, qsizetype(n));
    pos += qsizetype(n);
    return result;
  }
  quint64 number(int bytes) {
    auto b = take(bytes);
    quint64 v = 0;
    for (int i = 0; i < bytes; ++i)
      v |= quint64(static_cast<unsigned char>(b[i])) << (8 * i);
    return v;
  }
  quint64 compact() {
    auto n = number(1);
    if (n < 253)
      return n;
    const int size = n == 253 ? 2 : n == 254 ? 4 : 8;
    auto v = number(size);
    if (v < (size == 2 ? 253ULL : size == 4 ? 65536ULL : 4294967296ULL))
      throw std::runtime_error("Noncanonical transaction length");
    return v;
  }
};
} // namespace
bool ElectrumClient::validEndpoint(const QString &s) {
  QUrl u(s);
  const bool local =
      u.host() == "localhost" || u.host() == "127.0.0.1" || u.host() == "::1";
  return u.isValid() && !u.host().isEmpty() && u.port() > 0 &&
         u.port() <= 65535 && u.userInfo().isEmpty() && u.path().isEmpty() &&
         !u.hasQuery() && !u.hasFragment() &&
         (u.scheme() == "ssl" || (local && u.scheme() == "tcp"));
}
ElectrumClient::ElectrumClient(QObject *parent) : QObject(parent) {
  timeout_.setSingleShot(true);
  timeout_.setInterval(12000);
  pace_.setSingleShot(true);
  pace_.setInterval(150);
  connect(&timeout_, &QTimer::timeout, this,
          [this] { fail("Electrum connection timed out"); });
  connect(&pace_, &QTimer::timeout, this, [this] { pump(); });
  auto ready = [this] {
    checking_ = 1;
    send("server.version", {"Aurum/0.3", "1.4"});
  };
  connect(&socket_, &QSslSocket::encrypted, this, ready);
  connect(&socket_, &QSslSocket::connected, this, [this, ready] {
    if (endpoint_.scheme() == "tcp")
      ready();
  });
  connect(&socket_, &QSslSocket::readyRead, this, [this] { receive(); });
  connect(&socket_, &QSslSocket::sslErrors, this,
          [this](const QList<QSslError> &) {
            fail("Electrum certificate verification failed");
          });
  connect(&socket_, &QSslSocket::errorOccurred, this,
          [this](QAbstractSocket::SocketError) {
            fail("Electrum connection failed");
          });
  connect(&socket_, &QSslSocket::disconnected, this, [this] {
    verified_ = false;
    if (!jobs_.isEmpty())
      fail("Electrum disconnected; check transaction status before retrying a "
           "send");
  });
}
void ElectrumClient::setEndpoint(const QString &s) {
  if (!validEndpoint(s))
    throw std::runtime_error(
        "Use ssl://host:port for Electrum, or loopback tcp://host:port");
  cancel();
  endpoint_ = QUrl(s);
}
void ElectrumClient::cancel() { fail("Electrum request cancelled"); }
void ElectrumClient::fail(const QString &error) {
  if (failing_)
    return;
  failing_ = true;
  timeout_.stop();
  pace_.stop();
  verified_ = false;
  checking_ = 0;
  currentId_ = 0;
  buffer_.clear();
  auto pending = std::move(jobs_);
  jobs_.clear();
  socket_.abort();
  // Deliver after transport state is cleared; callbacks may enqueue fresh work.
  failing_ = false;
  while (!pending.isEmpty()) {
    auto j = pending.dequeue();
    j.callback({}, error);
  }
}
void ElectrumClient::request(const QString &method, const QJsonArray &params,
                             Callback cb) {
  if (jobs_.size() >= 128) {
    cb({}, "Electrum queue is full");
    return;
  }
  jobs_.enqueue({method, params, std::move(cb)});
  pump();
}
void ElectrumClient::pump() {
  if (jobs_.isEmpty() || currentId_ || pace_.isActive())
    return;
  if (!verified_) {
    if (socket_.state() != QAbstractSocket::UnconnectedState)
      return;
    if (endpoint_.isEmpty()) {
      fail("Electrum endpoint is not configured");
      return;
    }
    timeout_.start();
    if (endpoint_.scheme() == "ssl")
      socket_.connectToHostEncrypted(endpoint_.host(),
                                     quint16(endpoint_.port()));
    else
      socket_.connectToHost(endpoint_.host(), quint16(endpoint_.port()));
    return;
  }
  send(jobs_.head().method, jobs_.head().params);
}
void ElectrumClient::send(const QString &method, const QJsonArray &params) {
  currentId_ = ++nextId_;
  auto message = QJsonDocument(QJsonObject{{"jsonrpc", "2.0"},
                                           {"id", qint64(currentId_)},
                                           {"method", method},
                                           {"params", params}})
                     .toJson(QJsonDocument::Compact) +
                 '\n';
  timeout_.start();
  socket_.write(message);
}
void ElectrumClient::receive() {
  buffer_ += socket_.readAll();
  if (buffer_.size() > maxResponse) {
    fail("Electrum response exceeded 2 MiB");
    return;
  }
  while (buffer_.contains('\n')) {
    const auto end = buffer_.indexOf('\n');
    auto line = buffer_.left(end);
    buffer_.remove(0, end + 1);
    QJsonParseError error;
    auto doc = QJsonDocument::fromJson(line, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
      fail("Malformed Electrum response");
      return;
    }
    auto obj = doc.object();
    // Server notifications cannot fulfill a request.
    if (!obj.contains("id"))
      continue;
    if (!currentId_ || obj.value("id").toInteger(-1) != qint64(currentId_)) {
      fail("Unexpected Electrum response ID");
      return;
    }
    timeout_.stop();
    currentId_ = 0;
    if (checking_ == 1) {
      const auto result = obj.value("result").toArray();
      if (result.size() != 2 || result[1].toString() != "1.4") {
        fail("Electrum protocol negotiation failed");
        return;
      }
      checking_ = 2;
      send("server.features", {});
      continue;
    }
    if (checking_ == 2) {
      const auto f = obj.value("result").toObject();
      if (f.value("genesis_hash").toString() != genesis ||
          f.value("hash_function").toString() != "sha256") {
        fail("Electrum server is not Litecoin mainnet");
        return;
      }
      checking_ = false;
      verified_ = true;
      pace_.start();
      continue;
    }
    if (jobs_.isEmpty()) {
      fail("Unexpected Electrum response");
      return;
    }
    auto job = jobs_.dequeue();
    QString err;
    if (!obj.value("error").isNull() && !obj.value("error").isUndefined())
      err = "Electrum server rejected the request";
    else if (!obj.contains("result"))
      err = "Missing Electrum result";
    pace_.start();
    job.callback(obj.value("result"), err);
  }
}
QString electrumScriptHash(const QString &address) {
  auto hash = sha256(addressScript(address, Chain::Litecoin));
  std::reverse(hash.begin(), hash.end());
  return QString::fromLatin1(hash.toHex());
}
QJsonObject verifiedElectrumTransaction(const QString &rawHex,
                                        const QString &expectedId) {
  static const QRegularExpression hex("^[0-9a-fA-F]+$");
  if (rawHex.size() > 2 * maxResponse || rawHex.size() % 2 ||
      !hex.match(rawHex).hasMatch())
    throw std::runtime_error("Invalid raw transaction");
  const auto data = QByteArray::fromHex(rawHex.toLatin1());
  Reader r{data};
  QByteArray stripped = r.take(4);
  bool witness = false;
  if (r.pos < data.size() && data[r.pos] == 0) {
    r.take(1);
    if (r.number(1) != 1)
      throw std::runtime_error("Invalid witness flags");
    witness = true;
  }
  const auto startInputs = r.pos;
  auto inputs = r.compact();
  if (!inputs || inputs > 100000)
    throw std::runtime_error("Invalid input count");
  for (quint64 i = 0; i < inputs; ++i) {
    r.take(36);
    r.take(r.compact());
    r.take(4);
  }
  const auto outputs = r.compact();
  if (!outputs || outputs > 100000)
    throw std::runtime_error("Invalid output count");
  QJsonArray out;
  quint64 total = 0;
  for (quint64 i = 0; i < outputs; ++i) {
    auto value = r.number(8);
    if (value > 84000000ULL * 100000000 ||
        total > 84000000ULL * 100000000 - value)
      throw std::runtime_error("Invalid Litecoin output value");
    total += value;
    auto script = r.take(r.compact());
    out.append(
        QJsonObject{{"value", qint64(value)},
                    {"scriptpubkey", QString::fromLatin1(script.toHex())}});
  }
  stripped += data.mid(startInputs, r.pos - startInputs);
  if (witness)
    for (quint64 i = 0; i < inputs; ++i) {
      auto count = r.compact();
      if (count > 100000)
        throw std::runtime_error("Invalid witness count");
      for (quint64 j = 0; j < count; ++j)
        r.take(r.compact());
    }
  stripped += r.take(4);
  if (r.pos != data.size())
    throw std::runtime_error("Trailing transaction data");
  auto digest = hash256(stripped);
  std::reverse(digest.begin(), digest.end());
  const auto id = QString::fromLatin1(digest.toHex());
  if (id != expectedId.toLower())
    throw std::runtime_error("Previous transaction hash mismatch");
  return {{"txid", id}, {"vout", out}};
}
} // namespace aurum
