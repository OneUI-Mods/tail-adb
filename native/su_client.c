// SPDX-License-Identifier: MIT
// Copyright (c) 2026 ryuya0124

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#define TOKEN_BYTES 32
#define MAX_REQUEST 4096

struct request {
  uint8_t token[TOKEN_BYTES];
  uint32_t argc;
  uint32_t bytes;
  char arguments[];
};

static int fail(const char *message) {
  dprintf(STDERR_FILENO, "su: %s\n", message);
  return 1;
}

static int hex_value(char c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}

static int read_token(uint8_t token[TOKEN_BYTES]) {
  const char *value = getenv("TAILADB_SESSION");
  if (!value || strlen(value) != TOKEN_BYTES * 2)
    return -1;
  for (size_t i = 0; i < TOKEN_BYTES; i++) {
    int high = hex_value(value[i * 2]), low = hex_value(value[i * 2 + 1]);
    if (high < 0 || low < 0)
      return -1;
    token[i] = (uint8_t)((high << 4) | low);
  }
  return 0;
}

int main(int argc, char **argv) {
  const char *broker_value = getenv("TAILADB_BROKER_FD");
  char *broker_end = NULL;
  long broker = broker_value ? strtol(broker_value, &broker_end, 10) : -1;
  int socket_type = 0;
  socklen_t socket_type_length = sizeof(socket_type);
  if (!broker_value || !broker_end || *broker_end || broker < 3 ||
      broker > INT32_MAX || fcntl((int)broker, F_GETFD) < 0 ||
      getsockopt((int)broker, SOL_SOCKET, SO_TYPE, &socket_type,
                 &socket_type_length) ||
      socket_type != SOCK_SEQPACKET)
    return fail("this shell is not attached to Tail ADB");

  size_t bytes = 0;
  for (int i = 1; i < argc; i++) {
    size_t length = strlen(argv[i]) + 1;
    if (length > MAX_REQUEST || bytes > MAX_REQUEST - length)
      return fail("arguments are too large");
    bytes += length;
  }
  size_t total = sizeof(struct request) + bytes;
  struct request *request = calloc(1, total);
  if (!request)
    return fail("out of memory");
  if (read_token(request->token)) {
    free(request);
    return fail("missing session authorization");
  }
  request->argc = (uint32_t)(argc - 1);
  request->bytes = (uint32_t)bytes;
  char *next = request->arguments;
  for (int i = 1; i < argc; i++) {
    size_t length = strlen(argv[i]) + 1;
    memcpy(next, argv[i], length);
    next += length;
  }

  int fd = dup((int)broker);
  if (fd < 0) {
    free(request);
    return fail("privilege broker unavailable");
  }
  int descriptor_flags = fcntl(fd, F_GETFD);
  if (descriptor_flags < 0 ||
      fcntl(fd, F_SETFD, descriptor_flags | FD_CLOEXEC)) {
    close(fd);
    free(request);
    return fail("could not protect broker descriptor");
  }

  char control[CMSG_SPACE(sizeof(int) * 3)] = {0};
  struct iovec iov = {.iov_base = request, .iov_len = total};
  struct msghdr message = {.msg_iov = &iov,
                           .msg_iovlen = 1,
                           .msg_control = control,
                           .msg_controllen = sizeof(control)};
  struct cmsghdr *header = CMSG_FIRSTHDR(&message);
  header->cmsg_level = SOL_SOCKET;
  header->cmsg_type = SCM_RIGHTS;
  header->cmsg_len = CMSG_LEN(sizeof(int) * 3);
  int streams[3] = {STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO};
  memcpy(CMSG_DATA(header), streams, sizeof(streams));
  if (sendmsg(fd, &message, MSG_NOSIGNAL) != (ssize_t)total) {
    close(fd);
    free(request);
    return fail("could not send privilege request");
  }
  free(request);

  uint8_t granted = 0;
  if (recv(fd, &granted, 1, 0) != 1 || granted != 1) {
    close(fd);
    return fail("this Tailscale host is not allowed to elevate");
  }
  int status = 1 << 8;
  ssize_t received;
  do {
    received = recv(fd, &status, sizeof(status), 0);
  } while (received < 0 && errno == EINTR);
  close(fd);
  if (received != sizeof(status))
    return fail("privileged command ended unexpectedly");
  if (WIFEXITED(status))
    return WEXITSTATUS(status);
  if (WIFSIGNALED(status))
    return 128 + WTERMSIG(status);
  return 1;
}
