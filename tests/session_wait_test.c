// SPDX-License-Identifier: MIT
#define _GNU_SOURCE
#include "../native/session_wait.h"

#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>

struct waiter {
  const struct tailadb_session_wait *state;
  int peer;
  atomic_int result;
};

static void *wait_for_slot(void *data) {
  struct waiter *waiter = data;
  atomic_store(&waiter->result, tailadb_session_wait_poll(waiter->state, waiter->peer));
  return NULL;
}

static void pause_briefly(void) {
  struct timespec delay = {.tv_nsec = 80000000};
  nanosleep(&delay, NULL);
}

int main(void) {
  alarm(5);
  struct tailadb_session_wait state;
  assert(tailadb_session_wait_init(&state));
  assert(fcntl(state.read_fd, F_GETFD) & FD_CLOEXEC);
  assert(fcntl(state.write_fd, F_GETFD) & FD_CLOEXEC);
  assert(fcntl(state.read_fd, F_GETFL) & O_NONBLOCK);
  assert(fcntl(state.write_fd, F_GETFL) & O_NONBLOCK);

  int peer[2];
  assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, peer));
  struct waiter waiter = {.state = &state, .peer = peer[0], .result = -2};
  pthread_t thread;
  assert(!pthread_create(&thread, NULL, wait_for_slot, &waiter));
  assert(write(peer[1], "CNXN", 4) == 4);
  pause_briefly();
  assert(atomic_load(&waiter.result) == -2);
  assert(tailadb_session_wait_signal(&state));
  assert(!pthread_join(thread, NULL));
  assert(atomic_load(&waiter.result) == 1);
  char data[4];
  assert(read(peer[0], data, sizeof(data)) == 4 && !memcmp(data, "CNXN", 4));

  // A signal arriving before poll starts must not be lost.
  assert(tailadb_session_wait_signal(&state));
  assert(tailadb_session_wait_poll(&state, peer[0]) == 1);

  // Notifications are coalesced without blocking even when the pipe fills.
  for (unsigned i = 0; i < 100000; ++i)
    assert(tailadb_session_wait_signal(&state));
  assert(tailadb_session_wait_poll(&state, peer[0]) == 1);

  // A waiting peer can leave without waiting for the active owner's release.
#ifdef POLLRDHUP
  atomic_store(&waiter.result, -2);
  assert(!pthread_create(&thread, NULL, wait_for_slot, &waiter));
  pause_briefly();
  close(peer[1]);
  assert(!pthread_join(thread, NULL));
  assert(atomic_load(&waiter.result) == 0);
#else
  // Darwin poll does not report an unread TCP half-close with an empty event mask.
  // The Linux/Android integration test covers the production POLLRDHUP path.
  close(peer[1]);
#endif
  close(peer[0]);
  close(state.read_fd);
  close(state.write_fd);
  puts("Session wait: PASS (data isolation, notification races, coalescing; peer FIN on Linux)");
  return 0;
}
