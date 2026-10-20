// SPDX-License-Identifier: MIT
// Copyright (c) 2026 ryuya0124

#define _GNU_SOURCE
#include <android/log.h>
#include <android/multinetwork.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <limits.h>
#include <net/if.h>
#include <poll.h>
#include <sched.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/inotify.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <sys/random.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "daemon_profiles.h"
#include "status_record.h"

#define TOKEN_BYTES 32
#define MAX_REQUEST 4096
#define MAX_ALLOWED 32
#define MAX_VPN_CANDIDATES 8
#define LOG_TAG "TailAdbSupervisor"
#define BROKER_WARN(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

enum control_type {
  CONTROL_READY = 1,
  CONTROL_CONNECTED = 2,
  CONTROL_CLOSED = 3,
  CONTROL_BROKER = 4,
  CONTROL_FAILURE = 5
};

struct control_message {
  uint32_t type;
  uint32_t peer;
  uint8_t token[TOKEN_BYTES];
};

struct request {
  uint8_t token[TOKEN_BYTES];
  uint32_t argc;
  uint32_t bytes;
  char arguments[];
};

struct authorization {
  struct in_addr addresses[MAX_ALLOWED];
  size_t count;
};

struct vpn {
  char iface[IFNAMSIZ];
  char ip[INET_ADDRSTRLEN];
  uint64_t handle;
  unsigned index;
  unsigned uid;
};

struct vpn_scan {
  struct vpn candidates[MAX_VPN_CANDIDATES];
  size_t count;
};

enum vpn_result { VPN_UNAVAILABLE, VPN_READY, VPN_OTHER_USER, VPN_AMBIGUOUS, VPN_INSPECTION_ERROR };

static volatile sig_atomic_t stopped;
static bool terminal_error;
static char status_path[PATH_MAX];
static char peer_path[PATH_MAX];

static void secure_zero(void *memory, size_t length) {
  volatile uint8_t *bytes = memory;
  while (length--)
    *bytes++ = 0;
}

static void stop_signal(int signal) {
  (void)signal;
  stopped = 1;
}

static void event(const char *state, const char *detail) {
  if (!strncmp(state, "ERROR_", 6))
    terminal_error = true;
  printf("TAILADB\t%s\t%s\n", state, detail ? detail : "");
  fflush(stdout);
  if (!*status_path)
    return;
  const char *target = status_path;
  if (!strcmp(state, "PEER") || !strcmp(state, "PEER_CLOSED"))
    target = peer_path;
  char temporary[PATH_MAX];
  if (snprintf(temporary, sizeof(temporary), "%s.tmp", target) >= (int)sizeof(temporary))
    return;
  int fd = open(temporary, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0600);
  if (fd < 0)
    return;
  dprintf(fd, "TAILADB\t%s\t%s\n", state, detail ? detail : "");
  bool failed = fsync(fd) != 0;
  if (close(fd))
    failed = true;
  if (!failed && rename(temporary, target))
    failed = true;
  if (failed)
    unlink(temporary);
}

static bool safe_iface(const char *name) {
  if (!*name || strlen(name) >= IFNAMSIZ)
    return false;
  for (const char *p = name; *p; p++)
    if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') ||
          *p == '_' || *p == '.' || *p == '-'))
      return false;
  return true;
}

static bool package_uid(unsigned uid) {
  char command[160], line[512];
  snprintf(command, sizeof(command),
           "/system/bin/cmd package list packages -U --user %u com.tailscale.ipn", uid / 100000);
  FILE *f = popen(command, "r");
  if (!f)
    return false;
  bool good = false;
  while (fgets(line, sizeof(line), f)) {
    unsigned read_uid = 0;
    char pkg[200] = {0};
    if (sscanf(line, "package:%199s uid:%u", pkg, &read_uid) == 2 &&
        !strcmp(pkg, "com.tailscale.ipn") && read_uid == uid)
      good = true;
  }
  return pclose(f) == 0 && good;
}

static bool same_vpn(const struct vpn *left, const struct vpn *right) {
  return left->handle == right->handle && left->uid == right->uid && left->index == right->index &&
         !strcmp(left->ip, right->ip) && !strcmp(left->iface, right->iface);
}

static void vpn_detail(const struct vpn_scan *scan, unsigned user_id, char *detail, size_t size) {
  int used = snprintf(detail, size, "%u", user_id);
  if (used < 0 || (size_t)used >= size)
    return;
  for (size_t i = 0; i < scan->count; i++) {
    const struct vpn *v = &scan->candidates[i];
    int written = snprintf(detail + used, size - (size_t)used, "\t%u,%s,%s", v->uid / 100000, v->ip,
                           v->iface);
    if (written < 0 || (size_t)written >= size - (size_t)used)
      return;
    used += written;
  }
}

