// SPDX-License-Identifier: MIT
#include "../native/status_record.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
  assert(tailadb_status_record("TAILADB\tERROR_COMPAT\t\n") == TAILADB_STATUS_TERMINAL);
  assert(tailadb_status_record("TAILADB\tERROR_DAEMON\tchild_exit_123\n") ==
         TAILADB_STATUS_TERMINAL);
  assert(tailadb_status_record("TAILADB\tSTOPPED\t\n") == TAILADB_STATUS_TERMINAL);
  assert(tailadb_status_record("TAILADB\tLISTENING\t100.64.0.10:5555\n") == TAILADB_STATUS_ACTIVE);
  assert(tailadb_status_record("TAILADB\tWAITING_TAILSCALE\t0\n") == TAILADB_STATUS_ACTIVE);
  assert(tailadb_status_record("TAILADB\tSTARTING\t\n") == TAILADB_STATUS_ACTIVE);
  assert(tailadb_status_record("TAILADB\tPEER\t100.64.0.11\t1\n") == TAILADB_STATUS_ACTIVE);
  assert(tailadb_status_record(NULL) == TAILADB_STATUS_INVALID);
  assert(tailadb_status_record("") == TAILADB_STATUS_INVALID);
  assert(tailadb_status_record("TAILADB\tSTOPPED\t") == TAILADB_STATUS_INVALID);
  assert(tailadb_status_record("TAILADB\tERROR_COMPAT\t\nTAILADB\tLISTENING\t\n") ==
         TAILADB_STATUS_INVALID);
  assert(tailadb_status_record("TAILADB\tSTOPPED_EXTRA\t\n") == TAILADB_STATUS_ACTIVE);
  assert(tailadb_status_record("TAILADB\tE\t\n") == TAILADB_STATUS_INVALID);
  assert(tailadb_status_record("TAILADB\tERROR_123\t\n") == TAILADB_STATUS_INVALID);
  puts("Status records: PASS (terminal errors retained; stale live and peer records need owner)");
  return 0;
}
