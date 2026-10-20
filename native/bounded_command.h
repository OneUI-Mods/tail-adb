// SPDX-License-Identifier: MIT
#ifndef TAILADB_BOUNDED_COMMAND_H
#define TAILADB_BOUNDED_COMMAND_H

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/prctl.h>
#endif

static inline int64_t tailadb_now_ms(void) {
  struct timespec now;
#ifdef CLOCK_BOOTTIME
  if (clock_gettime(CLOCK_BOOTTIME, &now))
#else
  if (clock_gettime(CLOCK_MONOTONIC, &now))
#endif
    abort();
  return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

// Fixed argv only. Binder inspection must not freeze the supervisor or delay stop indefinitely.
// Output and lifetime are bounded; no shell expansion or inherited standard input is used.
static char *tailadb_capture(char *const argv[], size_t limit, int timeout_ms,
                             const volatile sig_atomic_t *cancelled) {
  if ((cancelled && *cancelled) || !limit || timeout_ms <= 0) {
    errno = ECANCELED;
    return NULL;
  }
  char *text = malloc(limit + 1);
  if (!text)
    return NULL;
  int channel[2];
  if (pipe(channel)) {
    free(text);
    return NULL;
  }
  for (int i = 0; i < 2; i++) {
    int flags = fcntl(channel[i], F_GETFD);
    if (flags < 0 || fcntl(channel[i], F_SETFD, flags | FD_CLOEXEC)) {
      close(channel[0]);
      close(channel[1]);
      free(text);
      return NULL;
    }
  }
  pid_t child = fork();
  if (!child) {
    signal(SIGTERM, SIG_DFL);
    signal(SIGINT, SIG_DFL);
#ifdef __linux__
    pid_t parent = getppid();
    if (prctl(PR_SET_PDEATHSIG, SIGKILL) || getppid() != parent)
      _exit(126);
#endif
    int null = open("/dev/null", O_RDWR);
    if (null < 0 || dup2(null, STDIN_FILENO) < 0 || dup2(null, STDERR_FILENO) < 0 ||
        dup2(channel[1], STDOUT_FILENO) < 0)
      _exit(126);
    if (null > STDERR_FILENO)
      close(null);
    close(channel[0]);
    close(channel[1]);
    execv(argv[0], argv);
    _exit(127);
  }
  close(channel[1]);
  int error = 0, status = 0;
  bool exited = false, eof = false;
  size_t used = 0;
  int flags = fcntl(channel[0], F_GETFL);
  if (child < 0 || flags < 0 || fcntl(channel[0], F_SETFL, flags | O_NONBLOCK))
    error = errno;
  int64_t deadline = tailadb_now_ms() + timeout_ms;
  while (!error && (!exited || !eof)) {
    if (cancelled && *cancelled) {
      error = ECANCELED;
      break;
    }
    if (tailadb_now_ms() >= deadline) {
      error = ETIMEDOUT;
      break;
    }
    char extra;
    ssize_t got =
        read(channel[0], used == limit ? &extra : text + used, used == limit ? 1 : limit - used);
    if (got > 0) {
      if (used == limit) {
        error = EOVERFLOW;
        break;
      }
      used += (size_t)got;
    } else if (!got)
      eof = true;
    else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
      error = errno;
      break;
    }
    if (!exited) {
      pid_t waited = waitpid(child, &status, WNOHANG);
      if (waited == child)
        exited = true;
      else if (waited < 0 && errno != EINTR) {
        error = errno;
        break;
      }
    }
    if (got <= 0 && (!exited || !eof)) {
      struct pollfd fd = {.fd = eof ? -1 : channel[0], .events = POLLIN};
      int64_t remaining = deadline - tailadb_now_ms();
      poll(&fd, 1, remaining > 50 ? 50 : remaining > 0 ? (int)remaining : 0);
    }
  }
  close(channel[0]);
  if (child > 0 && !exited) {
    kill(child, SIGKILL);
    while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
    }
  }
  if (!error && (!WIFEXITED(status) || WEXITSTATUS(status)))
    error = EIO;
  if (error) {
    free(text);
    errno = error;
    return NULL;
  }
  text[used] = 0;
  return text;
}

#endif
