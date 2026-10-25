#include <sys/prctl.h>
#include <sys/resource.h>

// Loaded by the upstream Monero process before its main function. Setting
// dumpability before wallet code starts is necessary because exec resets the
// flag inherited from Aurum's launcher.
__attribute__((constructor)) static void aurumMoneroHardener() {
  struct rlimit core{0, 0};
  setrlimit(RLIMIT_CORE, &core);
  prctl(PR_SET_DUMPABLE, 0, 0, 0, 0);
  prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
}
