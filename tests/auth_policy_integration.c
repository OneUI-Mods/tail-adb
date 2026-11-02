// SPDX-License-Identifier: MIT
// Compile the real interposed functions against host-only Android header shims.
// Suppress only the automatic constructor: this test initializes the private
// session explicitly and never opens an Android listener or invokes root code.
#define TAILADB_HOST_AUTH_TEST 1
#define constructor unused
#include "../native/policy.c"
#undef constructor
#include <assert.h>
#include <grp.h>
#include <sys/wait.h>

static unsigned approvals;
static unsigned delivered_keys;

static bool receive_key(void *opaque, const char *key, size_t length) {
  assert(opaque == &delivered_keys);
  assert(length == 707 && !memcmp(key + 700, " test-", 6));
  assert(key[706] == (delivered_keys ? 'b' : 'a'));
  delivered_keys++;
  return true;
}

static void check_key_snapshot(void) {
  // This suite runs only in a disposable root Linux container. Never replace an
  // existing Android key directory, even if the test is invoked incorrectly.
  assert(getuid() == 0 && access("/system/bin/sha256sum", F_OK) != 0);
  assert(!mkdir("/data", 0755) || errno == EEXIST);
  assert(!mkdir("/data/misc", 0755) || errno == EEXIST);
  assert(!mkdir("/data/misc/adb", 0750));
  assert(!chown("/data/misc/adb", 1000, 2000));
  int fd = open("/data/misc/adb/adb_keys", O_CREAT | O_EXCL | O_WRONLY, 0640);
  assert(fd >= 0 && !fchown(fd, 1000, 2000));
  char line[708];
  for (char suffix = 'a'; suffix <= 'b'; suffix++) {
    memset(line, suffix, 700);
    memcpy(line + 700, " test-", 6);
    line[706] = suffix;
    line[707] = '\n';
    assert(write(fd, line, sizeof(line)) == sizeof(line));
  }
  assert(!close(fd));
  assert(load_authorized_keys(&authorized_keys, &authorized_keys_length));
  assert(authorized_keys_length == 2 * sizeof(line));

  // Reproduce an auth thread that cannot open the key directory after dropping
  // privilege. Both Android ABIs must still deliver the verified startup copy.
  assert(!chmod("/data/misc/adb", 0700));
  pid_t child = fork();
  assert(child >= 0);
  if (!child) {
    assert(!setgroups(0, NULL) && !setgid(2000) && !setuid(2000));
    assert(open("/data/misc/adb", O_RDONLY | O_DIRECTORY) == -1 && errno == EACCES);
    char *blocked_keys = NULL;
    size_t blocked_length = 0;
    assert(!load_authorized_keys(&blocked_keys, &blocked_length));
    daemon_pid = getpid();
    for (int abi = TAILADB_AUTH_ANDROID16; abi <= TAILADB_AUTH_ANDROID17; abi++) {
      auth_abi = (enum tailadb_auth_abi)abi;
      delivered_keys = 0;
      adbd_auth_get_public_keys(&auth_context, receive_key, &delivered_keys);
      assert(delivered_keys == 2);
    }
    _exit(0);
  }
  int status;
  assert(waitpid(child, &status, 0) == child);
  assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
  free(authorized_keys);
  authorized_keys = NULL;
  authorized_keys_length = 0;

  // A new backend must re-read the current file; removed keys are not cached
  // across restarts. Symlinked and incorrectly owned files remain rejected.
  assert(!chmod("/data/misc/adb", 0750));
  assert(!unlink("/data/misc/adb/adb_keys"));
  assert(!load_authorized_keys(&authorized_keys, &authorized_keys_length));
  assert(!authorized_keys && !authorized_keys_length);
  assert(!symlink("/etc/passwd", "/data/misc/adb/adb_keys"));
  assert(!load_authorized_keys(&authorized_keys, &authorized_keys_length));
  assert(!unlink("/data/misc/adb/adb_keys"));
  fd = open("/data/misc/adb/adb_keys", O_CREAT | O_EXCL | O_WRONLY, 0640);
  assert(fd >= 0 && write(fd, line, sizeof(line)) == sizeof(line) && !close(fd));
  assert(!load_authorized_keys(&authorized_keys, &authorized_keys_length));
  assert(!unlink("/data/misc/adb/adb_keys") && !rmdir("/data/misc/adb"));
}

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
  check_key_snapshot();
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
      "Auth policy integration: PASS (startup key snapshot after UID drop, both ABIs, host/session gating, mDNS blocked)");
  return 0;
}
