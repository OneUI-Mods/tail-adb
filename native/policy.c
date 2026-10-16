// SPDX-License-Identifier: MIT
// Copyright (c) 2026 ryuya0124

#define _GNU_SOURCE
#include <arpa/inet.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/capability.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/random.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/system_properties.h>
#include <time.h>
#include <unistd.h>

#define TOKEN_BYTES 32

enum control_type {
  CONTROL_READY = 1,
  CONTROL_CONNECTED = 2,
  CONTROL_CLOSED = 3,
  CONTROL_BROKER = 4
};
struct control_message {
  uint32_t type;
  uint32_t peer;
  uint8_t token[TOKEN_BYTES];
};

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
static int inherited_fd = -1, control_fd = -1, active_fd = -1;
static struct sockaddr_in endpoint;
static bool configured, ready_sent, token_valid;
static uint8_t active_token[TOKEN_BYTES];
static pthread_mutex_t session_lock = PTHREAD_MUTEX_INITIALIZER;

static bool own_process(void) { return configured && getpid() == daemon_pid; }
static void die(int code) { _exit(code); }
static void secure_zero(void *memory, size_t length) {
  volatile uint8_t *bytes = memory;
  while (length--)
    *bytes++ = 0;
}

static void require_cloexec(int fd) {
  int flags = fcntl(fd, F_GETFD);
  if (flags < 0 || fcntl(fd, F_SETFD, flags | FD_CLOEXEC) ||
      (fcntl(fd, F_GETFD) & FD_CLOEXEC) == 0)
    die(120);
}

static void require_adbd_context(void) {
  int (*getcon_fn)(char **) = dlsym(RTLD_NEXT, "getcon");
  int (*setcon_fn)(const char *) = dlsym(RTLD_NEXT, "setcon");
  void (*freecon_fn)(char *) = dlsym(RTLD_NEXT, "freecon");
  if (!getcon_fn || !setcon_fn || !freecon_fn)
    die(120);
  char *context = NULL;
  if (getcon_fn(&context) || !context)
    die(120);
  bool already = !strcmp(context, "u:r:adbd:s0");
  bool expected = !strcmp(context, "u:r:ksu:s0");
  freecon_fn(context);
  if (!already && (!expected || setcon_fn("u:r:adbd:s0")))
    die(120);
  context = NULL;
  if (getcon_fn(&context) || !context || strcmp(context, "u:r:adbd:s0")) {
    if (context)
      freecon_fn(context);
    die(120);
  }
  freecon_fn(context);
}

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
             *fd = getenv("TAILADB_FD"), *control = getenv("TAILADB_CONTROL_FD");
  if (!ip || !port || !fd || !control)
    die(120);
  control_fd = atoi(control);
  inherited_fd = atoi(fd);
  if (control_fd < 3 || inherited_fd < 3 || control_fd == inherited_fd)
    die(120);
  require_cloexec(control_fd);
  endpoint.sin_family = AF_INET;
  if (inet_pton(AF_INET, ip, &endpoint.sin_addr) != 1)
    die(120);
  char *end = NULL;
  unsigned long p = strtoul(port, &end, 10);
  if (!end || *end || p < 1024 || p > 65535)
    die(120);
  endpoint.sin_port = htons((uint16_t)p);
  struct sockaddr_in actual = {0};
  socklen_t len = sizeof(actual);
  if (getsockname(inherited_fd, (struct sockaddr *)&actual, &len) ||
      len != sizeof(actual) || actual.sin_family != AF_INET ||
      actual.sin_addr.s_addr != endpoint.sin_addr.s_addr ||
      actual.sin_port != endpoint.sin_port)
    die(121);
  snprintf(props[0].value, sizeof(props[0].value), "acceptfd:%d", inherited_fd);
  daemon_pid = getpid();
  parent_pid = getppid();
  configured = true;
  require_adbd_context();
  unsetenv("LD_PRELOAD");
  unsetenv("LD_LIBRARY_PATH");
  unsetenv("TAILADB_CONTROL_FD");
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
  if (!real)
    die(120);
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
  if (!real)
    die(120);
  real(pi, cb, cookie);
}

int __system_property_get(const char *name, char *value) {
  int i = index_of(name);
  if (i >= 0) {
    strcpy(value, props[i].value);
    return (int)strlen(value);
  }
  int (*real)(const char *, char *) = dlsym(RTLD_NEXT, "__system_property_get");
  if (!real)
    die(120);
  return real(name, value);
}

uint32_t __system_property_serial(const prop_info *pi) {
  if (fake_index(pi) >= 0)
    return 2;
  uint32_t (*real)(const prop_info *) =
      dlsym(RTLD_NEXT, "__system_property_serial");
  if (!real)
    die(120);
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
  if (!real)
    die(120);
  return real(pi, old, next, timeout);
}

int __system_property_set(const char *name, const char *value) {
  if (own_process()) {
    errno = EPERM;
    return -1;
  }
  int (*real)(const char *, const char *) =
      dlsym(RTLD_NEXT, "__system_property_set");
  if (!real)
    die(120);
  return real(name, value);
}

