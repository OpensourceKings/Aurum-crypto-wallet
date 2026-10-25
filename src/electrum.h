#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QQueue>
#include <QSslSocket>
#include <QTimer>
#include <QUrl>
#include <functional>
namespace aurum {
// Public chain data only. This transport never receives secret key material.
class ElectrumClient : public QObject {
public:
  using Callback = std::function<void(QJsonValue, QString)>;
  explicit ElectrumClient(QObject *parent = nullptr);
  static bool validEndpoint(const QString &);
  void setEndpoint(const QString &);
  void cancel();
  void request(const QString &method, const QJsonArray &, Callback);

private:
  struct Job {
    QString method;
    QJsonArray params;
    Callback callback;
  };
  QSslSocket socket_;
  QTimer timeout_, pace_;
  QUrl endpoint_;
  QQueue<Job> jobs_;
  QByteArray buffer_;
  quint64 nextId_ = 0, currentId_ = 0;
  bool verified_ = false, failing_ = false;
  int checking_ = 0;
  void pump();
  void send(const QString &, const QJsonArray &);
  void receive();
  void fail(const QString &);
};
// Parse and hash the raw transaction locally before trusting a previous output.
QJsonObject verifiedElectrumTransaction(const QString &rawHex,
                                        const QString &expectedId);
QString electrumScriptHash(const QString &address);
} // namespace aurum
