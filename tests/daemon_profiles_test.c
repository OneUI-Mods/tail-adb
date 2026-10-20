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
  assert(tailadb_daemon_supported(tailadb_android17.adbd_sha256, tailadb_android17.auth_sha256) ==
         (bool)TAILADB_ANDROID17_VALIDATION);
  assert(tailadb_daemon_profile(adbd, auth) == &tailadb_android16);
  assert(!strcmp(tailadb_daemon_profile(adbd, auth)->policy_abi, "android16"));
  assert(!tailadb_daemon_supported(adbd, tailadb_android17.auth_sha256));
  assert(!tailadb_daemon_supported(tailadb_android17.adbd_sha256, auth));
  if (TAILADB_ANDROID17_VALIDATION) {
    const struct tailadb_daemon_profile *candidate =
        tailadb_daemon_profile(tailadb_android17.adbd_sha256, tailadb_android17.auth_sha256);
    assert(candidate == &tailadb_android17);
    assert(!strcmp(candidate->policy_abi, "android17"));
  }
  puts("Daemon profiles: PASS (Android 16 preserved; Android 17 requires validation build)");
  return 0;
}
