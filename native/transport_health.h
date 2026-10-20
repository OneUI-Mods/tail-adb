// SPDX-License-Identifier: MIT
#ifndef TAILADB_TRANSPORT_HEALTH_H
#define TAILADB_TRANSPORT_HEALTH_H

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdbool.h>
#include <sys/socket.h>

// Preserve a live Tailscale transport across uplink changes, but eventually release a dead peer's
// single transport slot. USER_TIMEOUT also bounds an outstanding transfer during a black hole.
static inline bool tailadb_transport_health(int fd) {
  int enabled = 1, idle = 30, interval = 10, count = 3, timeout_ms = 45000;
  return !setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &enabled, sizeof(enabled)) &&
         !setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle)) &&
         !setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &interval, sizeof(interval)) &&
         !setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &count, sizeof(count)) &&
         !setsockopt(fd, IPPROTO_TCP, TCP_USER_TIMEOUT, &timeout_ms, sizeof(timeout_ms));
}

#endif
