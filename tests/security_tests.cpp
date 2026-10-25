#include "security.h"
#include <QCoreApplication>
#include <QFile>
#include <QTextStream>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <sys/prctl.h>
#include <sys/ptrace.h>
#include <sys/random.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace aurum;
static void check(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
static void probe(bool protectedChild) {
  int ready[2], finish[2];
  check(pipe(ready) == 0 && pipe(finish) == 0, "pipe");
  auto pid = fork();
  check(pid >= 0, "fork");
  if (pid == 0) {
    close(ready[0]);
    close(finish[1]);
    unsigned char knownTestData[32];
    std::memset(knownTestData, 0x5a, sizeof knownTestData);
    if (protectedChild)
      protectProcess();
    auto ptr = reinterpret_cast<std::uintptr_t>(knownTestData);
    if (write(ready[1], &ptr, sizeof ptr) != sizeof ptr)
      _exit(2);
    char stop;
    if (read(finish[0], &stop, 1) != 1)
      _exit(3);
    _exit(0);
  }
  close(ready[1]);
  close(finish[0]);
  std::uintptr_t ptr = 0;
  check(read(ready[0], &ptr, sizeof ptr) == sizeof ptr, "child readiness");
  unsigned char copy[32] = {};
  iovec local{copy, sizeof copy},
      remote{reinterpret_cast<void *>(ptr), sizeof copy};
  auto n = process_vm_readv(pid, &local, 1, &remote, 1, 0);
  int readError = errno;
  QByteArray mem = "/proc/" + QByteArray::number(pid) + "/mem";
  int fd = open(mem.constData(), O_RDONLY);
  if (fd >= 0)
    close(fd);
  bool ok = protectedChild ? n == -1 && readError == EPERM && fd < 0
                           : n == sizeof copy && copy[0] == 0x5a && fd >= 0;
  if (protectedChild) {
    errno = 0;
    ok = ok && ptrace(PTRACE_ATTACH, pid, nullptr, nullptr) == -1 &&
         errno == EPERM;
  }
  check(write(finish[1], "x", 1) == 1, "release child");
  close(ready[0]);
  close(finish[1]);
  int status;
  check(waitpid(pid, &status, 0) == pid && WIFEXITED(status) &&
            WEXITSTATUS(status) == 0,
        "child completion");
  check(ok, protectedChild
                ? "protected process permitted external access"
                : "control process unavailable; cannot verify protection");
}
int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  try {
    probe(false);
    probe(true);
    auto sandboxChild = fork();
    check(sandboxChild >= 0, "sandbox fork");
    if (sandboxChild == 0) {
      protectProcess();
      sandboxSigner();
      unsigned char entropy[8];
      bool ok = open("/etc/passwd", O_RDONLY) == -1 && errno == EPERM;
      ok = ok && socket(AF_INET, SOCK_STREAM, 0) == -1 && errno == EPERM;
      ok = ok && syscall(SYS_fork) == -1 && errno == EPERM;
      char *args[] = {const_cast<char *>("/bin/true"), nullptr};
      ok = ok && execve(args[0], args, args + 1) == -1 && errno == EPERM;
      ok = ok && prctl(PR_SET_DUMPABLE, 1, 0, 0, 0) == -1 && errno == EPERM;
      ok = ok && getrandom(entropy, sizeof entropy, 0) == sizeof entropy;
      _exit(ok ? 0 : 10);
    }
    int sandboxStatus;
    check(waitpid(sandboxChild, &sandboxStatus, 0) == sandboxChild &&
              WIFEXITED(sandboxStatus) && WEXITSTATUS(sandboxStatus) == 0,
          "signer sandbox failed to restrict files, networking or execution");
    SecretBuffer secret(64);
    QFile smaps("/proc/self/smaps");
    check(smaps.open(QIODevice::ReadOnly), "read smaps");
    const auto address = reinterpret_cast<std::uintptr_t>(secret.data());
    bool inMapping = false, flagsFound = false;
    const auto lines = smaps.readAll().split('\n');
    for (const auto &line : lines) {
      auto first = line.split(' ').value(0);
      auto range = first.split('-');
      if (range.size() == 2) {
        bool a, b;
        auto start = range[0].toULongLong(&a, 16),
             end = range[1].toULongLong(&b, 16);
        if (a && b)
          inMapping = start <= address && address < end;
      }
      if (inMapping && line.startsWith("VmFlags:"))
        flagsFound = line.contains(" lo ") && line.contains(" dd ");
    }
    check(flagsFound, "secret mapping is not locked and dump-excluded");
    secret.readOnly();
    QByteArray pass("temporary test password");
    {
      WipePassword wipe(pass);
    }
    check(pass.isEmpty(), "password cleanup");
    protectProcess();
    rlimit core{};
    check(getrlimit(RLIMIT_CORE, &core) == 0 && core.rlim_cur == 0 &&
              core.rlim_max == 0,
          "core limit");
    QTextStream(stdout)
        << "PASS: control memory read succeeds; protected process blocks "
           "process_vm_readv, /proc/pid/mem and ptrace.\n"
           "PASS: secret mappings locked and excluded from dumps; password "
           "cleanup; core dumps disabled.\n";
    QTextStream(stdout) << "PASS: signer sandbox denies file open, sockets, "
                           "fork, exec and re-enabling dumps.\n";
    return 0;
  } catch (const std::exception &e) {
    QTextStream(stderr) << e.what() << '\n';
    return 1;
  }
}