static enum vpn_result vpn_info(struct vpn *out, unsigned user_id, struct vpn_scan *scan) {
  FILE *f = popen("/system/bin/dumpsys connectivity", "r");
  if (!f)
    return VPN_INSPECTION_ERROR;
  char *line = NULL;
  size_t cap = 0;
  int matches = 0;
  struct vpn candidate = {0};
  while (getline(&line, &cap, f) > 0) {
    if (!strstr(line, "NetworkAgentInfo{") ||
        !strstr(line, "VPN CONNECTED extra: VPN:com.tailscale.ipn}"))
      continue;
    char *iface = strstr(line, "InterfaceName: "), *ips = strstr(line, "LinkAddresses: [ "),
         *handle = strstr(line, "handle{"), *owner = strstr(line, "OwnerUid: ");
    if (!iface || !ips || !handle || !owner)
      continue;
    struct vpn v = {0};
    unsigned long long h = 0;
    if (sscanf(iface, "InterfaceName: %15s", v.iface) != 1 ||
        sscanf(handle, "handle{%llu}", &h) != 1 || sscanf(owner, "OwnerUid: %u", &v.uid) != 1)
      continue;
    if (!package_uid(v.uid))
      continue;
    // Only a Tailscale CGNAT /32 address in the LinkAddresses list is accepted.
    const char *p = ips + strlen("LinkAddresses: [ ");
    bool found = false;
    while (*p && *p != ']') {
      if ((*p >= '0' && *p <= '9') &&
          (p == ips + strlen("LinkAddresses: [ ") || p[-1] == ',' || p[-1] == ' ')) {
        char ip[INET_ADDRSTRLEN] = {0};
        unsigned prefix = 0;
        int n = 0;
        if (sscanf(p, "%15[0-9.]/%u%n", ip, &prefix, &n) == 2 && n > 0 && prefix == 32) {
          struct in_addr a;
          if (inet_pton(AF_INET, ip, &a) == 1 && (ntohl(a.s_addr) & 0xffc00000) == 0x64400000) {
            strcpy(v.ip, ip);
            found = true;
            break;
          }
        }
      }
      p++;
    }
    if (!found || !safe_iface(v.iface) || !(v.index = if_nametoindex(v.iface)))
      continue;
    v.handle = h;
    bool duplicate = false;
    for (size_t i = 0; i < scan->count; i++)
      if (same_vpn(&scan->candidates[i], &v))
        duplicate = true;
    if (duplicate)
      continue;
    if (scan->count >= MAX_VPN_CANDIDATES) {
      free(line);
      pclose(f);
      return VPN_INSPECTION_ERROR;
    }
    scan->candidates[scan->count++] = v;
    // dumpsys includes VPNs from every running Android user/profile. Only the
    // user selected in Tail ADB may supply the listener network.
    if (v.uid / 100000 != user_id)
      continue;
    candidate = v;
    matches++;
  }
  free(line);
  int rc = pclose(f);
  if (rc)
    return VPN_INSPECTION_ERROR;
  if (!matches)
    return scan->count ? VPN_OTHER_USER : VPN_UNAVAILABLE;
  if (matches != 1)
    return VPN_AMBIGUOUS;
  struct ifaddrs *all = NULL;
  if (getifaddrs(&all))
    return VPN_INSPECTION_ERROR;
  bool exists = false;
  for (struct ifaddrs *i = all; i; i = i->ifa_next)
    if (i->ifa_addr && i->ifa_addr->sa_family == AF_INET && !strcmp(i->ifa_name, candidate.iface) &&
        (i->ifa_flags & IFF_UP)) {
      char ip[INET_ADDRSTRLEN];
      inet_ntop(AF_INET, &((struct sockaddr_in *)i->ifa_addr)->sin_addr, ip, sizeof(ip));
      if (!strcmp(ip, candidate.ip))
        exists = true;
    }
  freeifaddrs(all);
  if (!exists)
    return VPN_INSPECTION_ERROR;
  *out = candidate;
  return VPN_READY;
}

static const struct tailadb_daemon_profile *supported_daemon(void) {
  FILE *f = popen("/system/bin/sha256sum /apex/com.android.adbd/bin/adbd "
                  "/system/lib64/libadbd_auth.so",
                  "r");
  if (!f)
    return NULL;
  char adbd[65] = {0}, auth[65] = {0}, path[PATH_MAX] = {0};
  bool good = fscanf(f, "%64s %1023s", adbd, path) == 2 &&
              !strcmp(path, "/apex/com.android.adbd/bin/adbd") &&
              fscanf(f, "%64s %1023s", auth, path) == 2 &&
              !strcmp(path, "/system/lib64/libadbd_auth.so");
  return pclose(f) == 0 && good ? tailadb_daemon_profile(adbd, auth) : NULL;
}

static int copy_daemon(const char *destination) {
  const char *source = "/apex/com.android.adbd/bin/adbd";
  int in = open(source, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
  if (in < 0)
    return -1;
  struct stat st;
  if (fstat(in, &st) || !S_ISREG(st.st_mode) || st.st_size < 100000 || st.st_size > 20000000) {
    close(in);
    errno = EINVAL;
    return -1;
  }
  int out = open(destination, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0700);
  if (out < 0) {
    close(in);
    return -1;
  }
  char buffer[65536];
  ssize_t got;
  bool good = true;
  while ((got = read(in, buffer, sizeof(buffer))) > 0) {
    ssize_t done = 0;
    while (done < got) {
      ssize_t n = write(out, buffer + done, got - done);
      if (n <= 0) {
        good = false;
        break;
      }
      done += n;
    }
    if (!good)
      break;
  }
  if (got < 0 || fsync(out) || fchmod(out, 0700))
    good = false;
  close(in);
  close(out);
  return good ? 0 : -1;
}

static int copy_helper(const char *source, const char *destination) {
  int in = open(source, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
  if (in < 0)
    return -1;
  struct stat st;
  if (fstat(in, &st) || !S_ISREG(st.st_mode) || st.st_size < 1000 || st.st_size > 2000000) {
    close(in);
    errno = EINVAL;
    return -1;
  }
  int out = open(destination, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0755);
  if (out < 0) {
    close(in);
    return -1;
  }
  char buffer[65536];
  ssize_t got;
  bool good = true;
  while ((got = read(in, buffer, sizeof(buffer))) > 0) {
    ssize_t done = 0;
    while (done < got) {
      ssize_t n = write(out, buffer + done, (size_t)(got - done));
      if (n <= 0) {
        good = false;
        break;
      }
      done += n;
    }
    if (!good)
      break;
  }
  if (got < 0 || fsync(out) || fchmod(out, 0755))
    good = false;
  close(in);
  close(out);
  return good ? 0 : -1;
}

static bool parse_authorization(const char *text, struct authorization *out) {
  if (strlen(text) > 1024)
    return false;
  char copy[1025];
  strcpy(copy, text);
  if (!*copy)
    return true;
  char *save = NULL;
  for (char *item = strtok_r(copy, ",", &save); item; item = strtok_r(NULL, ",", &save)) {
    if (out->count >= MAX_ALLOWED || inet_pton(AF_INET, item, &out->addresses[out->count]) != 1 ||
        (ntohl(out->addresses[out->count].s_addr) & 0xffc00000U) != 0x64400000U)
      return false;
    for (size_t i = 0; i < out->count; i++)
      if (out->addresses[i].s_addr == out->addresses[out->count].s_addr)
        return false;
    out->count++;
  }
  return true;
}

static bool host_allowed(const struct authorization *authorization, uint32_t peer) {
  for (size_t i = 0; i < authorization->count; i++)
    if (authorization->addresses[i].s_addr == peer)
      return true;
  return false;
}

static bool set_socket_creation_context(const char *context) {
  int fd = open("/proc/thread-self/attr/sockcreate", O_WRONLY | O_CLOEXEC | O_NOFOLLOW);
  if (fd < 0)
    return false;
  size_t size = context ? strlen(context) + 1 : 1;
  const char empty = 0;
  const char *value = context ? context : &empty;
  ssize_t written;
  do {
    written = write(fd, value, size);
  } while (written < 0 && errno == EINTR);
  int saved = errno;
  close(fd);
  errno = saved;
  return written == (ssize_t)size;
}

static int enter_clean_mount_namespace(void) {
  int initial = open("/proc/1/ns/mnt", O_RDONLY | O_CLOEXEC);
  if (initial < 0)
    return 1;
  if (setns(initial, CLONE_NEWNS)) {
    close(initial);
    return 2;
  }
  close(initial);
  if (unshare(CLONE_NEWNS))
    return 3;
  if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL))
    return 4;
  return 0;
}

