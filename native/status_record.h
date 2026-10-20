// SPDX-License-Identifier: MIT
#ifndef TAILADB_STATUS_RECORD_H
#define TAILADB_STATUS_RECORD_H

#include <stddef.h>
#include <string.h>

enum tailadb_status_record {
  TAILADB_STATUS_INVALID,
  TAILADB_STATUS_ACTIVE,
  TAILADB_STATUS_TERMINAL
};

// A status file contains exactly one bounded protocol record. Unknown active
// states still require a live, lock-owning backend; only STOPPED/ERROR_* survive
// the backend's exit. Never replay a peer alongside a terminal record.
static inline enum tailadb_status_record tailadb_status_record(const char *record) {
  if (!record || strncmp(record, "TAILADB\t", 8))
    return TAILADB_STATUS_INVALID;
  const char *state = record + 8;
  size_t length = 0;
  while ((state[length] >= 'A' && state[length] <= 'Z') || state[length] == '_') {
    if (++length > 40)
      return TAILADB_STATUS_INVALID;
  }
  if (length < 2 || state[length] != '\t')
    return TAILADB_STATUS_INVALID;
  const char *newline = strchr(state + length + 1, '\n');
  if (!newline || newline[1])
    return TAILADB_STATUS_INVALID;
  if ((length == 7 && !memcmp(state, "STOPPED", 7)) || (length > 6 && !memcmp(state, "ERROR_", 6)))
    return TAILADB_STATUS_TERMINAL;
  return TAILADB_STATUS_ACTIVE;
}

#endif
