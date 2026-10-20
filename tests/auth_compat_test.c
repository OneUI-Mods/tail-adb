// SPDX-License-Identifier: MIT
#include "../native/auth_compat.h"
#include <assert.h>
#include <stdio.h>

static void authorized(void *opaque, uint64_t id) {
  (void)opaque;
  (void)id;
}

static void removed(const char *key, size_t length) {
  (void)key;
  (void)length;
}

int main(void) {
  // Layout of AOSP's 64-bit AdbdAuthCallbacksV1, shared by both audited ROMs.
  assert(offsetof(struct auth_callbacks_v1, key_authorized) == 8);
  assert(offsetof(struct auth_callbacks_v1, key_removed) == 16);
  assert(sizeof(struct auth_callbacks_v1) == 24);
  assert(tailadb_auth_abi(NULL) == TAILADB_AUTH_UNKNOWN);
  assert(tailadb_auth_abi("android18") == TAILADB_AUTH_UNKNOWN);
  assert(tailadb_auth_version(TAILADB_AUTH_UNKNOWN) == 0);
  for (int i = TAILADB_AUTH_ANDROID16; i <= TAILADB_AUTH_ANDROID17; i++) {
    enum tailadb_auth_abi abi = (enum tailadb_auth_abi)i;
    assert(tailadb_auth_version(abi) == 1);
    struct auth_callbacks_v1 callbacks = {1, authorized, removed};
    assert(tailadb_auth_callbacks_valid(abi, &callbacks));
    assert(!tailadb_auth_callbacks_valid(abi, NULL));
    callbacks.version = 3;
    assert(!tailadb_auth_callbacks_valid(abi, &callbacks));
    callbacks.version = 2;
    assert(!tailadb_auth_callbacks_valid(abi, &callbacks));
    callbacks.version = 1;
    callbacks.key_removed = NULL;
    assert(!tailadb_auth_callbacks_valid(abi, &callbacks));
  }
  assert(tailadb_mdns_names_valid("device", "_adb._tcp"));
  assert(!tailadb_mdns_names_valid(NULL, "_adb._tcp"));
  assert(!tailadb_mdns_names_valid("device", NULL));
  char name[257];
  memset(name, 'a', 255);
  name[255] = 0;
  assert(tailadb_mdns_names_valid(name, "_adb._tcp"));
  assert(tailadb_mdns_names_valid("device", name));
  name[255] = 'a';
  name[256] = 0;
  assert(!tailadb_mdns_names_valid(name, "_adb._tcp"));
  assert(!tailadb_mdns_names_valid("device", name));
  puts("Auth compatibility: PASS (V1 ABI, negotiation, missing callbacks, API 37 name bounds)");
  return 0;
}