static int secure_listener(const struct vpn *v, int port) {
  if (!set_socket_creation_context("u:r:adbd:s0"))
    return -1;
  int fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
  int socket_error = errno;
  if (!set_socket_creation_context(NULL)) {
    if (fd >= 0)
      close(fd);
    return -1;
  }
  errno = socket_error;
  if (fd < 0)
    return -1;
  int reuse = 1;
  if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse))) {
    close(fd);
    return -1;
  }
  if (setsockopt(fd, SOL_SOCKET, SO_BINDTODEVICE, v->iface, strlen(v->iface) + 1) ||
      android_setsocknetwork(v->handle, fd)) {
    close(fd);
    return -1;
  }
  struct sockaddr_in a = {.sin_family = AF_INET, .sin_port = htons(port)};
  inet_pton(AF_INET, v->ip, &a.sin_addr);
  if (bind(fd, (struct sockaddr *)&a, sizeof(a)) || listen(fd, 4)) {
    close(fd);
    return -1;
  }
  char iface[IFNAMSIZ] = {0};
  socklen_t size = sizeof(iface);
  if (getsockopt(fd, SOL_SOCKET, SO_BINDTODEVICE, iface, &size) || strcmp(iface, v->iface)) {
    close(fd);
    errno = EACCES;
    return -1;
  }
  return fd;
}

static void close_unrelated(int keep1, int keep2) {
  DIR *d = opendir("/proc/self/fd");
  if (!d)
    _exit(124);
  int own = dirfd(d);
  struct dirent *e;
  while ((e = readdir(d))) {
    char *end;
    long fd = strtol(e->d_name, &end, 10);
    if (*end || fd <= 2 || fd == own || fd == keep1 || fd == keep2)
      continue;
    close((int)fd);
  }
  closedir(d);
}

static void reap(pid_t child) {
  if (child <= 0)
    return;
  kill(child, SIGTERM);
  for (int i = 0; i < 20; i++) {
    if (waitpid(child, NULL, WNOHANG) == child)
      return;
    usleep(50000);
  }
  kill(child, SIGKILL);
  waitpid(child, NULL, 0);
}

static bool child_ready(pid_t child, pid_t parent, int listener, const char *binary) {
  char path[PATH_MAX], data[4096] = {0};
  snprintf(path, sizeof(path), "/proc/%ld/status", (long)child);
  int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
  ssize_t length = fd >= 0 ? read(fd, data, sizeof(data) - 1) : -1;
  if (fd >= 0)
    close(fd);
  char parent_line[64];
  snprintf(parent_line, sizeof(parent_line), "\nPPid:\t%ld\n", (long)parent);
  if (length <= 0 || !strstr(data, "\nUid:\t2000\t2000\t2000\t2000\n") ||
      !strstr(data, "\nGid:\t2000\t2000\t2000\t2000\n") ||
      !strstr(data, "\nCapInh:\t0000000000000000\n") ||
      !strstr(data, "\nCapPrm:\t0000000000000000\n") ||
      !strstr(data, "\nCapEff:\t0000000000000000\n") || !strstr(data, parent_line))
    return false;
  snprintf(path, sizeof(path), "/proc/%ld/attr/current", (long)child);
  fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
  memset(data, 0, sizeof(data));
  length = fd >= 0 ? read(fd, data, sizeof(data) - 1) : -1;
  if (fd >= 0)
    close(fd);
  if (length <= 0 || strcmp(data, "u:r:adbd:s0"))
    return false;
  snprintf(path, sizeof(path), "/proc/%ld/exe", (long)child);
  char executable[PATH_MAX] = {0};
  length = readlink(path, executable, sizeof(executable) - 1);
  if (length <= 0 || strcmp(executable, binary))
    return false;
  struct stat expected;
  if (fstat(listener, &expected) || !S_ISSOCK(expected.st_mode))
    return false;
  snprintf(path, sizeof(path), "/proc/%ld/fd", (long)child);
  DIR *directory = opendir(path);
  if (!directory)
    return false;
  bool found = false;
  struct dirent *entry;
  while ((entry = readdir(directory))) {
    char *end = NULL;
    strtol(entry->d_name, &end, 10);
    if (*end)
      continue;
    char descriptor[PATH_MAX];
    if (snprintf(descriptor, sizeof(descriptor), "%s/%s", path, entry->d_name) >=
        (int)sizeof(descriptor))
      continue;
    struct stat actual;
    if (!stat(descriptor, &actual) && actual.st_dev == expected.st_dev &&
        actual.st_ino == expected.st_ino && S_ISSOCK(actual.st_mode))
      found = true;
  }
  closedir(directory);
  return found;
}

static void close_except(const int *keep, size_t count) {
  DIR *directory = opendir("/proc/self/fd");
  if (!directory)
    _exit(124);
  int own = dirfd(directory);
  struct dirent *entry;
  while ((entry = readdir(directory))) {
    char *end = NULL;
    long fd = strtol(entry->d_name, &end, 10);
    if (*end || fd <= 2 || fd == own)
      continue;
    bool retained = false;
    for (size_t i = 0; i < count; i++)
      if (fd == keep[i])
        retained = true;
    if (!retained)
      close((int)fd);
  }
  closedir(directory);
}

static bool constant_equal(const uint8_t *left, const uint8_t *right, size_t length) {
  uint8_t different = 0;
  for (size_t i = 0; i < length; i++)
    different |= left[i] ^ right[i];
  return different == 0;
}

