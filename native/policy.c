// SPDX-License-Identifier: MIT
// Copyright (c) 2026 ryuya0124

#define _GNU_SOURCE
#include <arpa/inet.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/system_properties.h>
#include <time.h>
#include <unistd.h>

// Private to the extra adbd process: never writes the global property area.
struct override {
  const char *name;
  char value[92];
};
static struct override props[] = {{"service.adb.listen_addrs", ""},
                                  {"service.adb.tcp.port", "-1"},
                                  {"persist.adb.tcp.port", "-1"},
                                  {"persist.adb.tls_server.enable", "0"},
                                  {"service.adb.root", "0"},
                                  {"ro.adb.secure", "1"},
                                  {"ro.secure", "1"}};
static pid_t daemon_pid, parent_pid;
static int inherited_fd = -1, ack_fd = -1;
static struct sockaddr_in endpoint;
static bool configured;
static bool own_process(void) { return configured && getpid() == daemon_pid; }
static int index_of(const char *name) {
  if (!own_process())
    return -1;
  for (unsigned i = 0; i < sizeof(props) / sizeof(props[0]); i++)
    if (!strcmp(name, props[i].name))
      return (int)i;
  return -1;
}
static int fake_index(const prop_info *pi) {
  if (!own_process())
    return -1;
  for (unsigned i = 0; i < sizeof(props) / sizeof(props[0]); i++)
    if (pi == (const prop_info *)&props[i])
      return (int)i;
  return -1;
}
__attribute__((constructor)) static void configure(void) {
  const char *ip = getenv("TAILADB_IP"), *port = getenv("TAILADB_PORT"),
             *fd = getenv("TAILADB_FD"), *ack = getenv("TAILADB_ACK_FD");
  if (!ip || !port || !fd || !ack)
    _exit(120);
  ack_fd = atoi(ack);
  if (ack_fd < 3)
    _exit(120);
  fcntl(ack_fd, F_SETFD, FD_CLOEXEC);
  endpoint.sin_family = AF_INET;
  if (inet_pton(AF_INET, ip, &endpoint.sin_addr) != 1)
    _exit(120);
  unsigned long p = strtoul(port, NULL, 10);
  if (p < 1024 || p > 65535)
    _exit(120);
  endpoint.sin_port = htons((uint16_t)p);
  inherited_fd = atoi(fd);
  struct sockaddr_in actual;
  socklen_t len = sizeof(actual);
  if (getsockname(inherited_fd, (struct sockaddr *)&actual, &len) ||
      actual.sin_family != AF_INET ||
      actual.sin_addr.s_addr != endpoint.sin_addr.s_addr ||
      actual.sin_port != endpoint.sin_port)
    _exit(121);
  snprintf(props[0].value, sizeof(props[0].value), "acceptfd:%d", inherited_fd);
  daemon_pid = getpid();
  parent_pid = getppid();
  configured = true;
  // Child shells must not inherit a loader override or our listening
  // descriptor.
  unsetenv("LD_PRELOAD");
  unsetenv("LD_LIBRARY_PATH");
  unsetenv("TAILADB_ACK_FD");
  unsetenv("TAILADB_IP");
  unsetenv("TAILADB_PORT");
  unsetenv("TAILADB_FD");
}
const prop_info *__system_property_find(const char *name) {
  int i = index_of(name);
  if (i >= 0)
    return (const prop_info *)&props[i];
  const prop_info *(*real)(const char *) =
      dlsym(RTLD_NEXT, "__system_property_find");
  return real(name);
}
void __system_property_read_callback(const prop_info *pi,
                                     void (*cb)(void *, const char *,
                                                const char *, uint32_t),
                                     void *cookie) {
  int i = fake_index(pi);
  if (i >= 0) {
    cb(cookie, props[i].name, props[i].value, 2);
    return;
  }
  void (*real)(const prop_info *,
               void (*)(void *, const char *, const char *, uint32_t), void *) =
      dlsym(RTLD_NEXT, "__system_property_read_callback");
  real(pi, cb, cookie);
}
int __system_property_get(const char *name, char *value) {
  int i = index_of(name);
  if (i >= 0) {
    strcpy(value, props[i].value);
    return strlen(value);
  }
  int (*real)(const char *, char *) = dlsym(RTLD_NEXT, "__system_property_get");
  return real(name, value);
}
uint32_t __system_property_serial(const prop_info *pi) {
  if (fake_index(pi) >= 0)
    return 2;
  uint32_t (*real)(const prop_info *) =
      dlsym(RTLD_NEXT, "__system_property_serial");
  return real(pi);
}
bool __system_property_wait(const prop_info *pi, uint32_t old, uint32_t *next,
                            const struct timespec *timeout) {
  if (fake_index(pi) >= 0) {
    struct timespec delay = timeout ? *timeout : (struct timespec){60, 0};
    nanosleep(&delay, NULL);
    if (next)
      *next = 2;
    return false;
  }
  bool (*real)(const prop_info *, uint32_t, uint32_t *,
               const struct timespec *) =
      dlsym(RTLD_NEXT, "__system_property_wait");
  return real(pi, old, next, timeout);
}
int __system_property_set(const char *name, const char *value) {
  if (own_process()) {
    errno = EPERM;
    return -1;
  }
  int (*real)(const char *, const char *) =
      dlsym(RTLD_NEXT, "__system_property_set");
  return real(name, value);
}
int access(const char *path, int mode) {
  if (own_process() && !strcmp(path, "/dev/usb-ffs/adb/ep0")) {
    errno = ENOENT;
    return -1;
  }
  int (*real)(const char *, int) = dlsym(RTLD_NEXT, "access");
  return real(path, mode);
}

static void protect_inherited_listener(int fd) {
  if (!own_process() || inherited_fd < 0)
    return;
  struct sockaddr_in a = {0};
  socklen_t z = sizeof(a);
  if (!getsockname(fd, (struct sockaddr *)&a, &z) && a.sin_family == AF_INET &&
      a.sin_port == endpoint.sin_port &&
      a.sin_addr.s_addr == endpoint.sin_addr.s_addr) {
    if (getuid() != 2000 || prctl(PR_SET_PDEATHSIG, SIGKILL) ||
        getppid() != parent_pid)
      _exit(126);
    fcntl(inherited_fd, F_SETFD, FD_CLOEXEC);
    if (ack_fd >= 0) {
      if (write(ack_fd, "S", 1) != 1)
        _exit(127);
      close(ack_fd);
      ack_fd = -1;
    }
  }
}
int accept4(int fd, struct sockaddr *addr, socklen_t *len, int flags) {
  protect_inherited_listener(fd);
  int (*real)(int, struct sockaddr *, socklen_t *, int) =
      dlsym(RTLD_NEXT, "accept4");
  return real(fd, addr, len, flags);
}
int accept(int fd, struct sockaddr *addr, socklen_t *len) {
  protect_inherited_listener(fd);
  int (*real)(int, struct sockaddr *, socklen_t *) = dlsym(RTLD_NEXT, "accept");
  return real(fd, addr, len);
}
