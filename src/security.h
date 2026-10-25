#pragma once
#include <QByteArray>
#include <cstddef>

namespace aurum {
// Invoke before constructing QApplication or creating any secret material.
void protectProcess();
// One-way restriction for the isolated signer; invoke before receiving secrets.
void sandboxSigner();

class SecretBuffer {
public:
  explicit SecretBuffer(std::size_t size);
  ~SecretBuffer();
  SecretBuffer(const SecretBuffer &) = delete;
  SecretBuffer &operator=(const SecretBuffer &) = delete;
  unsigned char *data() { return data_; }
  const unsigned char *data() const { return data_; }
  void readOnly();

private:
  unsigned char *data_ = nullptr;
};

class WipePassword {
public:
  explicit WipePassword(QByteArray &value) : value_(value) {}
  ~WipePassword();
  void clear();

private:
  QByteArray &value_;
};
} // namespace aurum
