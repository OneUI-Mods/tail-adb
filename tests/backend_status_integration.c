// SPDX-License-Identifier: MIT
// Run only in a disposable Linux host environment as root. This exercises the
// actual supervisor status/start code, without launching an Android daemon.
#define main supervisor_main
#include "../native/supervisor.c"
#undef main
#include <assert.h>

static const char *test_directory = "/data/local/tmp/net.ryuya_dev.tailadb";

static void record(const char *name, const char *value) {
  char path[PATH_MAX];
  snprintf(path, sizeof(path), "%s/%s", test_directory, name);
  int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0600);
  assert(fd >= 0);
  assert(write(fd, value, strlen(value)) == (ssize_t)strlen(value));
  assert(!close(fd));
}

static void read_status(char *out, size_t size) {
  int channel[2];
  assert(!pipe(channel));
  fflush(stdout);
  int saved = dup(STDOUT_FILENO);
  assert(saved >= 0 && dup2(channel[1], STDOUT_FILENO) >= 0);
  close(channel[1]);
  assert(!status_backend(test_directory));
  fflush(stdout);
  assert(dup2(saved, STDOUT_FILENO) >= 0);
  close(saved);
  ssize_t length = read(channel[0], out, size - 1);
  assert(length > 0 && length < (ssize_t)size - 1);
  out[length] = 0;
  close(channel[0]);
}

static void start_fails(int expected) {
  pid_t child = fork();
  assert(child >= 0);
  if (!child) {
    int null = open("/dev/null", O_WRONLY);
    assert(null >= 0);
    assert(dup2(null, STDOUT_FILENO) >= 0 && dup2(null, STDERR_FILENO) >= 0);
    close(null);
    char *args[] = {"tailadb-test",
                    "/unused/policy",
                    "/unused/su",
                    (char *)test_directory,
                    "5555",
                    "0",
                    "",
                    "",
                    NULL};
    _exit(supervisor_main(8, args));
  }
  int status;
  assert(waitpid(child, &status, 0) == child);
  assert(WIFEXITED(status) && WEXITSTATUS(status) == expected);
}

int main(void) {
  assert(getuid() == 0);
  // An existing directory could belong to a real backend. Never modify it.
  assert(access(test_directory, F_OK) && errno == ENOENT);
  assert(access("/system/bin/sha256sum", F_OK) && errno == ENOENT);
  bool made_data = !mkdir("/data", 0755);
  bool made_local = !mkdir("/data/local", 0755);
  bool made_tmp = !mkdir("/data/local/tmp", 0755);
  assert(!mkdir(test_directory, 0711));
  umask(077);
  char output[2048];

  // The real startup path must persist compatibility failure after owning lock.
  start_fails(1);
  read_status(output, sizeof(output));
  assert(!strcmp(output, "TAILADB\tERROR_COMPAT\t\n"));

  record("status", "TAILADB\tWAITING_TAILSCALE\t0\n");
  record("peer", "TAILADB\tPEER\t100.64.0.11\t1\n");
  read_status(output, sizeof(output));
  assert(!strcmp(output, "TAILADB\tSTOPPED\t\n"));

  int ready[2];
  assert(!pipe(ready));
  pid_t owner = fork();
  assert(owner >= 0);
  if (!owner) {
    close(ready[0]);
    signal(SIGTERM, SIG_IGN); // Simulate the legacy backend stuck in Binder inspection.
    char path[PATH_MAX], pid[32];
    snprintf(path, sizeof(path), "%s/lock", test_directory);
    int lock = open(path, O_CREAT | O_RDWR, 0600);
    assert(lock >= 0 && !flock(lock, LOCK_EX | LOCK_NB));
    snprintf(pid, sizeof(pid), "%ld\n", (long)getpid());
    record("pid", pid);
    record("status", "TAILADB\tLISTENING\t100.64.0.10:5555\n");
    assert(write(ready[1], "1", 1) == 1);
    close(ready[1]);
    for (;;)
      pause();
  }
  close(ready[1]);
  char byte;
  assert(read(ready[0], &byte, 1) == 1);
  close(ready[0]);
  read_status(output, sizeof(output));
  assert(strstr(output, "TAILADB\tLISTENING\t"));
  assert(strstr(output, "TAILADB\tPEER\t"));

  // A live backend waiting for its VPN must not replay an earlier peer either.
  record("status", "TAILADB\tWAITING_TAILSCALE\t0\n");
  read_status(output, sizeof(output));
  assert(!strcmp(output, "TAILADB\tWAITING_TAILSCALE\t0\n"));
  record("status", "TAILADB\tLISTENING\t100.64.0.10:5555\n");

  // A competing start must not overwrite the running backend's state or peer.
  start_fails(3);
  read_status(output, sizeof(output));
  assert(strstr(output, "TAILADB\tLISTENING\t"));
  assert(strstr(output, "TAILADB\tPEER\t"));

  // A live but unrelated/reused PID does not own the held lock descriptor.
  char pid[32];
  snprintf(pid, sizeof(pid), "%ld\n", (long)getpid());
  record("pid", pid);
  read_status(output, sizeof(output));
  assert(!strcmp(output, "TAILADB\tSTOPPED\t\n"));
  assert(stop_backend(test_directory) == 1); // Do not signal an unrelated/reused PID.
  snprintf(pid, sizeof(pid), "%ld\n", (long)owner);
  record("pid", pid);
  assert(!stop_backend(test_directory));
  int owner_status = 0;
  assert(waitpid(owner, &owner_status, 0) == owner);
  assert(WIFSIGNALED(owner_status) && WTERMSIG(owner_status) == SIGKILL);
  read_status(output, sizeof(output));
  assert(!strcmp(output, "TAILADB\tSTOPPED\t\n"));

  record("status", "TAILADB\tERROR_CRASH_LOOP\t3\n");
  read_status(output, sizeof(output));
  assert(!strcmp(output, "TAILADB\tERROR_CRASH_LOOP\t3\n"));

  char path[PATH_MAX];
  snprintf(path, sizeof(path), "%s/status", test_directory);
  assert(!unlink(path) && !symlink("/etc/passwd", path));
  read_status(output, sizeof(output));
  assert(!strcmp(output, "TAILADB\tSTOPPED\t\n"));

  const char *files[] = {"status", "peer", "pid", "lock"};
  for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
    snprintf(path, sizeof(path), "%s/%s", test_directory, files[i]);
    assert(!unlink(path));
  }
  assert(!rmdir(test_directory));
  if (made_tmp)
    assert(!rmdir("/data/local/tmp"));
  if (made_local)
    assert(!rmdir("/data/local"));
  if (made_data)
    assert(!rmdir("/data"));
  puts("Backend status integration: PASS (real startup, lock owner, stale PID, peer, symlink)");
  return 0;
}