int access(const char *path, int mode) {
  if (own_process() && !strcmp(path, "/dev/usb-ffs/adb/ep0")) {
    errno = ENOENT;
    return -1;
  }
  int (*real)(const char *, int) = dlsym(RTLD_NEXT, "access");
  if (!real)
    die(120);
  return real(path, mode);
}

static void send_control(const struct control_message *message) {
  ssize_t sent;
  do {
    sent = send(control_fd, message, sizeof(*message), MSG_NOSIGNAL);
  } while (sent < 0 && errno == EINTR);
  if (sent != sizeof(*message))
    die(127);
}

static void send_broker(const struct control_message *message, int broker) {
  char ancillary[CMSG_SPACE(sizeof(broker))] = {0};
  struct iovec iov = {.iov_base = (void *)message, .iov_len = sizeof(*message)};
  struct msghdr packet = {.msg_iov = &iov,
                          .msg_iovlen = 1,
                          .msg_control = ancillary,
                          .msg_controllen = sizeof(ancillary)};
  struct cmsghdr *header = CMSG_FIRSTHDR(&packet);
  header->cmsg_level = SOL_SOCKET;
  header->cmsg_type = SCM_RIGHTS;
  header->cmsg_len = CMSG_LEN(sizeof(broker));
  memcpy(CMSG_DATA(header), &broker, sizeof(broker));
  ssize_t sent;
  do {
    sent = sendmsg(control_fd, &packet, MSG_NOSIGNAL);
  } while (sent < 0 && errno == EINTR);
  if (sent != sizeof(*message))
    die(127);
}

static void verify_daemon(void) {
  if (getuid() != 2000 || geteuid() != 2000 || getgid() != 2000 ||
      getegid() != 2000 || prctl(PR_SET_PDEATHSIG, SIGKILL) ||
      getppid() != parent_pid)
    die(126);
  struct __user_cap_header_struct header = {
      .version = _LINUX_CAPABILITY_VERSION_3, .pid = 0};
  struct __user_cap_data_struct capabilities[2] = {0};
  if (syscall(__NR_capget, &header, capabilities) ||
      capabilities[0].effective || capabilities[0].permitted ||
      capabilities[0].inheritable || capabilities[1].effective ||
      capabilities[1].permitted || capabilities[1].inheritable)
    die(126);
  int (*getcon_fn)(char **) = dlsym(RTLD_NEXT, "getcon");
  void (*freecon_fn)(char *) = dlsym(RTLD_NEXT, "freecon");
  char *context = NULL;
  if (!getcon_fn || !freecon_fn || getcon_fn(&context) || !context ||
      strcmp(context, "u:r:adbd:s0")) {
    if (context && freecon_fn)
      freecon_fn(context);
    die(126);
  }
  freecon_fn(context);
  require_cloexec(inherited_fd);
  require_cloexec(control_fd);
}

static bool is_listener(int fd) {
  if (!own_process() || inherited_fd < 0)
    return false;
  struct sockaddr_in actual = {0};
  socklen_t length = sizeof(actual);
  return !getsockname(fd, (struct sockaddr *)&actual, &length) &&
         length == sizeof(actual) && actual.sin_family == AF_INET &&
         actual.sin_port == endpoint.sin_port &&
         actual.sin_addr.s_addr == endpoint.sin_addr.s_addr;
}

static void ready(void) {
  if (ready_sent)
    return;
  verify_daemon();
  struct control_message message = {.type = CONTROL_READY};
  send_control(&message);
  ready_sent = true;
}

static int accepted(int fd, const struct sockaddr *address, socklen_t length) {
  if (fd < 0)
    return fd;
  struct sockaddr_in peer = {0};
  if (!address || length < sizeof(peer) || address->sa_family != AF_INET) {
    close(fd);
    errno = EAFNOSUPPORT;
    return -1;
  }
  memcpy(&peer, address, sizeof(peer));
  if ((ntohl(peer.sin_addr.s_addr) & 0xffc00000U) != 0x64400000U) {
    close(fd);
    errno = EACCES;
    return -1;
  }
  pthread_mutex_lock(&session_lock);
  if (active_fd >= 0) {
    pthread_mutex_unlock(&session_lock);
    close(fd);
    errno = EBUSY;
    return -1;
  }
  if (getrandom(active_token, sizeof(active_token), 0) != sizeof(active_token)) {
    pthread_mutex_unlock(&session_lock);
    close(fd);
    errno = EIO;
    return -1;
  }
  active_fd = fd;
  token_valid = true;
  struct control_message message = {.type = CONTROL_CONNECTED,
                                    .peer = peer.sin_addr.s_addr};
  memcpy(message.token, active_token, sizeof(active_token));
  send_control(&message);
  pthread_mutex_unlock(&session_lock);
  return fd;
}

