// SPDX-License-Identifier: MIT
// Copyright (c) 2026 ryuya0124

#define _GNU_SOURCE
#include <android/multinetwork.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <poll.h>
#include <sched.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/inotify.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

struct vpn {
  char iface[IFNAMSIZ];
  char ip[INET_ADDRSTRLEN];
  uint64_t handle;
  unsigned index;
  unsigned uid;
};
static volatile sig_atomic_t stopped;
static void stop_signal(int signal) {
  (void)signal;
  stopped = 1;
}
static void event(const char *state, const char *detail) {
  printf("TAILADB\t%s\t%s\n", state, detail ? detail : "");
  fflush(stdout);
}
static bool safe_iface(const char *name) {
  if (!*name || strlen(name) >= IFNAMSIZ)
    return false;
  for (const char *p = name; *p; p++)
    if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
          (*p >= '0' && *p <= '9') || *p == '_' || *p == '.' || *p == '-'))
      return false;
  return true;
}
static bool package_uid(unsigned uid) {
  char command[160], line[512];
  snprintf(
      command, sizeof(command),
      "/system/bin/cmd package list packages -U --user %u com.tailscale.ipn",
      uid / 100000);
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
static bool vpn_info(struct vpn *out) {
  FILE *f = popen("/system/bin/dumpsys connectivity", "r");
  if (!f)
    return false;
  char *line = NULL;
  size_t cap = 0;
  int matches = 0;
  struct vpn candidate = {0};
  while (getline(&line, &cap, f) > 0) {
    if (!strstr(line, "NetworkAgentInfo{") ||
        !strstr(line, "VPN CONNECTED extra: VPN:com.tailscale.ipn}"))
      continue;
    char *iface = strstr(line, "InterfaceName: "),
         *ips = strstr(line, "LinkAddresses: [ "),
         *handle = strstr(line, "handle{"), *owner = strstr(line, "OwnerUid: ");
    if (!iface || !ips || !handle || !owner)
      continue;
    struct vpn v = {0};
    unsigned long long h = 0;
    if (sscanf(iface, "InterfaceName: %15s", v.iface) != 1 ||
        sscanf(handle, "handle{%llu}", &h) != 1 ||
        sscanf(owner, "OwnerUid: %u", &v.uid) != 1)
      continue;
    // Only a Tailscale CGNAT /32 address in the LinkAddresses list is accepted.
    const char *p = ips + strlen("LinkAddresses: [ ");
    bool found = false;
    while (*p && *p != ']') {
      if ((*p >= '0' && *p <= '9') && (p == ips + strlen("LinkAddresses: [ ") ||
                                       p[-1] == ',' || p[-1] == ' ')) {
        char ip[INET_ADDRSTRLEN] = {0};
        unsigned prefix = 0;
        int n = 0;
        if (sscanf(p, "%15[0-9.]/%u%n", ip, &prefix, &n) == 2 && n > 0 &&
            prefix == 32) {
          struct in_addr a;
          if (inet_pton(AF_INET, ip, &a) == 1 &&
              (ntohl(a.s_addr) & 0xffc00000) == 0x64400000) {
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
    candidate = v;
    matches++;
  }
  free(line);
  int rc = pclose(f);
  if (rc || matches != 1 || !package_uid(candidate.uid))
    return false;
  struct ifaddrs *all = NULL;
  if (getifaddrs(&all))
    return false;
  bool exists = false;
  for (struct ifaddrs *i = all; i; i = i->ifa_next)
    if (i->ifa_addr && i->ifa_addr->sa_family == AF_INET &&
        !strcmp(i->ifa_name, candidate.iface) && (i->ifa_flags & IFF_UP)) {
      char ip[INET_ADDRSTRLEN];
      inet_ntop(AF_INET, &((struct sockaddr_in *)i->ifa_addr)->sin_addr, ip,
                sizeof(ip));
      if (!strcmp(ip, candidate.ip))
        exists = true;
    }
  freeifaddrs(all);
  if (!exists)
    return false;
  *out = candidate;
  return true;
}
static bool supported_daemon(void) {
  FILE *f = popen("/system/bin/sha256sum /apex/com.android.adbd/bin/adbd", "r");
  if (!f)
    return false;
  char hash[65] = {0};
  bool good =
      fscanf(f, "%64s", hash) == 1 &&
      !strcmp(
          hash,
          "459d1f67760789bdf8eb5a7a5c1bbe409d5dd3837d408ce4d7d14e496fcb8cbb");
  return pclose(f) == 0 && good;
}
static int copy_daemon(const char *destination) {
  const char *source = "/apex/com.android.adbd/bin/adbd";
  int in = open(source, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
  if (in < 0)
    return -1;
  struct stat st;
  if (fstat(in, &st) || !S_ISREG(st.st_mode) || st.st_size < 100000 ||
      st.st_size > 20000000) {
    close(in);
    errno = EINVAL;
    return -1;
  }
  int out = open(destination,
                 O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0700);
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
static int secure_listener(const struct vpn *v, int port) {
  int fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0)
    return -1;
  int reuse = 1;
  if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse))) {
    close(fd);
    return -1;
  }
  if (setsockopt(fd, SOL_SOCKET, SO_BINDTODEVICE, v->iface,
                 strlen(v->iface) + 1) ||
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
  if (getsockopt(fd, SOL_SOCKET, SO_BINDTODEVICE, iface, &size) ||
      strcmp(iface, v->iface)) {
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
int main(int argc, char **argv) {
  setvbuf(stdout, NULL, _IOLBF, 0);
  umask(077);
  if (getuid() != 0) {
    event("ERROR_ROOT", "");
    return 1;
  }
  if (!supported_daemon()) {
    event("ERROR_COMPAT", "");
    return 1;
  }
  if (argc != 4) {
    event("ERROR_ARGUMENT", "");
    return 2;
  }
  const char *shim = argv[1], *directory = argv[2];
  char *end;
  long port = strtol(argv[3], &end, 10);
  if (*end || port < 1024 || port > 65535 || strlen(directory) > 300 ||
      strlen(shim) > 500) {
    event("ERROR_ARGUMENT", "");
    return 2;
  }
  if (mkdir(directory, 0700) && errno != EEXIST) {
    event("ERROR_STORAGE", "");
    return 3;
  }
  struct stat st;
  if (lstat(directory, &st) || !S_ISDIR(st.st_mode) || st.st_uid != 0 ||
      (st.st_mode & 077)) {
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
  char binary[400];
  snprintf(binary, sizeof(binary), "%s/adbd", directory);
  if (copy_daemon(binary)) {
    event("ERROR_DAEMON", "");
    return 4;
  }
  signal(SIGTERM, stop_signal);
  signal(SIGINT, stop_signal);
  signal(SIGPIPE, SIG_IGN);
  int route = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE);
  struct sockaddr_nl nl = {.nl_family = AF_NETLINK,
                           .nl_groups = RTMGRP_LINK | RTMGRP_IPV4_IFADDR |
                                        RTMGRP_IPV4_ROUTE};
  if (route < 0 || bind(route, (struct sockaddr *)&nl, sizeof(nl))) {
    event("ERROR_NETLINK", "");
    return 5;
  }
  int keys = inotify_init1(IN_CLOEXEC | IN_NONBLOCK);
  if (keys < 0 || inotify_add_watch(keys, "/data/misc/adb",
                                    IN_CLOSE_WRITE | IN_CREATE | IN_DELETE |
                                        IN_MOVED_TO | IN_MOVED_FROM |
                                        IN_DELETE_SELF | IN_MOVE_SELF) < 0) {
    event("ERROR_KEYS", "");
    return 6;
  }
  pid_t child = 0;
  int listener = -1, ack = -1;
  struct vpn current = {0};
  bool needs_check = true;
  unsigned attempts = 0;
  int failures = 0;
  char last_state[32] = "";
  while (!stopped) {
    if (needs_check) {
      struct vpn next = {0};
      bool available = vpn_info(&next);
      if (child && (!available || strcmp(next.ip, current.ip) ||
                    next.index != current.index ||
                    next.handle != current.handle || next.uid != current.uid)) {
        reap(child);
        child = 0;
        if (listener >= 0)
          close(listener);
        listener = -1;
        if (ack >= 0)
          close(ack);
        ack = -1;
        memset(&current, 0, sizeof(current));
      }
      if (!available) {
        if (strcmp(last_state, "WAITING_TAILSCALE")) {
          event("WAITING_TAILSCALE", "");
          strcpy(last_state, "WAITING_TAILSCALE");
        }
      } else if (!child && failures < 3) {
        listener = secure_listener(&next, port);
        if (listener < 0) {
          event("ERROR_BIND", "");
          break;
        }
        int pipefd[2];
        if (pipe2(pipefd, O_CLOEXEC)) {
          close(listener);
          listener = -1;
          event("ERROR_PIPE", "");
          break;
        }
        child = fork();
        if (child == 0) {
          close(pipefd[0]);
          pid_t parent = getppid();
          if (prctl(PR_SET_PDEATHSIG, SIGKILL) || getppid() != parent)
            _exit(122);
          if (unshare(CLONE_NEWNET))
            _exit(123);
          close_unrelated(listener, pipefd[1]);
          int devnull = open("/dev/null", O_RDONLY);
          if (devnull < 0)
            _exit(124);
          dup2(devnull, 0);
          if (devnull > 2)
            close(devnull);
          fcntl(listener, F_SETFD, 0);
          fcntl(pipefd[1], F_SETFD, 0);
          char fd[20], ackfd[20], number[8];
          snprintf(fd, sizeof(fd), "%d", listener);
          snprintf(ackfd, sizeof(ackfd), "%d", pipefd[1]);
          snprintf(number, sizeof(number), "%ld", port);
          setenv("TAILADB_FD", fd, 1);
          setenv("TAILADB_ACK_FD", ackfd, 1);
          setenv("TAILADB_IP", next.ip, 1);
          setenv("TAILADB_PORT", number, 1);
          setenv("LD_PRELOAD", shim, 1);
          setenv("LD_LIBRARY_PATH",
                 "/apex/com.android.adbd/lib64:/system/lib64", 1);
          unsetenv("ANDROID_SOCKET_adbd");
          setenv("ANDROID_LOG_TAGS", "*:w", 1);
          execl(binary, "tailadb-adbd", NULL);
          _exit(125);
        }
        close(pipefd[1]);
        if (child < 0) {
          close(pipefd[0]);
          close(listener);
          listener = -1;
          event("ERROR_FORK", "");
          break;
        }
        ack = pipefd[0];
        struct pollfd ready = {.fd = ack, .events = POLLIN};
        char token = 0;
        if (poll(&ready, 1, 3000) <= 0 || read(ack, &token, 1) != 1 ||
            token != 'S') {
          reap(child);
          child = 0;
          close(listener);
          listener = -1;
          close(ack);
          ack = -1;
          event("ERROR_ISOLATION", "");
          break;
        }
        close(ack);
        ack = -1;
        current = next;
        char info[160];
        snprintf(info, sizeof(info), "%s:%ld\t%s\t%u\t%llu", next.ip, port,
                 next.iface, next.uid / 100000,
                 (unsigned long long)next.handle);
        event("LISTENING", info);
        strcpy(last_state, "LISTENING");
      }
      needs_check = false;
    }
    if (child && waitpid(child, NULL, WNOHANG) == child) {
      child = 0;
      if (listener >= 0)
        close(listener);
      listener = -1;
      failures++;
      if (failures >= 3) {
        event("ERROR_CRASH_LOOP", "");
        break;
      }
      event("RETRYING", "");
      needs_check = true;
    }
    struct pollfd fds[3] = {{.fd = 0, .events = POLLIN | POLLHUP},
                            {.fd = route, .events = POLLIN},
                            {.fd = keys, .events = POLLIN}};
    // Netlink drives updates. A slow check covers VPN ownership changes without
    // link changes.
    int timeout = attempts < 4 ? 1000 : 15000;
    int result = poll(fds, 3, timeout);
    if (result < 0 && errno != EINTR) {
      event("ERROR_POLL", "");
      break;
    }
    if (fds[0].revents & (POLLHUP | POLLERR | POLLNVAL))
      break;
    if (fds[0].revents & POLLIN) {
      char c;
      if (read(0, &c, 1) <= 0 || c == 'q')
        break;
    }
    if (fds[1].revents & POLLIN) {
      char buffer[16384];
      while (recv(route, buffer, sizeof(buffer), MSG_DONTWAIT) > 0) {
      }
      needs_check = true;
      attempts = 0;
    }
    if (fds[2].revents & POLLIN) {
      char changes[8192]
          __attribute__((aligned(__alignof__(struct inotify_event))));
      ssize_t count;
      bool changed = false, watch_lost = false;
      while ((count = read(keys, changes, sizeof(changes))) > 0)
        for (char *p = changes; p < changes + count;) {
          const struct inotify_event *e = (const struct inotify_event *)p;
          if (e->mask & (IN_DELETE_SELF | IN_MOVE_SELF | IN_IGNORED))
            watch_lost = true;
          if ((e->mask & IN_Q_OVERFLOW) ||
              (e->len && !strcmp(e->name, "adb_keys")))
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
        needs_check = true;
        event("RETRYING", "");
      }
    }
    if (!result) {
      needs_check = true;
      attempts++;
    }
  }
  reap(child);
  if (listener >= 0)
    close(listener);
  if (ack >= 0)
    close(ack);
  close(route);
  close(keys);
  close(lock);
  unlink(binary);
  event("STOPPED", "");
  return 0;
}
