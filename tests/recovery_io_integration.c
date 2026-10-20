// SPDX-License-Identifier: MIT
// Disposable Linux container only. Exercise the real supervisor helpers and TCP socket options.
#define main supervisor_main
#include "../native/supervisor.c"
#undef main
#include "../native/transport_health.h"
#include <assert.h>

static int loopback(struct sockaddr_in *address) {
  int listener = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
  assert(listener >= 0);
  *address = (struct sockaddr_in){.sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
  assert(!bind(listener, (struct sockaddr *)address, sizeof(*address)) && !listen(listener, 4));
  socklen_t size = sizeof(*address);
  assert(!getsockname(listener, (struct sockaddr *)address, &size));
  return listener;
}

static void health(int listener, struct sockaddr_in *address, struct backend *backend,
                   struct authorization *allow, const char *expected) {
  int client = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
  assert(client >= 0 && !connect(client, (struct sockaddr *)address, sizeof(*address)));
  health_reply(listener, backend, allow);
  char text[128] = {0};
  ssize_t size = read(client, text, sizeof(text) - 1);
  assert(size >= 0 && !strcmp(text, expected));
  close(client);
}

int main(void) {
  struct sockaddr_in address;
  int listener = loopback(&address);
  struct backend backend = {.child = 42, .listener = -1, .control = -1};
  struct authorization allow = {0};
  health(listener, &address, &backend, &allow, ""); // Unknown hosts receive no state.
  // Loopback stands in for the VPN source inside this host-only test. Production allowlists can
  // only be populated by parse_authorization, which rejects all non-Tailscale addresses.
  assert(!parse_authorization("127.0.0.1", &allow));
  allow.count = 1;
  allow.addresses[0].s_addr = htonl(INADDR_LOOPBACK);
  health(listener, &address, &backend, &allow, "TAILADB_HEALTH\t1\tREADY\t42\n");
  backend.active = true;
  backend.peer = htonl(INADDR_LOOPBACK);
  health(listener, &address, &backend, &allow, "TAILADB_HEALTH\t1\tCHECK_ADB_SERVER\t42\n");
  backend.peer = inet_addr("100.64.0.12");
  health(listener, &address, &backend, &allow, "TAILADB_HEALTH\t1\tOTHER_HOST\t42\n");
  backend.child = 0;
  health(listener, &address, &backend, &allow, "TAILADB_HEALTH\t1\tWAITING\t0\n");

  int client = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
  assert(client >= 0 && !connect(client, (struct sockaddr *)&address, sizeof(address)));
  int accepted = accept(listener, NULL, NULL);
  assert(accepted >= 0 && tailadb_transport_health(accepted));
  int options[][3] = {{SOL_SOCKET, SO_KEEPALIVE, 1},
                      {IPPROTO_TCP, TCP_KEEPIDLE, 30},
                      {IPPROTO_TCP, TCP_KEEPINTVL, 10},
                      {IPPROTO_TCP, TCP_KEEPCNT, 3},
                      {IPPROTO_TCP, TCP_USER_TIMEOUT, 45000}};
  for (size_t i = 0; i < sizeof(options) / sizeof(options[0]); i++) {
    int value = 0;
    socklen_t size = sizeof(value);
    assert(!getsockopt(accepted, options[i][0], options[i][1], &value, &size));
    assert(value == options[i][2]);
  }
  close(client);
  close(accepted);

  int pair[2];
  assert(!socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair));
  backend.listener = listener;
  backend.control = pair[0];
  backend.active = true;
  memset(backend.token, 0x41, sizeof(backend.token));
  drop_backend(&backend);
  assert(backend.child == 0 && backend.listener == -1 && backend.control == -1 && !backend.active);
  assert(!backend.peer);
  for (size_t i = 0; i < sizeof(backend.token); i++)
    assert(!backend.token[i]);
  assert(fcntl(listener, F_GETFD) < 0 && errno == EBADF);
  assert(fcntl(pair[0], F_GETFD) < 0 && errno == EBADF);
  close(pair[1]);
  puts(
      "Recovery I/O: PASS (real health protocol, ACL, other host, TCP options, stale peer/FD cleanup)");
}
