// SPDX-License-Identifier: MIT
#ifndef TAILADB_SESSION_WAIT_H
#define TAILADB_SESSION_WAIT_H

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdbool.h>
#include <unistd.h>

// One listener thread waits for the current authenticated transport to close.
// The pipe is private, nonblocking and close-on-exec; shell children never inherit it.
struct tailadb_session_wait {
  int read_fd;
  int write_fd;
};

static inline bool tailadb_session_wait_init(struct tailadb_session_wait *state) {
  int descriptors[2];
  if (pipe(descriptors))
    return false;
  for (unsigned i = 0; i < 2; ++i) {
    int flags = fcntl(descriptors[i], F_GETFL);
    if (flags < 0 || fcntl(descriptors[i], F_SETFL, flags | O_NONBLOCK) ||
        fcntl(descriptors[i], F_SETFD, FD_CLOEXEC)) {
      int saved = errno;
      close(descriptors[0]);
      close(descriptors[1]);
      errno = saved;
      return false;
    }
  }
  state->read_fd = descriptors[0];
  state->write_fd = descriptors[1];
  return true;
}

static inline bool tailadb_session_wait_signal(const struct tailadb_session_wait *state) {
  char byte = 1;
  ssize_t result;
  do {
    result = write(state->write_fd, &byte, sizeof(byte));
  } while (result < 0 && errno == EINTR);
  // A full pipe already contains the notification; the writer must never block.
  return result == 1 || (result < 0 && (errno == EAGAIN || errno == EWOULDBLOCK));
}

// Return 1 for a slot-change notification, 0 for a disconnected waiting peer,
// and -1 on failure. Do not consume CNXN or any other unauthenticated peer data.
static inline int tailadb_session_wait_poll(const struct tailadb_session_wait *state, int peer) {
  short peer_events = 0;
#ifdef POLLRDHUP
  peer_events |= POLLRDHUP;
#endif
  struct pollfd descriptors[] = {{.fd = peer, .events = peer_events},
                                 {.fd = state->read_fd, .events = POLLIN}};
  for (;;) {
    int result = poll(descriptors, 2, -1);
    if (result < 0) {
      if (errno == EINTR)
        continue;
      return -1;
    }
    short gone = POLLHUP | POLLERR | POLLNVAL;
#ifdef POLLRDHUP
    gone |= POLLRDHUP;
#endif
    if (descriptors[0].revents & gone)
      return 0;
    if (descriptors[1].revents & (POLLHUP | POLLERR | POLLNVAL)) {
      errno = EIO;
      return -1;
    }
    if (descriptors[1].revents & POLLIN) {
      char bytes[128];
      for (;;) {
        ssize_t count = read(state->read_fd, bytes, sizeof(bytes));
        if (count > 0 || (count < 0 && errno == EINTR))
          continue;
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
          return 1;
        errno = EIO;
        return -1;
      }
    }
  }
}

#endif
