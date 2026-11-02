// SPDX-License-Identifier: MIT
#ifndef TAILADB_PACKAGE_UIDS_H
#define TAILADB_PACKAGE_UIDS_H

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define TAILADB_MAX_PACKAGE_UIDS 64

struct tailadb_package_uids {
  unsigned values[TAILADB_MAX_PACKAGE_UIDS];
  size_t count;
};

static inline bool tailadb_has_package_uid(const struct tailadb_package_uids *uids, unsigned uid) {
  for (size_t i = 0; i < uids->count; i++)
    if (uids->values[i] == uid)
      return true;
  return false;
}

// Samsung emits a comma-separated UID list for --user all; AOSP versions may
// emit one line per user. Never accept a numeric prefix or a similarly named app.
static inline bool tailadb_parse_package_uids(const char *text, struct tailadb_package_uids *out) {
  *out = (struct tailadb_package_uids){0};
  struct tailadb_package_uids parsed = {0};
  const char prefix[] = "package:com.tailscale.ipn uid:";
  while (*text) {
    const char *end = strchr(text, '\n');
    if (!end)
      end = text + strlen(text);
    if ((size_t)(end - text) >= sizeof(prefix) - 1 &&
        !memcmp(text, prefix, sizeof(prefix) - 1)) {
      const char *p = text + sizeof(prefix) - 1;
      for (;;) {
        if (p == end || *p < '0' || *p > '9')
          return false;
        char *next;
        errno = 0;
        unsigned long uid = strtoul(p, &next, 10);
        if (errno || next > end || uid > INT_MAX)
          return false;
        if (!tailadb_has_package_uid(&parsed, (unsigned)uid)) {
          if (parsed.count == TAILADB_MAX_PACKAGE_UIDS)
            return false;
          parsed.values[parsed.count++] = (unsigned)uid;
        }
        p = next;
        if (p < end && *p == ',') {
          p++;
          continue;
        }
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\r'))
          p++;
        if (p != end)
          return false;
        break;
      }
    }
    text = *end ? end + 1 : end;
  }
  if (!parsed.count)
    return false;
  *out = parsed;
  return true;
}
#endif
