// SPDX-License-Identifier: MIT
#ifndef TAILADB_DAEMON_PROFILES_H
#define TAILADB_DAEMON_PROFILES_H

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

struct tailadb_daemon_profile {
  const char *label;
  bool verified;
  const char *adbd_sha256;
  const char *auth_sha256;
};

#include "profiles/android16.h"
#include "profiles/android17.h"

static inline bool tailadb_daemon_supported(const char *adbd, const char *auth) {
  if (!adbd || !auth)
    return false;
  const struct tailadb_daemon_profile *profiles[] = {&tailadb_android16, &tailadb_android17};
  for (size_t i = 0; i < sizeof(profiles) / sizeof(profiles[0]); i++) {
    const struct tailadb_daemon_profile *profile = profiles[i];
    if (profile->verified && !strcmp(adbd, profile->adbd_sha256) &&
        !strcmp(auth, profile->auth_sha256))
      return true;
  }
  return false;
}

#endif
