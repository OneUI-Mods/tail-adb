// SPDX-License-Identifier: MIT
#include "../native/daemon_profiles.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
  const char *adbd = tailadb_android16.adbd_sha256;
  const char *auth = tailadb_android16.auth_sha256;
  assert(tailadb_daemon_supported(adbd, auth));
  assert(!tailadb_daemon_supported(auth, adbd));
  assert(!tailadb_daemon_supported(adbd, "unverified-auth"));
  assert(!tailadb_daemon_supported("unverified-adbd", auth));
  assert(!tailadb_daemon_supported("", ""));
  assert(!tailadb_daemon_supported(NULL, auth));
  assert(!tailadb_android17.verified);
  assert(!tailadb_daemon_supported(tailadb_android17.adbd_sha256, tailadb_android17.auth_sha256));
  puts("Daemon profile checks: PASS (known Android 16 pair retained; unknown pairs rejected)");
  return 0;
}