static void run_privileged(int client, int streams[3], struct request *request) {
  char *arguments[130] = {0};
  char *position = request->arguments;
  char *limit = request->arguments + request->bytes;
  for (uint32_t i = 0; i < request->argc; i++) {
    if (position >= limit) {
      int status = 1 << 8;
      send(client, &status, sizeof(status), MSG_NOSIGNAL);
      goto finished;
    }
    size_t remaining = (size_t)(limit - position);
    size_t length = strnlen(position, remaining);
    if (length == remaining) {
      int status = 1 << 8;
      send(client, &status, sizeof(status), MSG_NOSIGNAL);
      goto finished;
    }
    arguments[i] = position;
    position += length + 1;
  }
  if (position != limit) {
    int status = 1 << 8;
    send(client, &status, sizeof(status), MSG_NOSIGNAL);
    goto finished;
  }

  uint32_t first = 0;
  if (request->argc && (!strcmp(arguments[0], "0") || !strcmp(arguments[0], "root")))
    first++;
  char *command[132] = {0};
  if (first == request->argc) {
    command[0] = "/system/bin/sh";
    command[1] = "-i";
  } else if (!strcmp(arguments[first], "-c")) {
    if (first + 1 >= request->argc) {
      int status = 2 << 8;
      send(client, &status, sizeof(status), MSG_NOSIGNAL);
      goto finished;
    }
    command[0] = "/system/bin/sh";
    command[1] = "-c";
    for (uint32_t i = first + 1; i < request->argc; i++)
      command[i - first + 1] = arguments[i];
  } else {
    for (uint32_t i = first; i < request->argc; i++)
      command[i - first] = arguments[i];
  }

  pid_t child = fork();
  if (child == 0) {
    for (int i = 0; i < 3; i++)
      if (dup2(streams[i], i) < 0)
        _exit(126);
    // App data isolation can remain in the app-launched broker's mount namespace.
    // Only an already-authorized root command gets a private view of init's mounts;
    // keep the broker, the unprivileged daemon, and all other processes unchanged.
    if (enter_clean_mount_namespace()) {
      dprintf(STDERR_FILENO, "su: system mount namespace unavailable\n");
      _exit(126);
    }
    close_except(NULL, 0);
    clearenv();
    setenv("HOME", "/data/local/tmp", 1);
    setenv("USER", "root", 1);
    setenv("LOGNAME", "root", 1);
    setenv("SHELL", "/system/bin/sh", 1);
    setenv(
        "PATH",
        "/product/bin:/apex/com.android.runtime/bin:/apex/com.android.art/bin:/system_ext/bin:/system/bin",
        1);
    chdir("/");
    execvp(command[0], command);
    dprintf(STDERR_FILENO, "su: command failed: %s\n", strerror(errno));
    _exit(errno == ENOENT ? 127 : 126);
  }
  for (int i = 0; i < 3; i++)
    close(streams[i]);
  int status = 1 << 8;
  if (child > 0) {
    while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
    }
  }
  send(client, &status, sizeof(status), MSG_NOSIGNAL);
  return;

finished:
  for (int i = 0; i < 3; i++)
    if (streams[i] >= 0)
      close(streams[i]);
}

static bool shell_process(pid_t pid) {
  if (pid <= 1)
    return false;
  char path[64];
  snprintf(path, sizeof(path), "/proc/%ld/attr/current", (long)pid);
  int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
  if (fd < 0)
    return false;
  char context[64] = {0};
  ssize_t size = read(fd, context, sizeof(context) - 1);
  close(fd);
  while (size > 0 && (context[size - 1] == '\n' || context[size - 1] == 0))
    context[--size] = 0;
  return size == (ssize_t)strlen("u:r:shell:s0") && !strcmp(context, "u:r:shell:s0");
}

static void broker_session(int client, bool allowed, const uint8_t token[TOKEN_BYTES]) {
  int pass_credentials = 1;
  if (setsockopt(client, SOL_SOCKET, SO_PASSCRED, &pass_credentials, sizeof(pass_credentials)))
    _exit(1);
  for (;;) {
    uint8_t buffer[sizeof(struct request) + MAX_REQUEST] = {0};
    char control[CMSG_SPACE(sizeof(struct ucred)) + CMSG_SPACE(sizeof(int) * 3)] = {0};
    struct iovec iov = {.iov_base = buffer, .iov_len = sizeof(buffer)};
    struct msghdr message = {.msg_iov = &iov,
                             .msg_iovlen = 1,
                             .msg_control = control,
                             .msg_controllen = sizeof(control)};
    ssize_t received;
    do {
      received = recvmsg(client, &message, MSG_CMSG_CLOEXEC);
    } while (received < 0 && errno == EINTR);
    if (received <= 0)
      break;
    int streams[3] = {-1, -1, -1};
    struct ucred credentials = {0};
    bool have_streams = false, have_credentials = false, extra = false;
    for (struct cmsghdr *header = CMSG_FIRSTHDR(&message); header;
         header = CMSG_NXTHDR(&message, header)) {
      if (header->cmsg_level != SOL_SOCKET) {
        extra = true;
      } else if (header->cmsg_type == SCM_RIGHTS && header->cmsg_len == CMSG_LEN(sizeof(streams)) &&
                 !have_streams) {
        memcpy(streams, CMSG_DATA(header), sizeof(streams));
        have_streams = true;
      } else if (header->cmsg_type == SCM_CREDENTIALS &&
                 header->cmsg_len == CMSG_LEN(sizeof(credentials)) && !have_credentials) {
        memcpy(&credentials, CMSG_DATA(header), sizeof(credentials));
        have_credentials = true;
      } else {
        extra = true;
      }
    }
    struct request *request = (struct request *)buffer;
    bool shell_context = have_credentials && shell_process(credentials.pid);
    bool trusted =
        allowed && !(message.msg_flags & (MSG_CTRUNC | MSG_TRUNC)) && !extra && have_streams &&
        have_credentials && credentials.uid == 2000 && credentials.gid == 2000 && shell_context &&
        received >= (ssize_t)sizeof(*request) && request->bytes <= MAX_REQUEST &&
        request->argc <= 128 && received == (ssize_t)(sizeof(*request) + request->bytes) &&
        constant_equal(request->token, token, TOKEN_BYTES);
    for (int i = 0; i < 3; i++)
      if (streams[i] < 0 || fcntl(streams[i], F_GETFD) < 0)
        trusted = false;
    if (!trusted) {
      BROKER_WARN("root request denied policy=%d trunc=%d extra=%d streams=%d "
                  "credentials=%d uid=%u gid=%u shell_context=%d size=%zd argc=%u "
                  "bytes=%u",
                  allowed, !!(message.msg_flags & (MSG_CTRUNC | MSG_TRUNC)), extra, have_streams,
                  have_credentials, credentials.uid, credentials.gid, shell_context, received,
                  received >= (ssize_t)sizeof(*request) ? request->argc : 0,
                  received >= (ssize_t)sizeof(*request) ? request->bytes : 0);
      uint8_t denied = 0;
      send(client, &denied, 1, MSG_NOSIGNAL);
      for (int i = 0; i < 3; i++)
        if (streams[i] >= 0)
          close(streams[i]);
      continue;
    }
    uint8_t granted = 1;
    if (send(client, &granted, 1, MSG_NOSIGNAL) != 1) {
      for (int i = 0; i < 3; i++)
        close(streams[i]);
      break;
    }
    run_privileged(client, streams, request);
  }
  close(client);
  _exit(0);
}

