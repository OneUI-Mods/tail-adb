// SPDX-License-Identifier: MIT
#ifndef TAILADB_DAEMON_PROFILES_H
#define TAILADB_DAEMON_PROFILES_H

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

struct tailadb_daemon_profile {
  const char *label;
  const char *policy_abi;
  bool verified;
  const char *adbd_sha256;
  const char *auth_sha256;
};

#include "profiles/android16.h"
#include "profiles/android17.h"
#include "profiles/android17_legacy_adbd.h"

#ifndef TAILADB_ANDROID17_VALIDATION
#define TAILADB_ANDROID17_VALIDATION 0
#endif

static inline const struct tailadb_daemon_profile *tailadb_daemon_profile(const char *adbd,
                                                                          const char *auth) {
  if (!adbd || !auth)
    return NULL;
  const struct tailadb_daemon_profile *profiles[] = {&tailadb_android16, &tailadb_android17,
                                                     &tailadb_android17_legacy_adbd};
  for (size_t i = 0; i < sizeof(profiles) / sizeof(profiles[0]); i++) {
    const struct tailadb_daemon_profile *profile = profiles[i];
    bool permitted =
        profile->verified || (TAILADB_ANDROID17_VALIDATION && profile == &tailadb_android17);
    if (permitted && !strcmp(adbd, profile->adbd_sha256) && !strcmp(auth, profile->auth_sha256))
      return profile;
  }
  return NULL;
}

static inline bool tailadb_daemon_supported(const char *adbd, const char *auth) {
  return tailadb_daemon_profile(adbd, auth) != NULL;
}

#endif
