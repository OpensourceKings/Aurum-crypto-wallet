#include "signer.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QtEndian>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sodium.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

namespace aurum {
namespace {
void require(bool ok, const char *error) {
  if (!ok)
    throw std::runtime_error(error);
}
struct Child {
  int fd = -1;
  pid_t pid = -1;
  ~Child() {
    if (fd >= 0)
      close(fd);
    if (pid > 0) {
      kill(pid, SIGKILL);
      while (waitpid(pid, nullptr, 0) < 0 && errno == EINTR) {
      }
    }
  }
};
QJsonObject call(QJsonObject request, QByteArray &password) {
  WipePassword wipe(password);
  require(password.size() > 0 && password.size() <= 1024,
          "Invalid password length");
  SecretBuffer protectedPassword(password.size());
  const auto passwordSize = password.size();
  std::memcpy(protectedPassword.data(), password.constData(), passwordSize);
  wipe.clear();
  auto metadata = QJsonDocument(request).toJson(QJsonDocument::Compact);
  require(metadata.size() <= 65536, "Signing request is too large");
  auto path = QFile::encodeName(QCoreApplication::applicationDirPath() +
                                "/aurum-signer");
  require(QFileInfo(QString::fromLocal8Bit(path)).isExecutable(),
          "Signing helper is missing");
  int pair[2];
  require(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, pair) == 0,
          "Cannot create private signing channel");
  Child child;
  child.fd = pair[0];
  posix_spawn_file_actions_t actions;
  if (posix_spawn_file_actions_init(&actions) != 0) {
    close(pair[1]);
    throw std::runtime_error("Cannot initialize signer launch");
  }
  int setup = posix_spawn_file_actions_adddup2(&actions, pair[1], STDIN_FILENO);
  setup |= posix_spawn_file_actions_adddup2(&actions, pair[1], STDOUT_FILENO);
  setup |= posix_spawn_file_actions_addopen(&actions, STDERR_FILENO,
                                            "/dev/null", O_WRONLY, 0);
  setup |= posix_spawn_file_actions_addclose(&actions, pair[0]);
  setup |= posix_spawn_file_actions_addclose(&actions, pair[1]);
  char *argv[] = {path.data(), nullptr};
  // Explicit environment: no inherited loader hooks, plugin paths or secrets.
  char locale[] = "LANG=C.UTF-8", timezone[] = "TZ=UTC";
  char asan[] = "ASAN_OPTIONS=detect_leaks=0",
       ubsan[] = "UBSAN_OPTIONS=halt_on_error=1";
  char *env[] = {locale, timezone, asan, ubsan, nullptr};
  auto result = setup ? setup
                      : posix_spawn(&child.pid, path.constData(), &actions,
                                    nullptr, argv, env);
  posix_spawn_file_actions_destroy(&actions);
  close(pair[1]);
  require(result == 0, "Cannot launch isolated signer");
  require(fcntl(child.fd, F_SETFL, O_NONBLOCK) == 0,
          "Cannot configure signing channel");
  QElapsedTimer deadline;
  deadline.start();
  auto ready = [&](short events) {
    auto remaining = 30000 - deadline.elapsed();
    require(remaining > 0, "Signing helper timed out");
    pollfd p{child.fd, events, 0};
    int n;
    do {
      n = poll(&p, 1, int(remaining));
    } while (n < 0 && errno == EINTR);
    require(n > 0 && !(p.revents & POLLNVAL), "Signing channel unavailable");
  };
  auto sendBytes = [&](const void *data, size_t size) {
    auto *p = static_cast<const char *>(data);
    while (size) {
      ready(POLLOUT);
      auto n = send(child.fd, p, size, MSG_NOSIGNAL);
      if (n < 0 && (errno == EINTR || errno == EAGAIN))
        continue;
      require(n > 0, "Signing helper closed the channel");
      p += n;
      size -= n;
    }
  };
  quint32 header[2] = {qToBigEndian(quint32(metadata.size())),
                       qToBigEndian(quint32(passwordSize))};
  sendBytes(header, sizeof header);
  sendBytes(metadata.constData(), metadata.size());
  sendBytes(protectedPassword.data(), passwordSize);
  // Erase our only controlled plaintext copy immediately after the handoff.
  sodium_memzero(protectedPassword.data(), passwordSize);
  shutdown(child.fd, SHUT_WR);
  QByteArray response;
  char buffer[4096];
  for (;;) {
    ready(POLLIN);
    auto n = recv(child.fd, buffer, sizeof buffer, 0);
    if (n < 0 && (errno == EINTR || errno == EAGAIN))
      continue;
    require(n >= 0, "Cannot receive signing result");
    if (n == 0)
      break;
    response.append(buffer, n);
    require(response.size() <= 131072, "Invalid signing response length");
  }
  int status = 0;
  pid_t exited;
  do {
    exited = waitpid(child.pid, &status, 0);
  } while (exited < 0 && errno == EINTR);
  require(exited == child.pid, "Cannot collect signing helper");
  child.pid = -1;
  require(WIFEXITED(status) && WEXITSTATUS(status) == 0,
          "Signing helper failed; no transaction was submitted");
  QJsonParseError parse;
  auto doc = QJsonDocument::fromJson(response, &parse);
  require(parse.error == QJsonParseError::NoError && doc.isObject(),
          "Invalid signing response");
  auto out = doc.object();
  if (!out.value("ok").toBool())
    throw std::runtime_error(
        out.value("error").toString("Signing failed").toStdString());
  return out;
}
QJsonObject requestFor(const QString &file, const char *op) {
  QFile f(file);
  require(f.open(QIODevice::ReadOnly) && f.size() == 160,
          "Invalid encrypted wallet file");
  return {{"op", op},
          {"encrypted", QString::fromLatin1(f.readAll().toBase64())}};
}
void identify(QJsonObject &r, Chain c, const QByteArray &expected) {
  r["chain"] = int(c);
  r["expected"] = QString::fromLatin1(expected.toHex());
}
SignedTransaction transaction(const QJsonObject &r) {
  SignedTransaction tx;
  tx.wire = QByteArray::fromHex(r["wire"].toString().toLatin1());
  tx.id = r["id"].toString();
  bool a, b;
  tx.fee = r["fee"].toString().toULongLong(&a);
  tx.change = r["change"].toString().toULongLong(&b);
  tx.virtualBytes = r["virtualBytes"].toInt();
  require(a && b && !tx.wire.isEmpty() && !tx.id.isEmpty(),
          "Incomplete signing response");
  return tx;
}
} // namespace
PublicWallet openIsolated(const QString &file, QByteArray &password,
                          bool create) {
  WipePassword wipe(password);
  require(!create || !QFile::exists(file), "Wallet already exists");
  auto response =
      call(create ? QJsonObject{{"op", "create"}} : requestFor(file, "inspect"),
           password);
  PublicWallet out;
  auto publicKeys = response["publicKeys"].toArray();
  auto addresses = response["addresses"].toArray();
  require(publicKeys.size() == AssetCount && addresses.size() == AssetCount,
          "Incomplete public wallet");
  for (int i = 0; i < AssetCount; ++i) {
    out.publicKeys[i] =
        QByteArray::fromHex(publicKeys[i].toString().toLatin1());
    out.addresses[i] = addresses[i].toString();
    if (static_cast<Chain>(i) != Chain::Monero)
      require(!out.publicKeys[i].isEmpty() && !out.addresses[i].isEmpty(),
              "Invalid public wallet");
  }
  if (create) {
    auto encrypted =
        QByteArray::fromBase64(response["encrypted"].toString().toLatin1());
    require(encrypted.size() == 160, "Invalid encrypted wallet");
    require(QDir().mkpath(QFileInfo(file).absolutePath()),
            "Cannot create wallet directory");
    QSaveFile f(file);
    require(f.open(QIODevice::WriteOnly) &&
                f.setPermissions(QFileDevice::ReadOwner |
                                 QFileDevice::WriteOwner) &&
                f.write(encrypted) == encrypted.size() && f.commit(),
            "Cannot save encrypted wallet");
  }
  return out;
}
SignedTransaction signUtxoIsolated(const QString &file, QByteArray &password,
                                   const QByteArray &expected, Chain c,
                                   const QVector<Utxo> &inputs,
                                   const QString &to, quint64 amount,
                                   quint64 rate) {
  WipePassword wipe(password);
  auto r = requestFor(file, "utxo");
  identify(r, c, expected);
  QJsonArray list;
  for (const auto &u : inputs)
    list.append(QJsonObject{{"txid", QString::fromLatin1(u.txid.toHex())},
                            {"index", QString::number(u.index)},
                            {"amount", QString::number(u.amount)},
                            {"sequence", QString::number(u.sequence)}});
  r["inputs"] = list;
  r["to"] = to;
  r["amount"] = QString::number(amount);
  r["rate"] = QString::number(rate);
  return transaction(call(r, password));
}
SignedTransaction signEvmIsolated(const QString &file, QByteArray &password,
                                  const QByteArray &expected, Chain c,
                                  const EvmTransfer &t) {
  WipePassword wipe(password);
  auto r = requestFor(file, "evm");
  identify(r, c, expected);
  r["chainId"] = QString::number(t.chain);
  r["nonce"] = QString::number(t.nonce);
  r["gasLimit"] = QString::number(t.gasLimit);
  r["gasPrice"] = t.gasPrice;
  r["value"] = t.value;
  r["to"] = t.to;
  r["data"] = QString::fromLatin1(t.data.toHex());
  return transaction(call(r, password));
}
SignedTransaction signSolanaIsolated(const QString &file, QByteArray &password,
                                     const QByteArray &expected, Chain c,
                                     const QByteArray &message) {
  WipePassword wipe(password);
  auto r = requestFor(file, "solana");
  identify(r, c, expected);
  r["message"] = QString::fromLatin1(message.toHex());
  return transaction(call(r, password));
}
} // namespace aurum