static void start_broker_session(int client, bool allowed, const uint8_t token[TOKEN_BYTES]) {
  pid_t child = fork();
  if (child == 0) {
    pid_t monitor = fork();
    if (monitor < 0)
      _exit(1);
    if (monitor > 0)
      _exit(0);
    int keep[] = {client};
    close_except(keep, 1);
    broker_session(client, allowed, token);
  } else if (child > 0) {
    waitpid(child, NULL, 0);
  }
  close(client);
}

static bool safe_control_directory(const char *directory) {
  if (!directory || strcmp(directory, "/data/local/tmp/net.ryuya_dev.tailadb"))
    return false;
  struct stat st;
  return !lstat(directory, &st) && S_ISDIR(st.st_mode) && st.st_uid == 0 && !(st.st_mode & 066);
}

static bool process_has_file(pid_t pid, const char *path);

static bool read_control_record(const char *directory, const char *name, char *buffer,
                                size_t capacity) {
  char path[PATH_MAX];
  if (snprintf(path, sizeof(path), "%s/%s", directory, name) >= (int)sizeof(path))
    return false;
  int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
  if (fd < 0)
    return false;
  struct stat st;
  size_t used = 0;
  bool valid = !fstat(fd, &st) && S_ISREG(st.st_mode) && st.st_uid == 0 && !(st.st_mode & 0022) &&
               st.st_size > 0 && st.st_size < (off_t)capacity;
  while (valid && used < capacity - 1) {
    ssize_t count = read(fd, buffer + used, capacity - used - 1);
    if (!count)
      break;
    if (count < 0) {
      if (errno == EINTR)
        continue;
      valid = false;
      break;
    }
    used += (size_t)count;
  }
  close(fd);
  buffer[used] = 0;
  return valid && used == (size_t)st.st_size && !memchr(buffer, 0, used);
}

static bool backend_owns_lock(const char *directory) {
  char text[32], path[PATH_MAX];
  if (!read_control_record(directory, "pid", text, sizeof(text)))
    return false;
  char *end;
  errno = 0;
  long pid = strtol(text, &end, 10);
  if (errno || pid <= 1 || pid > INT_MAX || (*end && strcmp(end, "\n")))
    return false;
  snprintf(path, sizeof(path), "/proc/%ld/status", pid);
  int fd = open(path, O_RDONLY | O_CLOEXEC);
  char status[2048] = {0};
  ssize_t size = fd < 0 ? -1 : read(fd, status, sizeof(status) - 1);
  if (fd >= 0)
    close(fd);
  if (size <= 0 || !strstr(status, "\nUid:\t0\t0\t0\t0\n"))
    return false;
  snprintf(path, sizeof(path), "%s/lock", directory);
  fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
  if (fd < 0)
    return false;
  int locked = flock(fd, LOCK_EX | LOCK_NB);
  bool held = locked < 0 && (errno == EWOULDBLOCK || errno == EAGAIN);
  if (!locked)
    flock(fd, LOCK_UN);
  close(fd);
  return held && process_has_file((pid_t)pid, path) && !kill((pid_t)pid, 0);
}

static int status_backend(const char *directory) {
  if (!safe_control_directory(directory)) {
    event("STOPPED", "");
    return 0;
  }
  char buffer[1024];
  if (!read_control_record(directory, "status", buffer, sizeof(buffer))) {
    event("STOPPED", "");
    return 0;
  }
  enum tailadb_status_record kind = tailadb_status_record(buffer);
  if (kind == TAILADB_STATUS_INVALID ||
      (kind == TAILADB_STATUS_ACTIVE && !backend_owns_lock(directory))) {
    event("STOPPED", "");
    return 0;
  }
  bool listening = !strncmp(buffer, "TAILADB\tLISTENING\t", 18);
  fputs(buffer, stdout);
  if (listening && backend_owns_lock(directory) &&
      read_control_record(directory, "peer", buffer, sizeof(buffer)) &&
      (!strncmp(buffer, "TAILADB\tPEER\t", 13) || !strncmp(buffer, "TAILADB\tPEER_CLOSED\t", 20)) &&
      tailadb_status_record(buffer) != TAILADB_STATUS_INVALID)
    fputs(buffer, stdout);
  return 0;
}

static bool process_has_file(pid_t pid, const char *path) {
  struct stat expected;
  if (stat(path, &expected))
    return false;
  char directory_path[64];
  snprintf(directory_path, sizeof(directory_path), "/proc/%ld/fd", (long)pid);
  DIR *directory = opendir(directory_path);
  if (!directory)
    return false;
  bool found = false;
  struct dirent *entry;
  while (!found && (entry = readdir(directory))) {
    char *end = NULL;
    strtol(entry->d_name, &end, 10);
    if (*end)
      continue;
    char descriptor[96];
    if (snprintf(descriptor, sizeof(descriptor), "%s/%s", directory_path, entry->d_name) >=
        (int)sizeof(descriptor))
      continue;
    struct stat actual;
    if (!stat(descriptor, &actual) && actual.st_dev == expected.st_dev &&
        actual.st_ino == expected.st_ino)
      found = true;
  }
  closedir(directory);
  return found;
}

static int stop_backend(const char *directory) {
  if (!safe_control_directory(directory)) {
    event("STOPPED", "");
    return 0;
  }
  char path[PATH_MAX];
  snprintf(path, sizeof(path), "%s/pid", directory);
  int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
  char text[32] = {0};
  if (fd < 0 || read(fd, text, sizeof(text) - 1) <= 0) {
    if (fd >= 0)
      close(fd);
    event("STOPPED", "");
    return 0;
  }
  close(fd);
  char *end = NULL;
  long pid = strtol(text, &end, 10);
  if (!end || (*end && *end != '\n') || pid <= 1 || pid > INT_MAX) {
    event("ERROR_CONTROL", "");
    return 1;
  }
  char proc[64];
  snprintf(proc, sizeof(proc), "/proc/%ld/status", pid);
  fd = open(proc, O_RDONLY | O_CLOEXEC);
  char process_status[2048] = {0};
  ssize_t got = fd >= 0 ? read(fd, process_status, sizeof(process_status) - 1) : -1;
  if (fd >= 0)
    close(fd);
  char lock_path[PATH_MAX];
  snprintf(lock_path, sizeof(lock_path), "%s/lock", directory);
  if (got <= 0 || !strstr(process_status, "\nUid:\t0\t0\t0\t0\n") ||
      !process_has_file((pid_t)pid, lock_path) || kill((pid_t)pid, SIGTERM)) {
    event(errno == ESRCH ? "STOPPED" : "ERROR_CONTROL", "");
    return errno == ESRCH ? 0 : 1;
  }
  for (int i = 0; i < 30; i++) {
    if (kill((pid_t)pid, 0) && errno == ESRCH) {
      event("STOPPED", "");
      return 0;
    }
    usleep(100000);
  }
  event("ERROR_CONTROL", "");
  return 1;
}

