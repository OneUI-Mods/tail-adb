// SPDX-License-Identifier: MIT
// Copyright (c) 2026 ryuya0124

#define _GNU_SOURCE
#include <android/log.h>
#include <arpa/inet.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/capability.h>
#include <limits.h>
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
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/system_properties.h>
#include <time.h>
#include <unistd.h>

#include "auth_compat.h"

#define TOKEN_BYTES 32
#define MAX_ALLOWED 32
#define LOG_TAG "TailAdbPolicy"
#define AUTH_INFO(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define AUTH_WARN(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

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
static struct in_addr allowed_shell[MAX_ALLOWED];
static size_t allowed_shell_count;
static uint32_t active_peer;
static bool configured, ready_sent, token_valid;
static unsigned next_transport_fd = 128;
static uint8_t active_token[TOKEN_BYTES];
static char shell_path[PATH_MAX + 160];
static pthread_mutex_t session_lock = PTHREAD_MUTEX_INITIALIZER;

struct local_auth_context {
  uint32_t marker;
};

static struct local_auth_context auth_context = {.marker = 0x54414442};
static struct auth_callbacks_v1 auth_callbacks;
static uint64_t next_auth_id = 1;
static enum tailadb_auth_abi auth_abi;

static bool own_process(void) {
  return configured && getpid() == daemon_pid;
}

static void die(int code) {
  _exit(code);
}

static void secure_zero(void *memory, size_t length) {
  volatile uint8_t *bytes = memory;
  while (length--)
    *bytes++ = 0;
}

static void require_cloexec(int fd) {
  int flags = fcntl(fd, F_GETFD);
  if (flags < 0 || fcntl(fd, F_SETFD, flags | FD_CLOEXEC) || (fcntl(fd, F_GETFD) & FD_CLOEXEC) == 0)
    die(120);
}

