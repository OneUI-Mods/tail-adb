// SPDX-License-Identifier: MIT
#ifndef TAILADB_NETWORK_RECOVERY_H
#define TAILADB_NETWORK_RECOVERY_H

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <limits.h>

struct tailadb_recovery {
  int64_t inspect_at;
  int64_t retry_at;
  int64_t last_inspected;
  int64_t child_started;
  unsigned waiting_attempts;
  unsigned bind_attempts;
  unsigned child_failures;
  unsigned stable_checks;
  bool network_dirty;
};

static inline int tailadb_retry_delay(unsigned attempt) {
  return attempt < 4 ? 1000 << attempt : 15000;
}

static inline bool tailadb_transient_bind_error(int error) {
#ifdef ENONET
  if (error == ENONET)
    return true;
#endif
  return error == ENETDOWN || error == ENETUNREACH || error == ENODEV || error == EADDRNOTAVAIL ||
         error == ENOENT || error == EINTR || error == EAGAIN;
}

static inline void tailadb_network_event(struct tailadb_recovery *state, int64_t now) {
  state->network_dirty = true;
  state->stable_checks = state->waiting_attempts = 0;
  int64_t due = now + 250;
  if (due < state->last_inspected + 1000)
    due = state->last_inspected + 1000;
  if (due < state->inspect_at)
    state->inspect_at = due;
}

static inline void tailadb_network_checked(struct tailadb_recovery *state, int64_t now,
                                           bool available, bool unchanged) {
  state->last_inspected = now;
  unsigned attempt = state->waiting_attempts;
  if (available) {
    state->waiting_attempts = 0;
    if (!unchanged || state->network_dirty)
      state->stable_checks = 0;
    else if (state->stable_checks < 2)
      state->stable_checks++;
    state->inspect_at = now + (15000 << state->stable_checks);
  } else {
    state->stable_checks = 0;
    if (state->waiting_attempts < 6)
      state->waiting_attempts++;
    int delay = attempt < 4 ? 1000 << attempt : attempt == 4 ? 15000 : attempt == 5 ? 30000 : 60000;
    state->inspect_at = now + delay;
  }
  state->network_dirty = false;
}

static inline void tailadb_bind_retry(struct tailadb_recovery *state, int64_t now) {
  state->retry_at = now + tailadb_retry_delay(state->bind_attempts);
  if (state->bind_attempts < 4)
    state->bind_attempts++;
  state->inspect_at = state->retry_at;
}

static inline void tailadb_child_stable(struct tailadb_recovery *state, int64_t now) {
  if (state->child_started && now - state->child_started >= 60000)
    state->child_failures = 0;
}

static inline bool tailadb_child_failed(struct tailadb_recovery *state, int64_t now) {
  tailadb_child_stable(state, now);
  state->child_started = 0;
  if (++state->child_failures >= 3)
    return false;
  state->retry_at = now + tailadb_retry_delay(state->child_failures - 1);
  state->inspect_at = state->retry_at;
  return true;
}

static inline int tailadb_poll_timeout(const struct tailadb_recovery *state, int64_t now) {
  int64_t due = state->inspect_at > state->retry_at ? state->inspect_at : state->retry_at;
  if (due <= now)
    return 0;
  return due - now > INT_MAX ? INT_MAX : (int)(due - now);
}

#endif
