#include "security.h"
#include <sodium.h>
#include <stdexcept>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#ifdef __linux__
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace aurum {
void sandboxSigner() {
#ifdef __linux__
#if defined(__x86_64__)
  constexpr unsigned architecture = AUDIT_ARCH_X86_64;
#elif defined(__aarch64__)
  constexpr unsigned architecture = AUDIT_ARCH_AARCH64;
#else
#error Unsupported signer sandbox architecture
#endif
#define ALLOW_SYSCALL(name)                                                    \
  BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_##name, 0, 1),                       \
      BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW)
  const sock_filter filter[] = {
      BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, arch)),
      BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, architecture, 1, 0),
      BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS),
      BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, nr)),
      ALLOW_SYSCALL(read),
      ALLOW_SYSCALL(write),
      ALLOW_SYSCALL(close),
      // Local pipes and signal stacks support diagnostic memory checks;
      // neither provides filesystem or external process access.
      ALLOW_SYSCALL(pipe2),
      ALLOW_SYSCALL(fcntl),
      ALLOW_SYSCALL(sigaltstack),
#ifdef SYS_pipe
      ALLOW_SYSCALL(pipe),
#endif
      ALLOW_SYSCALL(brk),
      ALLOW_SYSCALL(mmap),
      ALLOW_SYSCALL(munmap),
      ALLOW_SYSCALL(mprotect),
      ALLOW_SYSCALL(madvise),
      ALLOW_SYSCALL(mlock),
      ALLOW_SYSCALL(munlock),
      ALLOW_SYSCALL(futex),
      ALLOW_SYSCALL(getrandom),
      ALLOW_SYSCALL(clock_gettime),
      ALLOW_SYSCALL(rt_sigaction),
      ALLOW_SYSCALL(rt_sigprocmask),
      ALLOW_SYSCALL(rt_sigreturn),
      ALLOW_SYSCALL(getpid),
      ALLOW_SYSCALL(gettid),
      ALLOW_SYSCALL(sched_yield),
      ALLOW_SYSCALL(exit),
      ALLOW_SYSCALL(exit_group),
      BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EPERM)};
#undef ALLOW_SYSCALL
  const sock_fprog program{
      static_cast<unsigned short>(sizeof filter / sizeof filter[0]),
      const_cast<sock_filter *>(filter)};
  if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0 ||
      prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &program) != 0)
    throw std::runtime_error("Cannot isolate signer");
#elif defined(_WIN32)
  // Job object: the signer may not spawn children or touch the desktop, and
  // is killed if the job handle goes away.
  HANDLE job = CreateJobObjectW(nullptr, nullptr);
  if (!job)
    throw std::runtime_error("Cannot isolate signer");
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
  limits.BasicLimitInformation.LimitFlags =
      JOB_OBJECT_LIMIT_ACTIVE_PROCESS | JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE |
      JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION;
  limits.BasicLimitInformation.ActiveProcessLimit = 1;
  JOBOBJECT_BASIC_UI_RESTRICTIONS ui{};
  ui.UIRestrictionsClass = JOB_OBJECT_UILIMIT_ALL;
  if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits,
                               sizeof limits) ||
      !SetInformationJobObject(job, JobObjectBasicUIRestrictions, &ui,
                               sizeof ui) ||
      !AssignProcessToJobObject(job, GetCurrentProcess()))
    throw std::runtime_error("Cannot isolate signer");
#else
  throw std::runtime_error("Signer sandbox is unsupported on this platform");
#endif
}
void protectProcess() {
#ifdef __linux__
  const rlimit noCore{0, 0};
  if (setrlimit(RLIMIT_CORE, &noCore) != 0 ||
      prctl(PR_SET_DUMPABLE, 0, 0, 0, 0) != 0 ||
      prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0)
    throw std::runtime_error("Cannot enable process protections");
#elif defined(_WIN32)
  // No crash dialogs or WER dumps containing key material; no CWD DLL search.
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX |
               SEM_NOOPENFILEERRORBOX);
  SetDllDirectoryW(L"");
  SetSearchPathMode(BASE_SEARCH_PATH_ENABLE_SAFE_SEARCHMODE);
#else
  throw std::runtime_error("Process protections are unsupported here");
#endif
}
SecretBuffer::SecretBuffer(std::size_t size) {
  if (!size || sodium_init() < 0)
    throw std::runtime_error("Cannot initialize secret memory");
  data_ = static_cast<unsigned char *>(sodium_malloc(size));
  if (!data_)
    throw std::runtime_error("Cannot allocate secret memory");
  bool protectedMemory = sodium_mlock(data_, size) == 0;
#ifdef __linux__
  // Some diagnostic runtimes intercept mlock as a successful no-op. Require
  // the kernel to lock these pages too; the protection must actually exist.
  protectedMemory = protectedMemory && syscall(SYS_mlock, data_, size) == 0;
  const auto pageSize = sysconf(_SC_PAGESIZE);
  if (pageSize <= 0) {
    protectedMemory = false;
  } else {
    const auto begin = reinterpret_cast<std::uintptr_t>(data_);
    const auto page = begin - begin % static_cast<std::size_t>(pageSize);
    const auto end = begin + size;
    protectedMemory =
        protectedMemory &&
        madvise(reinterpret_cast<void *>(page), end - page, MADV_DONTDUMP) == 0;
  }
#endif
  if (!protectedMemory) {
    sodium_free(data_);
    data_ = nullptr;
    throw std::runtime_error(
        "Cannot lock and exclude secret memory from dumps");
  }
  sodium_memzero(data_, size);
}
SecretBuffer::~SecretBuffer() { sodium_free(data_); }
void SecretBuffer::readOnly() {
  if (sodium_mprotect_readonly(data_) != 0)
    throw std::runtime_error("Cannot protect secret memory from writes");
}
WipePassword::~WipePassword() { clear(); }
void WipePassword::clear() {
  if (!value_.isEmpty()) {
    sodium_memzero(value_.data(), value_.size());
    value_.clear();
  }
}
} // namespace aurum
