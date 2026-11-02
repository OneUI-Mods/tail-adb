// SPDX-License-Identifier: MIT
#include "../native/network_recovery.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
  struct tailadb_recovery state = {0};
  assert(!tailadb_poll_timeout(&state, 100));
  tailadb_network_checked(&state, 100, true, false);
  assert(state.inspect_at == 15100);
  // Unrelated socket traffic must not move the absolute inspection deadline.
  for (int i = 101; i < 15100; i += 37)
    assert(tailadb_poll_timeout(&state, i) == 15100 - i);
  assert(!tailadb_poll_timeout(&state, 15100));

  tailadb_network_event(&state, 200);
  assert(state.inspect_at == 1100);
  for (int i = 201; i < 1100; i++)
    tailadb_network_event(&state, i);
  assert(state.inspect_at == 1100); // A link event storm cannot postpone the check.

  int64_t now = 1100;
  const int waits[] = {1000, 2000, 4000, 8000, 15000, 30000, 60000, 60000};
  for (size_t i = 0; i < sizeof(waits) / sizeof(waits[0]); i++) {
    tailadb_network_checked(&state, now, false, false);
    assert(state.inspect_at == now + waits[i]);
    now = state.inspect_at;
  }
  tailadb_network_event(&state, now - 500);
  assert(state.inspect_at < now); // A restored VPN wakes the otherwise slow waiting loop.
  tailadb_network_checked(&state, now, true, false);
  assert(state.waiting_attempts == 0);
  assert(state.inspect_at == now + 15000);
  tailadb_network_checked(&state, now + 15000, true, true);
  assert(state.inspect_at == now + 45000);
  tailadb_network_checked(&state, now + 45000, true, true);
  assert(state.inspect_at == now + 105000);
  tailadb_network_event(&state, now + 46000);
  assert(state.inspect_at == now + 46250);
  tailadb_network_checked(&state, now + 46250, true, true);
  assert(state.inspect_at == now + 61250); // A real event returns to the fast cadence.
  now += 62000;

  tailadb_bind_retry(&state, now);
  tailadb_network_event(&state, now);
  assert(tailadb_poll_timeout(&state, now) == 1000); // No retry storm during address churn.
  assert(tailadb_transient_bind_error(EADDRNOTAVAIL));
  assert(tailadb_transient_bind_error(ENETDOWN));
  assert(!tailadb_transient_bind_error(EACCES));
  assert(!tailadb_transient_bind_error(EADDRINUSE));

  state.child_started = now;
  assert(tailadb_child_failed(&state, now + 10));
  state.child_started = now + 1000;
  assert(tailadb_child_failed(&state, now + 1010));
  state.child_started = now + 3000;
  assert(!tailadb_child_failed(&state, now + 3010)); // Three rapid abnormal exits still stop.
  state.child_started = now + 5000;
  tailadb_child_stable(&state, now + 65000);
  assert(state.child_failures == 0);
  assert(tailadb_child_failed(&state, now + 65001));
  assert(state.child_failures == 1); // A later failure after recovery is not a crash loop.
  puts("Network recovery: PASS (deadlines, flaps, backoff, bind errors, stable/crashing child)");
}
