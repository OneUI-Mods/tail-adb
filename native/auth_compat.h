// SPDX-License-Identifier: MIT
#ifndef TAILADB_AUTH_COMPAT_H
#define TAILADB_AUTH_COMPAT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum tailadb_auth_abi { TAILADB_AUTH_UNKNOWN, TAILADB_AUTH_ANDROID16, TAILADB_AUTH_ANDROID17 };

// Android 17 negotiates V1 when get_max_version() returns 1. Keep the proven
// authorization callbacks; TailADB owns its listener and never needs callbacks
// that start Android's separate Wi-Fi TLS server or attach to system_server.
struct auth_callbacks_v1 {
  uint32_t version;
  void (*key_authorized)(void *opaque, uint64_t id);
  void (*key_removed)(const char *public_key, size_t length);
};

static inline enum tailadb_auth_abi tailadb_auth_abi(const char *name) {
  if (name && !strcmp(name, "android16"))
    return TAILADB_AUTH_ANDROID16;
  if (name && !strcmp(name, "android17"))
    return TAILADB_AUTH_ANDROID17;
  return TAILADB_AUTH_UNKNOWN;
}

static inline uint32_t tailadb_auth_version(enum tailadb_auth_abi abi) {
  return abi == TAILADB_AUTH_ANDROID16 || abi == TAILADB_AUTH_ANDROID17 ? 1 : 0;
}

static inline bool tailadb_auth_callbacks_valid(enum tailadb_auth_abi abi,
                                                const struct auth_callbacks_v1 *callbacks) {
  return callbacks && tailadb_auth_version(abi) == 1 && callbacks->version == 1 &&
         callbacks->key_authorized && callbacks->key_removed;
}

static inline bool tailadb_mdns_names_valid(const char *instance, const char *service) {
  if (!instance || !service)
    return false;
  // Match Android 17's maximum of 255 bytes plus NUL without an unbounded strlen.
  for (size_t i = 0; i <= 255; i++) {
    if (!instance[i]) {
      for (size_t j = 0; j <= 255; j++)
        if (!service[j])
          return true;
      return false;
    }
  }
  return false;
}

#endif