static bool read_context(char *out, size_t size) {
  int fd = open("/proc/thread-self/attr/current", O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
  if (fd < 0)
    return false;
  ssize_t length;
  do {
    length = read(fd, out, size - 1);
  } while (length < 0 && errno == EINTR);
  int saved = errno;
  close(fd);
  errno = saved;
  if (length <= 0 || (size_t)length >= size)
    return false;
  out[length] = 0;
  while (length > 0 && (out[length - 1] == '\n' || out[length - 1] == 0))
    out[--length] = 0;
  return length > 0;
}

static bool write_context(const char *context) {
  int fd = open("/proc/thread-self/attr/current", O_WRONLY | O_CLOEXEC | O_NOFOLLOW);
  if (fd < 0)
    return false;
  size_t size = strlen(context) + 1;
  ssize_t written;
  do {
    written = write(fd, context, size);
  } while (written < 0 && errno == EINTR);
  int saved = errno;
  close(fd);
  errno = saved;
  return written == (ssize_t)size;
}

static void require_adbd_context(void) {
  char context[256] = {0};
  if (!read_context(context, sizeof(context)))
    die(120);
  bool already = !strcmp(context, "u:r:adbd:s0");
  bool expected = !strcmp(context, "u:r:ksu:s0");
  if (!already && (!expected || !write_context("u:r:adbd:s0")))
    die(120);
  memset(context, 0, sizeof(context));
  if (!read_context(context, sizeof(context)) || strcmp(context, "u:r:adbd:s0"))
    die(120);
}

static bool valid_key_character(char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '+' ||
         c == '/' || c == '=';
}

static bool parse_allowed_shell(const char *text) {
  if (!text || strlen(text) > MAX_ALLOWED * INET_ADDRSTRLEN)
    return false;
  if (!*text)
    return true;
  const char *start = text;
  while (*start) {
    const char *end = strchr(start, ',');
    size_t length = end ? (size_t)(end - start) : strlen(start);
    if (!length || length >= INET_ADDRSTRLEN || allowed_shell_count >= MAX_ALLOWED)
      return false;
    char ip[INET_ADDRSTRLEN];
    memcpy(ip, start, length);
    ip[length] = 0;
    struct in_addr address;
    if (inet_pton(AF_INET, ip, &address) != 1 ||
        (ntohl(address.s_addr) & 0xffc00000U) != 0x64400000U)
      return false;
    for (size_t i = 0; i < allowed_shell_count; i++)
      if (allowed_shell[i].s_addr == address.s_addr)
        return false;
    allowed_shell[allowed_shell_count++] = address;
    if (!end)
      break;
    start = end + 1;
  }
  return true;
}

static bool shell_allowed(uint32_t peer) {
  for (size_t i = 0; i < allowed_shell_count; i++)
    if (allowed_shell[i].s_addr == peer)
      return true;
  return false;
}

static bool validate_key_line(const char *line, size_t length) {
  if (!length || length > 4096 || memchr(line, 0, length) || memchr(line, '\r', length))
    return false;
  size_t key_length = 0;
  while (key_length < length && line[key_length] != ' ' && line[key_length] != '\t')
    key_length++;
  if (key_length != 700)
    return false;
  for (size_t i = 0; i < key_length; i++)
    if (!valid_key_character(line[i]) || (line[i] == '=' && i + 2 < key_length))
      return false;
  return true;
}

static bool load_authorized_keys(char **contents, size_t *length) {
  *contents = NULL;
  *length = 0;
  int directory = open("/data/misc/adb", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
  if (directory < 0) {
    AUTH_WARN("authorized keys: directory open failed errno=%d", errno);
    return false;
  }
  struct stat directory_stat;
  bool trusted = !fstat(directory, &directory_stat) && S_ISDIR(directory_stat.st_mode) &&
                 directory_stat.st_uid == 1000 && directory_stat.st_gid == 2000 &&
                 !(directory_stat.st_mode & 0027);
  int fd = trusted ? openat(directory, "adb_keys", O_RDONLY | O_CLOEXEC | O_NOFOLLOW) : -1;
  close(directory);
  if (fd < 0) {
    AUTH_WARN("authorized keys: trust/open failed errno=%d", errno);
    return false;
  }
  struct stat before, after;
  trusted = !fstat(fd, &before) && S_ISREG(before.st_mode) && before.st_uid == 1000 &&
            before.st_gid == 2000 && !(before.st_mode & 0022) && before.st_size > 0 &&
            before.st_size <= 262144;
  char *data = trusted ? malloc((size_t)before.st_size + 1) : NULL;
  size_t used = 0;
  while (data && used < (size_t)before.st_size) {
    ssize_t count = read(fd, data + used, (size_t)before.st_size - used);
    if (count > 0)
      used += (size_t)count;
    else if (count < 0 && errno == EINTR)
      continue;
    else
      break;
  }
  char extra;
  ssize_t extra_size = data ? read(fd, &extra, 1) : -1;
  trusted = data && used == (size_t)before.st_size && extra_size == 0 && !fstat(fd, &after) &&
            before.st_dev == after.st_dev && before.st_ino == after.st_ino &&
            before.st_size == after.st_size && before.st_mtime == after.st_mtime;
  close(fd);
  if (!trusted) {
    AUTH_WARN("authorized keys: stable read or metadata validation failed");
    free(data);
    return false;
  }
  data[used] = 0;
  size_t keys = 0, start = 0;
  while (start < used) {
    size_t end = start;
    while (end < used && data[end] != '\n')
      end++;
    if (!validate_key_line(data + start, end - start) || ++keys > 128) {
      AUTH_WARN("authorized keys: invalid line index=%zu length=%zu", keys + 1, end - start);
      secure_zero(data, used);
      free(data);
      return false;
    }
    start = end + 1;
  }
  *contents = data;
  *length = used;
  AUTH_INFO("authorized keys: loaded count=%zu bytes=%zu", keys, used);
  return true;
}

void *adbd_auth_new(void *callbacks) {
  const struct auth_callbacks_v1 *version = callbacks;
  if (!own_process() || !tailadb_auth_callbacks_valid(auth_abi, version))
    die(123);
  auth_callbacks = *version;
  AUTH_INFO("auth new version=%u", version->version);
  return &auth_context;
}

void adbd_auth_delete(void *context) {
  if (own_process() && context != &auth_context)
    die(123);
}

void adbd_auth_run(void *context) {
  if (!own_process() || context != &auth_context)
    die(123);
  AUTH_INFO("auth run");
  for (;;)
    pause();
}

void adbd_auth_get_public_keys(void *context,
                               bool (*callback)(void *opaque, const char *public_key,
                                                size_t length),
                               void *opaque) {
  if (!own_process() || context != &auth_context || !callback)
    die(123);
  char *contents = NULL;
  size_t length = 0;
  AUTH_INFO("auth get public keys");
  if (!load_authorized_keys(&contents, &length))
    return;
  size_t start = 0;
  size_t delivered = 0;
  while (start < length) {
    size_t end = start;
    while (end < length && contents[end] != '\n')
      end++;
    delivered++;
    if (!callback(opaque, contents + start, end - start))
      break;
    start = end + 1;
  }
  secure_zero(contents, length);
  free(contents);
  AUTH_INFO("auth public keys delivered=%zu", delivered);
}

static uint64_t new_auth_id(void) {
  uint64_t id = __atomic_fetch_add(&next_auth_id, 1, __ATOMIC_RELAXED);
  if (!id)
    die(123);
  return id;
}

uint64_t adbd_auth_notify_auth(void *context, const char *key, size_t length) {
  if (!own_process() || context != &auth_context || !key || !length)
    die(123);
  uint64_t id = new_auth_id();
  AUTH_INFO("auth notify authorized length=%zu id=%llu", length, (unsigned long long)id);
  return id;
}

void adbd_auth_notify_disconnect(void *context, uint64_t id) {
  if (!own_process() || context != &auth_context || !id)
    die(123);
  AUTH_INFO("auth disconnect id=%llu", (unsigned long long)id);
}

uint64_t adbd_auth_prompt_user_with_id(void *context, const char *key, size_t length,
                                       void *opaque) {
  if (!own_process() || context != &auth_context || !key || !length)
    die(123);
  uint64_t id = new_auth_id();
  pthread_mutex_lock(&session_lock);
  bool allowed = token_valid && shell_allowed(active_peer);
  pthread_mutex_unlock(&session_lock);
  if (allowed) {
    AUTH_INFO("auth approved by Tailscale host policy length=%zu id=%llu", length,
              (unsigned long long)id);
    auth_callbacks.key_authorized(opaque, id);
  } else {
    AUTH_WARN("auth rejected pending host approval length=%zu id=%llu", length,
              (unsigned long long)id);
  }
  return id;
}

void adbd_auth_prompt_user(void *context, const char *key, size_t length, void *opaque) {
  (void)adbd_auth_prompt_user_with_id(context, key, length, opaque);
}

uint64_t adbd_auth_tls_device_connected(void *context, int32_t type, const char *key,
                                        size_t length) {
  (void)type;
  if (!own_process() || context != &auth_context || !key || !length)
    die(123);
  uint64_t id = new_auth_id();
  AUTH_INFO("auth TLS connected type=%d length=%zu id=%llu", type, length, (unsigned long long)id);
  return id;
}

void adbd_auth_tls_device_disconnected(void *context, int32_t type, uint64_t id) {
  (void)type;
  adbd_auth_notify_disconnect(context, id);
}

uint32_t adbd_auth_get_max_version(void) {
  if (!own_process() || !tailadb_auth_version(auth_abi))
    die(123);
  return tailadb_auth_version(auth_abi);
}

bool adbd_auth_supports_feature(int32_t feature) {
  (void)feature;
  return false;
}

void adbd_auth_send_tls_server_port(void *context, uint16_t port) {
  (void)port;
  if (!own_process() || context != &auth_context)
    die(123);
}

// API 37 imports these functions even when using V1 authorization. Do not pass
// our private context to libadbd_auth, which expects its own C++ object. An
// isolated acceptfd listener must never register a LAN discovery service.
uint32_t adbd_auth_register_service(void *context, const char *instance, const char *service,
                                    uint16_t port) {
  (void)port;
  if (!own_process() || context != &auth_context)
    die(123);
  if (!tailadb_mdns_names_valid(instance, service))
    return 1; // ADBD_AUTH_REGISTER_BAD_NAME
  AUTH_WARN("unexpected mDNS registration in isolated listener");
  die(123);
  return 1;
}

uint32_t adbd_auth_unregister_service(void *context, const char *instance, const char *service) {
  if (!own_process() || context != &auth_context)
    die(123);
  if (!tailadb_mdns_names_valid(instance, service))
    return 1; // ADBD_AUTH_UNREGISTER_BAD_NAME
  AUTH_WARN("unexpected mDNS removal in isolated listener");
  die(123);
  return 1;
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
  const char *ip = getenv("TAILADB_IP"), *port = getenv("TAILADB_PORT"), *fd = getenv("TAILADB_FD"),
             *control = getenv("TAILADB_CONTROL_FD"), *su_directory = getenv("TAILADB_SU_DIR"),
             *shell_hosts = getenv("TAILADB_ALLOWED_SHELL");
  auth_abi = tailadb_auth_abi(getenv("TAILADB_AUTH_ABI"));
  if (!ip || !port || !fd || !control || !su_directory || !shell_hosts ||
      auth_abi == TAILADB_AUTH_UNKNOWN || !parse_allowed_shell(shell_hosts) ||
      strcmp(su_directory, "/data/local/tmp/net.ryuya_dev.tailadb"))
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
  if (snprintf(shell_path, sizeof(shell_path),
               "PATH=%s:/product/bin:/apex/com.android.runtime/bin:"
               "/apex/com.android.art/bin:/system_ext/bin:/system/bin",
               su_directory) >= (int)sizeof(shell_path))
    die(120);
  struct sockaddr_in actual = {0};
  socklen_t len = sizeof(actual);
  if (getsockname(inherited_fd, (struct sockaddr *)&actual, &len) || len != sizeof(actual) ||
      actual.sin_family != AF_INET || actual.sin_addr.s_addr != endpoint.sin_addr.s_addr ||
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
  unsetenv("TAILADB_SU_DIR");
  unsetenv("TAILADB_ALLOWED_SHELL");
  unsetenv("TAILADB_AUTH_ABI");
}

const prop_info *__system_property_find(const char *name) {
  int i = index_of(name);
  if (i >= 0)
    return (const prop_info *)&props[i];
  const prop_info *(*real)(const char *) = dlsym(RTLD_NEXT, "__system_property_find");
  if (!real)
    die(120);
  return real(name);
}

void __system_property_read_callback(const prop_info *pi,
                                     void (*cb)(void *, const char *, const char *, uint32_t),
                                     void *cookie) {
  int i = fake_index(pi);
  if (i >= 0) {
    cb(cookie, props[i].name, props[i].value, 2);
    return;
  }
  void (*real)(const prop_info *, void (*)(void *, const char *, const char *, uint32_t), void *) =
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
  uint32_t (*real)(const prop_info *) = dlsym(RTLD_NEXT, "__system_property_serial");
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
  bool (*real)(const prop_info *, uint32_t, uint32_t *, const struct timespec *) =
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
  int (*real)(const char *, const char *) = dlsym(RTLD_NEXT, "__system_property_set");
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
  if (getuid() != 2000 || geteuid() != 2000 || getgid() != 2000 || getegid() != 2000 ||
      prctl(PR_SET_PDEATHSIG, SIGKILL) || getppid() != parent_pid)
    die(126);
  struct __user_cap_header_struct header = {.version = _LINUX_CAPABILITY_VERSION_3, .pid = 0};
  struct __user_cap_data_struct capabilities[2] = {0};
  if (syscall(__NR_capget, &header, capabilities) || capabilities[0].effective ||
      capabilities[0].permitted || capabilities[0].inheritable || capabilities[1].effective ||
      capabilities[1].permitted || capabilities[1].inheritable)
    die(126);
  char context[256] = {0};
  if (!read_context(context, sizeof(context)) || strcmp(context, "u:r:adbd:s0"))
    die(126);
  require_cloexec(inherited_fd);
  require_cloexec(control_fd);
}

static bool is_listener(int fd) {
  if (!own_process() || inherited_fd < 0)
    return false;
  struct sockaddr_in actual = {0};
  socklen_t length = sizeof(actual);
  return !getsockname(fd, (struct sockaddr *)&actual, &length) && length == sizeof(actual) &&
         actual.sin_family == AF_INET && actual.sin_port == endpoint.sin_port &&
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
  unsigned minimum = __atomic_fetch_add(&next_transport_fd, 1, __ATOMIC_RELAXED);
  if (minimum > 4096)
    minimum = 128;
  int stable_fd = fcntl(fd, F_DUPFD_CLOEXEC, (int)minimum);
  int duplicate_error = errno;
  close(fd);
  if (stable_fd < 0) {
    errno = duplicate_error;
    return -1;
  }
  fd = stable_fd;
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
  active_peer = peer.sin_addr.s_addr;
  token_valid = true;
  char peer_text[INET_ADDRSTRLEN] = {0};
  inet_ntop(AF_INET, &peer.sin_addr, peer_text, sizeof(peer_text));
  AUTH_INFO("socket accepted fd=%d peer=%s", fd, peer_text);
  struct control_message message = {.type = CONTROL_CONNECTED, .peer = peer.sin_addr.s_addr};
  memcpy(message.token, active_token, sizeof(active_token));
  send_control(&message);
  pthread_mutex_unlock(&session_lock);
  return fd;
}

int accept4(int fd, struct sockaddr *address, socklen_t *length, int flags) {
  int (*real)(int, struct sockaddr *, socklen_t *, int) = dlsym(RTLD_NEXT, "accept4");
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

static void session_closed(int fd) {
  if (own_process()) {
    pthread_mutex_lock(&session_lock);
    if (fd == active_fd) {
      AUTH_INFO("socket close fd=%d", fd);
      struct control_message message = {.type = CONTROL_CLOSED};
      send_control(&message);
      active_fd = -1;
      active_peer = 0;
      token_valid = false;
      secure_zero(active_token, sizeof(active_token));
    }
    pthread_mutex_unlock(&session_lock);
  }
}

int close(int fd) {
  int (*real)(int) = dlsym(RTLD_NEXT, "close");
  if (!real)
    die(120);
  session_closed(fd);
  return real(fd);
}

int android_fdsan_close_with_tag(int fd, uint64_t owner_tag) {
  int (*real)(int, uint64_t) = dlsym(RTLD_NEXT, "android_fdsan_close_with_tag");
  if (!real)
    die(120);
  session_closed(fd);
  return real(fd, owner_tag);
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

  int (*real_execve)(const char *, char *const[], char *const[]) = dlsym(RTLD_NEXT, "execve");
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
  if (broker_flags < 0 || fcntl(broker[0], F_SETFD, broker_flags & ~FD_CLOEXEC) ||
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
  snprintf(broker_environment, sizeof(broker_environment), "TAILADB_BROKER_FD=%d", broker[0]);

  char *updated[259];
  size_t count = 0, input = 0;
  for (; environment[input] && input < 255; input++)
    if (strncmp(environment[input], "PATH=", 5))
      updated[count++] = environment[input];
  if (environment[input]) {
    secure_zero(session, sizeof(session));
    close(broker[0]);
    errno = E2BIG;
    return -1;
  }
  updated[count++] = session;
  updated[count++] = broker_environment;
  updated[count++] = shell_path;
  updated[count] = NULL;
  int result = real_execve(path, arguments, updated);
  close(broker[0]);
  return result;
}