int accept4(int fd, struct sockaddr *address, socklen_t *length, int flags) {
  int (*real)(int, struct sockaddr *, socklen_t *, int) =
      dlsym(RTLD_NEXT, "accept4");
  if (!real)
    die(120);
  if (!is_listener(fd))
    return real(fd, address, length, flags);
  ready();
  struct sockaddr_in peer = {0};
  socklen_t peer_length = sizeof(peer);
  int result = real(fd, (struct sockaddr *)&peer, &peer_length, flags | SOCK_CLOEXEC);
  if (address && length && result >= 0) {
    socklen_t copy = *length < peer_length ? *length : peer_length;
    memcpy(address, &peer, copy);
    *length = peer_length;
  }
  return accepted(result, (struct sockaddr *)&peer, peer_length);
}

int accept(int fd, struct sockaddr *address, socklen_t *length) {
  int (*real)(int, struct sockaddr *, socklen_t *) = dlsym(RTLD_NEXT, "accept");
  if (!real)
    die(120);
  if (!is_listener(fd))
    return real(fd, address, length);
  ready();
  struct sockaddr_in peer = {0};
  socklen_t peer_length = sizeof(peer);
  int result = real(fd, (struct sockaddr *)&peer, &peer_length);
  if (result >= 0)
    require_cloexec(result);
  if (address && length && result >= 0) {
    socklen_t copy = *length < peer_length ? *length : peer_length;
    memcpy(address, &peer, copy);
    *length = peer_length;
  }
  return accepted(result, (struct sockaddr *)&peer, peer_length);
}

int close(int fd) {
  int (*real)(int) = dlsym(RTLD_NEXT, "close");
  if (!real)
    die(120);
  if (own_process()) {
    pthread_mutex_lock(&session_lock);
    if (fd == active_fd) {
      struct control_message message = {.type = CONTROL_CLOSED};
      send_control(&message);
      active_fd = -1;
      token_valid = false;
      secure_zero(active_token, sizeof(active_token));
    }
    pthread_mutex_unlock(&session_lock);
  }
  return real(fd);
}

int execle(const char *path, const char *arg, ...) {
  char *arguments[128];
  size_t argc = 0;
  arguments[argc++] = (char *)arg;
  va_list values;
  va_start(values, arg);
  char *next;
  while ((next = va_arg(values, char *)) != NULL) {
    if (argc >= sizeof(arguments) / sizeof(arguments[0]) - 1) {
      va_end(values);
      errno = E2BIG;
      return -1;
    }
    arguments[argc++] = next;
  }
  arguments[argc] = NULL;
  char *const *environment = va_arg(values, char *const *);
  va_end(values);

  int (*real_execve)(const char *, char *const[], char *const[]) =
      dlsym(RTLD_NEXT, "execve");
  if (!real_execve) {
    errno = ENOSYS;
    return -1;
  }
  if (!configured || getppid() != daemon_pid || !environment)
    return real_execve(path, arguments, (char *const *)environment);
  pthread_mutex_lock(&session_lock);
  bool valid = token_valid;
  uint8_t token[TOKEN_BYTES];
  memcpy(token, active_token, sizeof(token));
  pthread_mutex_unlock(&session_lock);
  if (!valid) {
    secure_zero(token, sizeof(token));
    errno = EACCES;
    return -1;
  }

  int broker[2];
  if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, broker)) {
    secure_zero(token, sizeof(token));
    return -1;
  }
  int broker_flags = fcntl(broker[0], F_GETFD);
  if (broker_flags < 0 ||
      fcntl(broker[0], F_SETFD, broker_flags & ~FD_CLOEXEC) ||
      (fcntl(broker[0], F_GETFD) & FD_CLOEXEC)) {
    close(broker[0]);
    close(broker[1]);
    secure_zero(token, sizeof(token));
    errno = EACCES;
    return -1;
  }
  struct control_message broker_message = {.type = CONTROL_BROKER};
  memcpy(broker_message.token, token, sizeof(token));
  send_broker(&broker_message, broker[1]);
  close(broker[1]);

  char session[sizeof("TAILADB_SESSION=") + TOKEN_BYTES * 2];
  strcpy(session, "TAILADB_SESSION=");
  static const char hex[] = "0123456789abcdef";
  size_t offset = sizeof("TAILADB_SESSION=") - 1;
  for (size_t i = 0; i < TOKEN_BYTES; i++) {
    session[offset + i * 2] = hex[token[i] >> 4];
    session[offset + i * 2 + 1] = hex[token[i] & 15];
  }
  session[sizeof(session) - 1] = 0;
  secure_zero(token, sizeof(token));
  char broker_environment[48];
  snprintf(broker_environment, sizeof(broker_environment),
           "TAILADB_BROKER_FD=%d", broker[0]);

  char *updated[258];
  size_t count = 0;
  for (; environment[count] && count < 255; count++)
    updated[count] = environment[count];
  if (environment[count]) {
    secure_zero(session, sizeof(session));
    close(broker[0]);
    errno = E2BIG;
    return -1;
  }
  updated[count++] = session;
  updated[count++] = broker_environment;
  updated[count] = NULL;
  int result = real_execve(path, arguments, updated);
  close(broker[0]);
  return result;
}
