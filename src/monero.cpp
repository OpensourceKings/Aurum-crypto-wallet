#include "monero.h"
#include "security.h"
#include <QCoreApplication>
#include <QAuthenticator>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QRandomGenerator>
#include <QStandardPaths>
#include <QUrl>
#include <QUrlQuery>
#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

namespace aurum {
namespace {
QString defaultWalletDirectory() {
  return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
         "/monero";
}
QString defaultRpcBinary() {
  const auto bundled = QCoreApplication::applicationDirPath() +
                       "/monero-wallet-rpc";
  if (QFileInfo(bundled).isExecutable())
    return bundled;
  const auto found = QStandardPaths::findExecutable("monero-wallet-rpc");
  return found;
}
QString defaultDaemonBinary() {
  const auto bundled = QCoreApplication::applicationDirPath() + "/monerod";
  if (QFileInfo(bundled).isExecutable())
    return bundled;
  return QStandardPaths::findExecutable("monerod");
}
QString systemCaBundle() {
  const QStringList candidates{
      "/etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem",
      "/etc/pki/tls/certs/ca-bundle.crt",
      "/etc/ssl/certs/ca-bundle.crt",
      "/etc/ssl/cert.pem",
  };
  for (const auto &candidate : candidates) {
    const QFileInfo info(candidate);
    if (info.exists() && info.isFile() && info.isReadable())
      return info.absoluteFilePath();
  }
  return {};
}
bool localHost(const QString &host) {
  return host == "127.0.0.1" || host == "localhost" || host == "::1";
}
bool validNode(const QString &text) {
  const QUrl u(text);
  if (!u.isValid() || u.host().isEmpty() || !u.userInfo().isEmpty() ||
      u.hasFragment() || u.hasQuery() ||
      (u.scheme() != "https" &&
       !(u.scheme() == "http" &&
         (localHost(u.host()) ||
          u.host().endsWith(".onion", Qt::CaseInsensitive)))))
    return false;
  return u.path().isEmpty() || u.path() == "/";
}
bool validProxy(const QString &text) {
  if (text.isEmpty())
    return true;
  const auto parts = text.split(':');
  if (parts.size() != 2 || parts[0].isEmpty())
    return false;
  bool ok = false;
  const int port = parts[1].toInt(&ok);
  return ok && port > 0 && port < 65536 &&
         (parts[0] == "127.0.0.1" || parts[0] == "localhost" ||
          parts[0] == "::1");
}
QString rpcError(const QJsonObject &root) {
  const auto error = root.value("error").toObject();
  if (error.isEmpty())
    return {};
  return error.value("message").toString("Monero wallet RPC failed");
}
} // namespace

MoneroClient::MoneroClient(QObject *parent) : QObject(parent) {
  walletDir_ = defaultWalletDirectory();
  rpcBinary_ = defaultRpcBinary();
  daemonBinary_ = defaultDaemonBinary();
  setNode(node_, proxy_);
  refreshTimer_.setInterval(10000);
  connect(&manager_, &QNetworkAccessManager::authenticationRequired, this,
          [this](QNetworkReply *, QAuthenticator *authenticator) {
            if (!rpcUser_.isEmpty() && !rpcPassword_.isEmpty()) {
              authenticator->setUser(QString::fromUtf8(rpcUser_));
              authenticator->setPassword(QString::fromUtf8(rpcPassword_));
            }
          });
  connect(&refreshTimer_, &QTimer::timeout, this, &MoneroClient::refresh);
  state_.status = "Connecting to Monero through Tor";
}

MoneroClient::~MoneroClient() { stop(); }

void MoneroClient::setNode(const QString &endpoint, const QString &proxy) {
  if (!validNode(endpoint) || !validProxy(proxy))
    throw std::runtime_error(
        "Monero node must be HTTPS, loopback HTTP, or an .onion HTTP node; "
        "proxy must be a local SOCKS endpoint");
  if (QUrl(endpoint).host().endsWith(".onion", Qt::CaseInsensitive) &&
      proxy.isEmpty())
    throw std::runtime_error(
        "An .onion Monero node requires a local Tor SOCKS proxy");
  node_ = endpoint;
  proxy_ = proxy;
  if (running() || daemonPid_ > 0) {
    stop();
    state_ = {};
    state_.status = "Node settings changed";
    emit changed();
  }
}

void MoneroClient::setWalletDirectory(const QString &directory) {
  const QFileInfo info(directory);
  if (info.exists() && !info.isDir())
    throw std::runtime_error("Monero wallet path is not a directory");
  walletDir_ = info.absoluteFilePath();
}

void MoneroClient::setRpcBinary(const QString &path) {
  if (!QFileInfo(path).isExecutable())
    throw std::runtime_error("monero-wallet-rpc was not found");
  rpcBinary_ = QFileInfo(path).absoluteFilePath();
}

bool MoneroClient::spawnDaemon(QString *error) {
  const auto url = QUrl(node_);
  // A user-selected remote node owns synchronization. The bundled daemon is
  // only started for Aurum's default loopback endpoint.
  if (url.scheme() != "http" || !localHost(url.host()) ||
      url.port(18081) != 18081 || daemonPid_ > 0)
    return true;
  if (daemonBinary_.isEmpty() || !QFileInfo(daemonBinary_).isExecutable())
    return true;
  const auto hardener =
      QFile::encodeName(QCoreApplication::applicationDirPath() +
                        "/libaurum-monero-hardener.so");
  if (!QFileInfo(QString::fromLocal8Bit(hardener)).isFile()) {
    *error = "Monero hardening library is missing";
    return false;
  }
  const auto daemonDir = walletDir_ + "/daemon";
  if (!QDir().mkpath(daemonDir)) {
    *error = "Cannot create the Monero daemon directory";
    return false;
  }
  QFile::setPermissions(daemonDir, QFileDevice::ReadOwner |
                                      QFileDevice::WriteOwner |
                                      QFileDevice::ExeOwner);
  const auto binary = daemonBinary_.toUtf8();
  const auto data = daemonDir.toUtf8();
  const auto proxy = proxy_.toUtf8();
  QVector<QByteArray> strings{binary,
                              "--non-interactive",
                              "--no-igd",
                              "--hide-my-port",
                              "--prune-blockchain",
                              "--sync-pruned-blocks",
                              "--data-dir",
                              data,
                              "--rpc-bind-ip",
                              "127.0.0.1",
                              "--rpc-bind-port",
                              "18081",
                              "--p2p-bind-ip",
                              "127.0.0.1",
                              "--log-file",
                              "/dev/null",
                              "--log-level",
                              "0",
                              "--max-concurrency",
                              "1"};
  if (!proxy.isEmpty())
    strings << "--proxy" << proxy;
  QVector<char *> argv;
  argv.reserve(strings.size() + 1);
  for (auto &s : strings)
    argv.push_back(s.data());
  argv.push_back(nullptr);
  const pid_t parentPid = getpid();
  const pid_t child = fork();
  if (child == 0) {
    if (prctl(PR_SET_PDEATHSIG, SIGTERM) != 0 || getppid() != parentPid)
      _exit(127);
    const int nullfd = open("/dev/null", O_RDWR | O_CLOEXEC);
    if (nullfd >= 0) {
      dup2(nullfd, STDIN_FILENO);
      dup2(nullfd, STDOUT_FILENO);
      dup2(nullfd, STDERR_FILENO);
      if (nullfd > STDERR_FILENO)
        close(nullfd);
    }
    struct rlimit core{0, 0};
    setrlimit(RLIMIT_CORE, &core);
    prctl(PR_SET_DUMPABLE, 0, 0, 0, 0);
    prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
    clearenv();
    setenv("LANG", "C.UTF-8", 1);
    setenv("TZ", "UTC", 1);
    setenv("LD_PRELOAD", hardener.constData(), 1);
    execve(binary.constData(), argv.data(), environ);
    _exit(127);
  }
  if (child < 0) {
    *error = QString("Cannot launch monerod: %1")
                 .arg(QString::fromLocal8Bit(std::strerror(errno)));
    return false;
  }
  daemonPid_ = child;
  return true;
}

bool MoneroClient::spawn(QString *error) {
  if (running())
    return true;
  if (rpcBinary_.isEmpty() || !QFileInfo(rpcBinary_).isExecutable()) {
    *error = "Monero support requires the verified monero-wallet-rpc binary";
    return false;
  }
  if (!QDir().mkpath(walletDir_)) {
    *error = "Cannot create the Monero wallet directory";
    return false;
  }
  QFile::setPermissions(walletDir_, QFileDevice::ReadOwner |
                                      QFileDevice::WriteOwner |
                                      QFileDevice::ExeOwner);
  const auto node = node_.toUtf8();
  const auto proxy = proxy_.toUtf8();
  const auto nodeUrl = QUrl(node_);
  const auto caBundle = nodeUrl.scheme() == "https" ? systemCaBundle() : QString{};
  if (nodeUrl.scheme() == "https" && caBundle.isEmpty()) {
    *error = "No system CA bundle is available for the Monero HTTPS node";
    return false;
  }
  const auto dir = walletDir_.toUtf8();
  const auto binary = rpcBinary_.toUtf8();
  rpcUser_ = QByteArray("aurum-") +
             QByteArray::number(QRandomGenerator::global()->generate64(), 16);
  rpcPassword_ =
      QByteArray::number(QRandomGenerator::global()->generate64(), 16) +
      QByteArray::number(QRandomGenerator::global()->generate64(), 16);
  const auto rpcLogin = rpcUser_ + ":" + rpcPassword_;
  const auto hardener =
      QFile::encodeName(QCoreApplication::applicationDirPath() +
                        "/libaurum-monero-hardener.so");
  if (!QFileInfo(QString::fromLocal8Bit(hardener)).isFile()) {
    *error = "Monero hardening library is missing";
    return false;
  }
  if (!spawnDaemon(error))
    return false;
  quint16 candidate = 24000 +
                      quint16(QRandomGenerator::global()->bounded(20000));
  for (int attempt = 0; attempt != 8; ++attempt) {
    const auto port = QByteArray::number(candidate + attempt);
    QVector<QByteArray> strings{binary,
                                "--non-interactive",
                                "--rpc-login",
                                rpcLogin,
                                "--rpc-bind-ip",
                                "127.0.0.1",
                                "--rpc-bind-port",
                                port,
                                "--wallet-dir",
                                dir,
                                "--daemon-address",
                                node,
                                "--log-file",
                                "/dev/null",
                                "--log-level",
                                "0",
                                "--max-concurrency",
                                "1"};
    if (!proxy.isEmpty()) {
      strings << "--proxy" << proxy;
    }
    // Monero deliberately requires an explicit trust source when a proxy is
    // used. Keep normal certificate verification; never use
    // --daemon-ssl-allow-any-cert for the public node.
    if (!caBundle.isEmpty())
      strings << "--daemon-ssl-ca-certificates" << caBundle.toUtf8();
    QVector<char *> argv;
    argv.reserve(strings.size() + 1);
    for (auto &s : strings)
      argv.push_back(s.data());
    argv.push_back(nullptr);
    const pid_t parentPid = getpid();
    pid_t child = fork();
    if (child == 0) {
      if (prctl(PR_SET_PDEATHSIG, SIGTERM) != 0 || getppid() != parentPid)
        _exit(127);
      const int nullfd = open("/dev/null", O_RDWR | O_CLOEXEC);
      if (nullfd >= 0) {
        dup2(nullfd, STDIN_FILENO);
        dup2(nullfd, STDOUT_FILENO);
        dup2(nullfd, STDERR_FILENO);
        if (nullfd > STDERR_FILENO)
          close(nullfd);
      }
      struct rlimit core{0, 0};
      setrlimit(RLIMIT_CORE, &core);
      prctl(PR_SET_DUMPABLE, 0, 0, 0, 0);
      prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
      clearenv();
      setenv("LANG", "C.UTF-8", 1);
      setenv("TZ", "UTC", 1);
      setenv("LD_PRELOAD", hardener.constData(), 1);
      execve(binary.constData(), argv.data(), environ);
      _exit(127);
    }
    if (child < 0) {
      *error = QString("Cannot launch monero-wallet-rpc: %1")
                   .arg(QString::fromLocal8Bit(std::strerror(errno)));
      return false;
    }
    pid_ = child;
    port_ = candidate + attempt;
    return true;
  }
  *error = "Could not choose a local Monero RPC port";
  return false;
}

void MoneroClient::rpc(const QString &method, const QJsonObject &params,
                       std::function<void(QJsonObject, QString)> callback) {
  if (!running()) {
    callback({}, "Monero wallet RPC is not running");
    return;
  }
  QNetworkRequest request(QUrl(QString("http://127.0.0.1:%1/json_rpc").arg(
      port_)));
  // The upstream server yields between automatic-refresh batches. A batch
  // over Tor can take longer than an ordinary loopback request.
  request.setTransferTimeout(method == "get_version" ? 2000
                             : method == "refresh" ? 600000
                             : 30000);
  request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
  request.setHeader(QNetworkRequest::UserAgentHeader, "AurumWallet/0.7.2");
  const auto body = QJsonDocument(QJsonObject{{"jsonrpc", "2.0"},
                                              {"id", "aurum"},
                                              {"method", method},
                                              {"params", params}})
                        .toJson(QJsonDocument::Compact);
  auto *reply = manager_.post(request, body);
  auto data = std::make_shared<QByteArray>();
  connect(reply, &QNetworkReply::readyRead, this, [reply, data] {
    *data += reply->readAll();
    if (data->size() > 512 * 1024)
      reply->abort();
  });
  const auto generation = generation_;
  connect(reply, &QNetworkReply::finished, this,
          [this, reply, data, callback, generation] {
            *data += reply->readAll();
            reply->deleteLater();
            if (generation != generation_)
              return;
            QString error;
            QJsonObject root;
            QJsonParseError parse;
            const auto doc = QJsonDocument::fromJson(*data, &parse);
            if (parse.error != QJsonParseError::NoError || !doc.isObject())
              error = reply->error() == QNetworkReply::NoError
                          ? "Monero wallet RPC returned malformed JSON"
                          : reply->errorString();
            else {
              root = doc.object();
              error = rpcError(root);
              if (error.isEmpty() && root.value("result").isObject())
                root = root.value("result").toObject();
            }
            if (error.isEmpty() && reply->error() != QNetworkReply::NoError)
              error = reply->errorString();
            callback(root, error);
          });
}

void MoneroClient::waitForRpc(int attempt, Callback callback) {
  rpc("get_version", {}, [this, attempt, callback](QJsonObject, QString e) {
    if (e.isEmpty()) {
      callback({});
      return;
    }
    if (attempt >= 80) {
      callback("Monero wallet RPC did not start: " + e);
      return;
    }
    QTimer::singleShot(100, this,
                       [this, attempt, callback] { waitForRpc(attempt + 1, callback); });
  });
}

void MoneroClient::openWallet(QByteArray password, Callback callback) {
  WipePassword wipe(password);
  const QString name = QFileInfo(walletFile()).fileName();
  QJsonObject params{{"filename", name},
                     {"password", QString::fromUtf8(password)}};
  wipe.clear();
  rpc("open_wallet", params, [callback](QJsonObject, QString e) {
    callback(e);
  });
}

void MoneroClient::openOrCreate(QByteArray password, Callback callback) {
  WipePassword wipe(password);
  const bool exists = QFile::exists(walletFile()) ||
                      QFile::exists(walletFile() + ".keys");
  if (exists) {
    openWallet(std::move(password), callback);
    return;
  }
  const QString name = QFileInfo(walletFile()).fileName();
  QJsonObject params{{"filename", name},
                     {"password", QString::fromUtf8(password)},
                     {"language", "English"}};
  wipe.clear();
  rpc("create_wallet", params, [callback](QJsonObject, QString e) {
    callback(e);
  });
}

void MoneroClient::updateAddress(Callback callback) {
  rpc("get_address", {{"account_index", 0}},
      [this, callback](QJsonObject result, QString e) {
        if (e.isEmpty()) {
          const auto value = result.value("address").toString();
          if (value.size() != 95 && value.size() != 106)
            e = "Monero wallet returned an invalid address";
          else
            state_.address = value;
        }
        callback(e);
      });
}

void MoneroClient::refreshBalance(Callback callback) {
  rpc("get_balance", {{"account_index", 0}, {"all_accounts", false}},
      [this, callback](QJsonObject result, QString e) {
        if (e.isEmpty()) {
          const auto raw = result.value("balance");
          bool ok = false;
          quint64 balance = 0;
          if (raw.isString()) {
            balance = raw.toString().toULongLong(&ok);
          } else if (raw.isDouble()) {
            const auto integer = raw.toInteger(-1);
            ok = integer >= 0;
            if (ok)
              balance = static_cast<quint64>(integer);
          }
          if (!ok)
            e = "Monero wallet returned an invalid balance";
          else {
            state_.balance = balance;
            state_.units = QString::number(balance);
            unlockedBalance_ = result.value("unlocked_balance").toVariant().toULongLong();
            state_.known = synchronized_;
            state_.updated = QDateTime::currentDateTimeUtc();
          }
        }
        callback(e);
      });
}

void MoneroClient::start(QByteArray password, Callback callback) {
  stop();
  state_ = {};
  state_.status = "Starting Monero wallet";
  emit changed();
  QString error;
  if (!spawn(&error)) {
    // spawnDaemon() can have started a local pruned daemon before the wallet
    // RPC child failed. Tear down both children on every startup failure.
    stop();
    password.fill('\0');
    reportError(error);
    callback(error);
    return;
  }
  const auto generation = ++generation_;
  opening_ = true;
  waitForRpc(0, [this, password = std::move(password), callback,
                 generation](QString e) mutable {
    WipePassword wipe(password);
    if (generation != generation_)
      return;
    if (!e.isEmpty()) {
      opening_ = false;
      reportError(e);
      stop();
      callback(e);
      return;
    }
    openOrCreate(std::move(password), [this, callback, generation](QString e) {
      if (generation != generation_)
        return;
      if (!e.isEmpty()) {
        opening_ = false;
        reportError("Cannot open the encrypted Monero wallet: " + e);
        stop();
        callback(e);
        return;
      }
      updateAddress([this, callback, generation](QString e) {
        if (generation != generation_)
          return;
        if (!e.isEmpty()) {
          opening_ = false;
          reportError(e);
          stop();
          callback(e);
          return;
        }
        state_.status = "Wallet open · syncing in background";
        opening_ = false;
        emit changed();
        refreshTimer_.start();
        QTimer::singleShot(500, this, [this, generation] {
          if (generation == generation_)
            refresh();
        });
        callback({});
      });
    });
  });
}

void MoneroClient::refresh() {
  if (!running() || opening_ || polling_ || state_.address.isEmpty())
    return;
  polling_ = true;
  state_.refreshing = true;
  state_.status = state_.known ? "Checking sync · Monero" : "Syncing · Monero";
  emit changed();
  const auto generation = generation_;
  rpc("refresh", {{"autoconfirm", false}, {"start_height", 0}},
      [this, generation](QJsonObject, QString e) {
        if (generation != generation_)
          return;
        if (!e.isEmpty()) {
          polling_ = false;
          synchronized_ = false;
          state_.refreshing = false;
          state_.status = e.contains("syncing", Qt::CaseInsensitive) ||
                                  e.contains("timed out", Qt::CaseInsensitive)
              ? "Syncing · background"
              : "Monero sync status: " + e;
          emit changed();
          return;
        }
        synchronized_ = true;
        state_.status = "Synced · Monero wallet RPC";
        refreshBalance([this, generation](QString balanceError) {
            if (generation != generation_)
              return;
            polling_ = false;
            state_.refreshing = false;
            if (!balanceError.isEmpty()) {
              synchronized_ = false;
              if (balanceError.contains("timed out", Qt::CaseInsensitive)) {
                state_.status = "Syncing · background";
                emit changed();
              } else {
                reportError("Monero balance: " + balanceError);
              }
              return;
            }
            emit changed();
          });
      });
}

void MoneroClient::transfer(const QString &recipient, const QString &amount,
                            QByteArray password, TransferCallback callback) {
  if (!running() || state_.address.isEmpty()) {
    password.fill('\0');
    callback({}, "Monero wallet is not open");
    return;
  }
  if (!synchronized_ || !state_.known || polling_) {
    password.fill('\0');
    callback({}, "Wait for Monero synchronization to finish before sending");
    return;
  }
  quint64 atomic = 0;
  try {
    atomic = parseAmount(amount.trimmed(), 12);
  } catch (...) {
    password.fill('\0');
    callback({}, "Enter a valid XMR amount");
    return;
  }
  if (atomic == 0) {
    password.fill('\0');
    callback({}, "Enter a positive XMR amount");
    return;
  }
  if (atomic > unlockedBalance_) {
    password.fill('\0');
    callback({}, "Insufficient unlocked XMR balance");
    return;
  }
  QJsonObject destination{{"address", recipient.trimmed()},
                          {"amount", QString::number(atomic)}};
  const auto generation = generation_;
  rpc("close_wallet", {}, [this, password = std::move(password), destination,
                            callback, generation](QJsonObject, QString e) mutable {
    WipePassword wipe(password);
    if (generation != generation_)
      return;
    if (!e.isEmpty()) {
      callback({}, "Could not prepare the Monero wallet: " + e);
      return;
    }
    openWallet(std::move(password), [this, destination, callback,
                                     generation](QString error) {
      if (generation != generation_)
        return;
      if (!error.isEmpty()) {
        callback({}, "Password rejected by the Monero wallet: " + error);
        return;
      }
      rpc("transfer", {{"destinations", QJsonArray{destination}},
                         {"get_tx_key", false},
                         {"get_tx_hex", false}},
          [this, callback, generation](QJsonObject result, QString e) {
            if (generation != generation_)
              return;
            if (e.isEmpty()) {
              const auto hash = result.value("tx_hash").toString();
              if (hash.size() != 64)
                e = "Monero wallet returned an invalid transaction hash";
              else {
                refresh();
                callback(hash, {});
                return;
              }
            }
            callback({}, e);
          });
    });
  });
}

void MoneroClient::reportError(const QString &error) {
  state_.refreshing = false;
  state_.known = false;
  if (error.contains("invalid password", Qt::CaseInsensitive) ||
      error.contains("password", Qt::CaseInsensitive))
    state_.status =
        "Monero wallet password rejected; its XMR wallet is separate";
  else if (error.contains("proxy", Qt::CaseInsensitive) ||
           error.contains("SOCKS", Qt::CaseInsensitive) ||
           error.contains("TLS", Qt::CaseInsensitive) ||
           error.contains("SSL", Qt::CaseInsensitive))
    state_.status = "Tor/Monero node connection failed: " + error;
  else
    state_.status = error;
  emit changed();
}

void MoneroClient::stop() {
  ++generation_;
  opening_ = false;
  polling_ = false;
  synchronized_ = false;
  unlockedBalance_ = 0;
  refreshTimer_.stop();
  for (auto *reply : manager_.findChildren<QNetworkReply *>())
    if (reply->isRunning())
      reply->abort();
  if (pid_ > 0) {
    kill(pid_, SIGTERM);
    int status = 0;
    for (int i = 0; i < 20; ++i) {
      const auto result = waitpid(pid_, &status, WNOHANG);
      if (result == pid_) {
        pid_ = -1;
        break;
      }
      if (result < 0 && errno != EINTR)
        break;
      usleep(10000);
    }
    if (pid_ > 0) {
      kill(pid_, SIGKILL);
      while (waitpid(pid_, &status, 0) < 0 && errno == EINTR) {
      }
      pid_ = -1;
    }
  }
  port_ = 0;
  rpcUser_.fill('\0');
  rpcUser_.clear();
  WipePassword wipe(rpcPassword_);
  stopDaemon();
}

void MoneroClient::stopDaemon() {
  if (daemonPid_ <= 0)
    return;
  int status = 0;
  const auto first = waitpid(daemonPid_, &status, WNOHANG);
  if (first == daemonPid_ || (first < 0 && errno != EINTR)) {
    daemonPid_ = -1;
    return;
  }
  kill(daemonPid_, SIGTERM);
  for (int i = 0; i < 100; ++i) {
    const auto result = waitpid(daemonPid_, &status, WNOHANG);
    if (result == daemonPid_) {
      daemonPid_ = -1;
      return;
    }
    if (result < 0 && errno != EINTR)
      break;
    usleep(10000);
  }
  kill(daemonPid_, SIGKILL);
  while (waitpid(daemonPid_, &status, 0) < 0 && errno == EINTR) {
  }
  daemonPid_ = -1;
}

} // namespace aurum
