// SPDX-License-Identifier: MIT
#ifndef TAILADB_HOST_MULTINETWORK_H
#define TAILADB_HOST_MULTINETWORK_H
#include <stdint.h>
#include <stdlib.h>

static inline int android_setsocknetwork(uint64_t network, int fd) {
  (void)network;
  (void)fd;
  abort(); // No listener may be opened by the status integration test.
}
#endif
