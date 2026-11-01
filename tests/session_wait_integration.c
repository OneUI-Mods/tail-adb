// SPDX-License-Identifier: MIT
#define TAILADB_HOST_AUTH_TEST 1
#define constructor unused
#include "../native/policy.c"
#undef constructor

#include <assert.h>
#include <stdatomic.h>

struct pending_accept {
  int fd;
  struct sockaddr_in peer;
  atomic_int result;
};

static void *accept_pending(void *data) {
  struct pending_accept *pending = data;
  atomic_store(&pending->result,
               accepted(pending->fd, (struct sockaddr *)&pending->peer, sizeof(pending->peer)));
  return NULL;
}

static void tcp_pair(int *server, int *client) {
  int listener = socket(AF_INET, SOCK_STREAM, 0);
  assert(listener >= 0);
  struct sockaddr_in local = {.sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
  assert(!bind(listener, (struct sockaddr *)&local, sizeof(local)));
  assert(!listen(listener, 1));
  socklen_t length = sizeof(local);
  assert(!getsockname(listener, (struct sockaddr *)&local, &length));
  *client = socket(AF_INET, SOCK_STREAM, 0);
  assert(*client >= 0 && !connect(*client, (struct sockaddr *)&local, sizeof(local)));
  // Bypass the interposed listener wrapper; accepted() is the function under test.
  int (*real_accept)(int, struct sockaddr *, socklen_t *) = dlsym(RTLD_NEXT, "accept");
  *server = real_accept(listener, NULL, NULL);
  assert(*server >= 0);
  close(listener);
}

static struct control_message receive_control(int fd, enum control_type expected) {
  struct control_message message;
  assert(recv(fd, &message, sizeof(message), 0) == sizeof(message));
  assert(message.type == (unsigned)expected);
  return message;
}

int main(void) {
  alarm(8);
  assert(tailadb_session_wait_init(&session_wait));
  int control[2];
  assert(!socketpair(AF_UNIX, SOCK_SEQPACKET, 0, control));
  control_fd = control[0];
  daemon_pid = getpid();
  configured = true;

  int first, first_client;
  tcp_pair(&first, &first_client);
  struct sockaddr_in first_peer = {.sin_family = AF_INET,
                                   .sin_addr.s_addr = inet_addr("100.64.0.10")};
  int owner = accepted(first, (struct sockaddr *)&first_peer, sizeof(first_peer));
  assert(owner >= 0 && owner == active_fd && token_valid);
  struct control_message initial = receive_control(control[1], CONTROL_CONNECTED);

  int second, second_client;
  tcp_pair(&second, &second_client);
  struct pending_accept pending = {
      .fd = second,
      .peer = {.sin_family = AF_INET, .sin_addr.s_addr = inet_addr("100.64.0.20")},
      .result = -2};
  pthread_t thread;
  assert(!pthread_create(&thread, NULL, accept_pending, &pending));
  assert(send(second_client, "CNXN", 4, 0) == 4);
  usleep(100000);
  assert(atomic_load(&pending.result) == -2);
  pthread_mutex_lock(&session_lock);
  assert(active_fd == owner && active_peer == first_peer.sin_addr.s_addr);
  assert(!memcmp(initial.token, active_token, TOKEN_BYTES));
  pthread_mutex_unlock(&session_lock);
  assert(send(first_client, "live", 4, 0) == 4);
  char data[4];
  assert(recv(owner, data, 4, 0) == 4 && !memcmp(data, "live", 4));

  close(owner);
  receive_control(control[1], CONTROL_CLOSED);
  assert(!pthread_join(thread, NULL));
  int next = atomic_load(&pending.result);
  assert(next >= 0 && next == active_fd && active_peer == pending.peer.sin_addr.s_addr);
  struct control_message admitted = receive_control(control[1], CONTROL_CONNECTED);
  assert(memcmp(initial.token, admitted.token, TOKEN_BYTES));
  assert(recv(next, data, 4, 0) == 4 && !memcmp(data, "CNXN", 4));

  // Cancellation while data is queued must not replace the authenticated owner.
  int third, third_client;
  tcp_pair(&third, &third_client);
  pending.fd = third;
  atomic_store(&pending.result, -2);
  assert(!pthread_create(&thread, NULL, accept_pending, &pending));
  assert(send(third_client, "CNXN", 4, 0) == 4);
  usleep(100000);
  close(third_client);
  assert(!pthread_join(thread, NULL));
  assert(atomic_load(&pending.result) == -1);
  assert(active_fd == next && token_valid && !memcmp(active_token, admitted.token, TOKEN_BYTES));

  close(next);
  receive_control(control[1], CONTROL_CLOSED);
  assert(active_fd == -1 && !token_valid && active_peer == 0);
  for (unsigned i = 0; i < TOKEN_BYTES; ++i)
    assert(active_token[i] == 0);
  configured = false;
  close(first_client);
  close(second_client);
  close(control[0]);
  close(control[1]);
  close(session_wait.read_fd);
  close(session_wait.write_fd);
  puts("Session integration: PASS (live owner, parked data, handoff, fresh token, cancellation)");
  return 0;
}
