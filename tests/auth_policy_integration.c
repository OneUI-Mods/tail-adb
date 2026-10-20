// SPDX-License-Identifier: MIT
// Compile the real interposed functions against host-only Android header shims.
// Suppress only the automatic constructor: this test initializes the private
// session explicitly and never opens an Android listener or invokes root code.
#define TAILADB_HOST_AUTH_TEST 1
#define constructor unused
#include "../native/policy.c"
#undef constructor
#include <assert.h>
#include <sys/wait.h>

static unsigned approvals;

static void authorized(void *opaque, uint64_t id) {
  assert(opaque == &approvals && id > 0);
  approvals++;
}

static void removed(const char *key, size_t length) {
  (void)key;
  (void)length;
}

static void expect_rejection(int operation) {
  pid_t child = fork();
  assert(child >= 0);
  if (!child) {
    daemon_pid = getpid();
    if (operation == 0)
      adbd_auth_register_service(&auth_context, "test", "_adb._tcp", 5555);
    else if (operation == 1)
      adbd_auth_unregister_service(&auth_context, "test", "_adb._tcp");
    else if (operation == 2) {
      struct auth_callbacks_v1 wrong = {3, authorized, removed};
      adbd_auth_new(&wrong);
    } else if (operation == 3)
      adbd_auth_register_service(NULL, "test", "_adb._tcp", 5555);
    else {
      auth_abi = TAILADB_AUTH_UNKNOWN;
      adbd_auth_get_max_version();
    }
    _exit(99);
  }
  int status;
  assert(waitpid(child, &status, 0) == child);
  assert(WIFEXITED(status) && WEXITSTATUS(status) == 123);
}

int main(void) {
  configured = true;
  daemon_pid = getpid();
  for (int i = TAILADB_AUTH_ANDROID16; i <= TAILADB_AUTH_ANDROID17; i++) {
    auth_abi = (enum tailadb_auth_abi)i;
    assert(adbd_auth_get_max_version() == 1);
    assert(!adbd_auth_supports_feature(0));
    struct auth_callbacks_v1 callbacks = {1, authorized, removed};
    void *context = adbd_auth_new(&callbacks);
    assert(context == &auth_context);

    active_peer = inet_addr("100.64.0.11");
    allowed_shell_count = 1;
    allowed_shell[0].s_addr = inet_addr("100.64.0.12");
    token_valid = false;
    approvals = 0;
    uint64_t first = adbd_auth_prompt_user_with_id(context, "test-key", 8, &approvals);
    assert(first > 0 && approvals == 0);
    token_valid = true;
    assert(adbd_auth_prompt_user_with_id(context, "test-key", 8, &approvals) > first);
    assert(approvals == 0); // A valid session is insufficient without the allowed host.
    allowed_shell[0].s_addr = active_peer;
    adbd_auth_prompt_user_with_id(context, "test-key", 8, &approvals);
    assert(approvals == 1);
    token_valid = false;
    adbd_auth_prompt_user_with_id(context, "test-key", 8, &approvals);
    assert(approvals == 1); // Revoking the session stops subsequent approvals.

    assert(adbd_auth_register_service(context, NULL, "_adb._tcp", 5555) == 1);
    assert(adbd_auth_unregister_service(context, "test", NULL) == 1);
    for (int operation = 0; operation <= 4; operation++)
      expect_rejection(operation);
  }
  configured = false;
  puts(
      "Auth policy integration: PASS (real exports, both ABIs, host/session gating, mDNS blocked)");
  return 0;
}