int main(int argc, char **argv) {
  setvbuf(stdout, NULL, _IOLBF, 0);
  umask(077);
  if (getuid() != 0) {
    event("ERROR_ROOT", "");
    return 1;
  }
  if (argc == 3 && !strcmp(argv[1], "--status"))
    return status_backend(argv[2]);
  if (argc == 3 && !strcmp(argv[1], "--stop"))
    return stop_backend(argv[2]);
  bool daemonize = argc == 9 && !strcmp(argv[1], "--start");
  int argument = daemonize ? 2 : 1;
  if ((!daemonize && argc != 8) || (daemonize && argc != 9)) {
    event("ERROR_ARGUMENT", "");
    return 2;
  }
  const char *shim = argv[argument], *su_source = argv[argument + 1],
             *directory = argv[argument + 2];
  char *end;
  long port = strtol(argv[argument + 3], &end, 10);
  char *user_end;
  long user_id = strtol(argv[argument + 4], &user_end, 10);
  struct authorization shell_authorization = {0};
  struct authorization root_authorization = {0};
  if (*end || *user_end || port < 1024 || port > 65535 || user_id < 0 || user_id > 21474 ||
      strcmp(directory, "/data/local/tmp/net.ryuya_dev.tailadb") || strlen(shim) > 500 ||
      strlen(su_source) > 500 || !parse_authorization(argv[argument + 5], &shell_authorization) ||
      !parse_authorization(argv[argument + 6], &root_authorization)) {
    event("ERROR_ARGUMENT", "");
    return 2;
  }
  for (size_t i = 0; i < root_authorization.count; i++) {
    if (!host_allowed(&shell_authorization, root_authorization.addresses[i].s_addr)) {
      event("ERROR_ARGUMENT", "");
      return 2;
    }
  }
  if (daemonize) {
    pid_t daemon = fork();
    if (daemon < 0) {
      event("ERROR_FORK", "");
      return 1;
    }
    if (daemon > 0) {
      event("STARTING", "");
      return 0;
    }
    if (setsid() < 0)
      return 1;
    int null = open("/dev/null", O_RDWR | O_CLOEXEC);
    if (null < 0 || dup2(null, STDIN_FILENO) < 0 || dup2(null, STDOUT_FILENO) < 0 ||
        dup2(null, STDERR_FILENO) < 0)
      return 1;
    if (null > STDERR_FILENO)
      close(null);
  }
  if (mkdir(directory, 0711) && errno != EEXIST) {
    event("ERROR_STORAGE", "");
    return 3;
  }
  struct stat st;
  if (lstat(directory, &st) || !S_ISDIR(st.st_mode) || st.st_uid != 0 || (st.st_mode & 066) ||
      chmod(directory, 0711)) {
    event("ERROR_STORAGE", "");
    return 3;
  }
  char lockpath[400];
  snprintf(lockpath, sizeof(lockpath), "%s/lock", directory);
  int lock = open(lockpath, O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
  if (lock < 0 || flock(lock, LOCK_EX | LOCK_NB)) {
    event("ERROR_ALREADY_RUNNING", "");
    return 3;
  }
  // Only the lock owner may replace status/peer. Compatibility errors must be
  // persisted too, otherwise the UI can resurrect a previous WAITING state.
  snprintf(status_path, sizeof(status_path), "%s/status", directory);
  snprintf(peer_path, sizeof(peer_path), "%s/peer", directory);
  unlink(peer_path);
  char pidpath[400];
  snprintf(pidpath, sizeof(pidpath), "%s/pid", directory);
  int pidfile = open(pidpath, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0600);
  if (pidfile < 0 || dprintf(pidfile, "%ld\n", (long)getpid()) < 0 || fsync(pidfile) ||
      close(pidfile)) {
    if (pidfile >= 0)
      close(pidfile);
    event("ERROR_STORAGE", "");
    return 3;
  }
  event("STARTING", "");
  const struct tailadb_daemon_profile *daemon_profile = supported_daemon();
  if (!daemon_profile) {
    event("ERROR_COMPAT", "");
    return 1;
  }
  char binary[400];
  snprintf(binary, sizeof(binary), "%s/adbd", directory);
  char su_wrapper[400];
  snprintf(su_wrapper, sizeof(su_wrapper), "%s/su", directory);
  if (copy_daemon(binary) || copy_helper(su_source, su_wrapper)) {
    event("ERROR_DAEMON", "");
    return 4;
  }
  signal(SIGTERM, stop_signal);
  signal(SIGINT, stop_signal);
  signal(SIGPIPE, SIG_IGN);
  int route = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE);
  struct sockaddr_nl nl = {.nl_family = AF_NETLINK,
                           .nl_groups = RTMGRP_LINK | RTMGRP_IPV4_IFADDR | RTMGRP_IPV4_ROUTE};
  if (route < 0 || bind(route, (struct sockaddr *)&nl, sizeof(nl))) {
    event("ERROR_NETLINK", "");
    return 5;
  }
  int keys = inotify_init1(IN_CLOEXEC | IN_NONBLOCK);
  if (keys < 0 || inotify_add_watch(keys, "/data/misc/adb",
                                    IN_CLOSE_WRITE | IN_CREATE | IN_DELETE | IN_MOVED_TO |
                                        IN_MOVED_FROM | IN_DELETE_SELF | IN_MOVE_SELF) < 0) {
    event("ERROR_KEYS", "");
    return 6;
  }
  pid_t child = 0;
  int listener = -1, control = -1;
  struct vpn current = {0};
  uint32_t current_peer = 0;
  uint8_t current_token[TOKEN_BYTES] = {0};
  bool session_active = false;
  bool needs_check = true;
  unsigned attempts = 0;
  int failures = 0;
  char last_state[32] = "";
  char last_detail[768] = "";
  while (!stopped) {
    if (needs_check) {
      struct vpn next = {0};
      struct vpn_scan scan = {0};
      enum vpn_result vpn_state = vpn_info(&next, (unsigned)user_id, &scan);
      bool available = vpn_state == VPN_READY;
      if (child && (!available || strcmp(next.ip, current.ip) || next.index != current.index ||
                    next.handle != current.handle || next.uid != current.uid)) {
        reap(child);
        child = 0;
        if (listener >= 0)
          close(listener);
        listener = -1;
        if (control >= 0)
          close(control);
        control = -1;
        session_active = false;
        current_peer = 0;
        secure_zero(current_token, sizeof(current_token));
        memset(&current, 0, sizeof(current));
      }
      if (!available) {
        char detail[768] = {0};
        vpn_detail(&scan, (unsigned)user_id, detail, sizeof(detail));
        const char *state = vpn_state == VPN_OTHER_USER         ? "WAITING_SELECTED_USER"
                            : vpn_state == VPN_AMBIGUOUS        ? "WAITING_MULTIPLE_TAILSCALE"
                            : vpn_state == VPN_INSPECTION_ERROR ? "WAITING_NETWORK_INSPECTION"
                                                                : "WAITING_TAILSCALE";
        if (strcmp(last_state, state) || strcmp(last_detail, detail)) {
          event(state, detail);
          strcpy(last_state, state);
          strcpy(last_detail, detail);
        }
      } else if (!child && failures < 3) {
        listener = secure_listener(&next, port);
        if (listener < 0) {
          event("ERROR_BIND", "");
          break;
        }
        int control_pair[2];
        if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, control_pair)) {
          close(listener);
          listener = -1;
          event("ERROR_PIPE", "");
          break;
        }
        child = fork();
        if (child == 0) {
          close(control_pair[0]);
          pid_t parent = getppid();
          if (prctl(PR_SET_PDEATHSIG, SIGKILL) || getppid() != parent)
            _exit(122);
          int mount_result = enter_clean_mount_namespace();
          if (mount_result) {
            struct control_message failure = {.type = CONTROL_FAILURE,
                                              .peer = ((unsigned)mount_result << 16) |
                                                      (unsigned)(errno & 0xffff)};
            send(control_pair[1], &failure, sizeof(failure), MSG_NOSIGNAL);
            _exit(123);
          }
          if (unshare(CLONE_NEWNET)) {
            struct control_message failure = {.type = CONTROL_FAILURE,
                                              .peer = (6U << 16) | (unsigned)(errno & 0xffff)};
            send(control_pair[1], &failure, sizeof(failure), MSG_NOSIGNAL);
            _exit(124);
          }
          close_unrelated(listener, control_pair[1]);
          int devnull = open("/dev/null", O_RDONLY);
          if (devnull < 0)
            _exit(124);
          if (dup2(devnull, 0) < 0)
            _exit(124);
          if (devnull > 2)
            close(devnull);
          int listener_flags = fcntl(listener, F_GETFD);
          int control_flags = fcntl(control_pair[1], F_GETFD);
          if (listener_flags < 0 || control_flags < 0 ||
              fcntl(listener, F_SETFD, listener_flags & ~FD_CLOEXEC) ||
              fcntl(control_pair[1], F_SETFD, control_flags & ~FD_CLOEXEC) ||
              (fcntl(listener, F_GETFD) & FD_CLOEXEC) ||
              (fcntl(control_pair[1], F_GETFD) & FD_CLOEXEC))
            _exit(124);
          char fd[20], controlfd[20], number[8];
          snprintf(fd, sizeof(fd), "%d", listener);
          snprintf(controlfd, sizeof(controlfd), "%d", control_pair[1]);
          snprintf(number, sizeof(number), "%ld", port);
          setenv("TAILADB_FD", fd, 1);
          setenv("TAILADB_CONTROL_FD", controlfd, 1);
          setenv("TAILADB_IP", next.ip, 1);
          setenv("TAILADB_PORT", number, 1);
          setenv("TAILADB_SU_DIR", directory, 1);
          setenv("TAILADB_ALLOWED_SHELL", argv[argument + 5], 1);
          setenv("TAILADB_AUTH_ABI", daemon_profile->policy_abi, 1);
          setenv("LD_PRELOAD", shim, 1);
          setenv("LD_LIBRARY_PATH", "/apex/com.android.adbd/lib64:/system/lib64", 1);
          unsetenv("ANDROID_SOCKET_adbd");
          setenv("ANDROID_LOG_TAGS", "*:i", 1);
          execl(binary, "tailadb-adbd", NULL);
          _exit(125);
        }
        close(control_pair[1]);
        if (child < 0) {
          close(control_pair[0]);
          close(listener);
          listener = -1;
          event("ERROR_FORK", "");
          break;
        }
        control = control_pair[0];
        struct pollfd ready = {.fd = control, .events = POLLIN};
        struct control_message message = {0};
        int ready_result = poll(&ready, 1, 1000);
        ssize_t ready_size = ready_result > 0 ? recv(control, &message, sizeof(message), 0) : -1;
        bool verified_ready = !ready_result && child_ready(child, getpid(), listener, binary);
        if (!verified_ready &&
            (ready_result <= 0 || ready_size != sizeof(message) || message.type != CONTROL_READY)) {
          int child_status = 0;
          pid_t waited = waitpid(child, &child_status, WNOHANG);
          if (!waited) {
            kill(child, SIGTERM);
            for (int i = 0; i < 20 && !waited; i++) {
              usleep(50000);
              waited = waitpid(child, &child_status, WNOHANG);
            }
          }
          if (!waited) {
            kill(child, SIGKILL);
            do {
              waited = waitpid(child, &child_status, 0);
            } while (waited < 0 && errno == EINTR);
          }
          child = 0;
          close(listener);
          listener = -1;
          close(control);
          control = -1;
          char detail[64] = "control_ready_failed";
          if (ready_size == sizeof(message) && message.type == CONTROL_FAILURE)
            snprintf(detail, sizeof(detail), "stage_%u_errno_%u", message.peer >> 16,
                     message.peer & 0xffff);
          else if (waited > 0 && WIFEXITED(child_status))
            snprintf(detail, sizeof(detail), "child_exit_%d", WEXITSTATUS(child_status));
          else if (waited > 0 && WIFSIGNALED(child_status))
            snprintf(detail, sizeof(detail), "child_signal_%d", WTERMSIG(child_status));
          event("ERROR_ISOLATION", detail);
          break;
        }
        current = next;
        char info[160];
        snprintf(info, sizeof(info), "%s:%ld\t%s\t%u\t%llu", next.ip, port, next.iface,
                 next.uid / 100000, (unsigned long long)next.handle);
        event("LISTENING", info);
        strcpy(last_state, "LISTENING");
        strcpy(last_detail, info);
      }
      needs_check = false;
    }
    if (child && waitpid(child, NULL, WNOHANG) == child) {
      child = 0;
      if (listener >= 0)
        close(listener);
      listener = -1;
      if (control >= 0)
        close(control);
      control = -1;
      session_active = false;
      current_peer = 0;
      secure_zero(current_token, sizeof(current_token));
      failures++;
      if (failures >= 3) {
        event("ERROR_CRASH_LOOP", "");
        break;
      }
      event("RETRYING", "");
      needs_check = true;
    }
    struct pollfd fds[3] = {{.fd = route, .events = POLLIN},
                            {.fd = keys, .events = POLLIN},
                            {.fd = control, .events = POLLIN | POLLHUP}};
    // Netlink drives updates. A slow check covers VPN ownership changes without
    // link changes.
    int timeout = attempts < 4 ? 1000 : 15000;
    int result = poll(fds, 3, timeout);
    if (result < 0 && errno != EINTR) {
      event("ERROR_POLL", "");
      break;
    }
    if (fds[0].revents & POLLIN) {
      char buffer[16384];
      while (recv(route, buffer, sizeof(buffer), MSG_DONTWAIT) > 0) {
      }
      needs_check = true;
      attempts = 0;
    }
    if (fds[1].revents & POLLIN) {
      char changes[8192] __attribute__((aligned(__alignof__(struct inotify_event))));
      ssize_t count;
      bool changed = false, watch_lost = false;
      while ((count = read(keys, changes, sizeof(changes))) > 0)
        for (char *p = changes; p < changes + count;) {
          const struct inotify_event *e = (const struct inotify_event *)p;
          if (e->mask & (IN_DELETE_SELF | IN_MOVE_SELF | IN_IGNORED))
            watch_lost = true;
          if ((e->mask & IN_Q_OVERFLOW) || (e->len && !strcmp(e->name, "adb_keys")))
            changed = true;
          p += sizeof(*e) + e->len;
        }
      if (watch_lost) {
        event("ERROR_KEYS", "");
        break;
      }
      if (changed) {
        reap(child);
        child = 0;
        if (listener >= 0)
          close(listener);
        listener = -1;
        if (control >= 0)
          close(control);
        control = -1;
        session_active = false;
        current_peer = 0;
        secure_zero(current_token, sizeof(current_token));
        needs_check = true;
        event("RETRYING", "");
      }
    }
    if (fds[2].revents & POLLIN) {
      struct control_message message;
      ssize_t size;
      do {
        char ancillary[CMSG_SPACE(sizeof(int))] = {0};
        struct iovec iov = {.iov_base = &message, .iov_len = sizeof(message)};
        struct msghdr packet = {.msg_iov = &iov,
                                .msg_iovlen = 1,
                                .msg_control = ancillary,
                                .msg_controllen = sizeof(ancillary)};
        size = recvmsg(control, &packet, MSG_DONTWAIT | MSG_CMSG_CLOEXEC);
        if (size != sizeof(message))
          break;
        int broker_client = -1;
        struct cmsghdr *header = CMSG_FIRSTHDR(&packet);
        bool valid_broker =
            message.type == CONTROL_BROKER && !(packet.msg_flags & (MSG_CTRUNC | MSG_TRUNC)) &&
            header && !CMSG_NXTHDR(&packet, header) && header->cmsg_level == SOL_SOCKET &&
            header->cmsg_type == SCM_RIGHTS && header->cmsg_len == CMSG_LEN(sizeof(broker_client));
        if (valid_broker)
          memcpy(&broker_client, CMSG_DATA(header), sizeof(broker_client));
        if (message.type == CONTROL_READY) {
          if (header) {
            stopped = 1;
            event("ERROR_CONTROL", "");
          }
        } else if (message.type == CONTROL_CONNECTED) {
          if (header) {
            stopped = 1;
            event("ERROR_CONTROL", "");
            continue;
          }
          current_peer = message.peer;
          memcpy(current_token, message.token, sizeof(current_token));
          session_active = true;
          char peer[INET_ADDRSTRLEN] = {0};
          inet_ntop(AF_INET, &current_peer, peer, sizeof(peer));
          char detail[64];
          snprintf(detail, sizeof(detail), "%s\t%s", peer,
                   host_allowed(&root_authorization, current_peer)    ? "ROOT"
                   : host_allowed(&shell_authorization, current_peer) ? "SHELL"
                                                                      : "PENDING");
          event("PEER", detail);
        } else if (message.type == CONTROL_CLOSED) {
          if (header) {
            stopped = 1;
            event("ERROR_CONTROL", "");
            continue;
          }
          session_active = false;
          current_peer = 0;
          secure_zero(current_token, sizeof(current_token));
          event("PEER_CLOSED", "");
        } else if (message.type == CONTROL_BROKER && valid_broker && session_active &&
                   constant_equal(message.token, current_token, TOKEN_BYTES)) {
          start_broker_session(broker_client, host_allowed(&root_authorization, current_peer),
                               current_token);
          broker_client = -1;
        } else {
          event("ERROR_CONTROL", "");
          stopped = 1;
        }
        if (broker_client >= 0)
          close(broker_client);
      } while (size == sizeof(message) && !stopped);
      if (size == 0 || (size < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
        event("ERROR_CONTROL", "");
        stopped = 1;
      } else if (size > 0 && size != sizeof(message)) {
        event("ERROR_CONTROL", "");
        stopped = 1;
      }
    }
    if (fds[2].revents & (POLLHUP | POLLERR | POLLNVAL)) {
      event("ERROR_CONTROL", "");
      stopped = 1;
    }
    if (!result) {
      needs_check = true;
      attempts++;
    }
  }
  reap(child);
  if (listener >= 0)
    close(listener);
  if (control >= 0)
    close(control);
  close(route);
  close(keys);
  close(lock);
  unlink(pidpath);
  unlink(binary);
  unlink(su_wrapper);
  if (!terminal_error)
    event("STOPPED", "");
  return 0;
}
