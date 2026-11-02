// SPDX-License-Identifier: MIT
#include "../native/package_uids.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
  struct tailadb_package_uids uids = {0};
  assert(tailadb_parse_package_uids("package:com.tailscale.ipn uid:10495,1010495,15010495\n", &uids));
  assert(uids.count == 3 && tailadb_has_package_uid(&uids, 1010495));
  assert(!tailadb_has_package_uid(&uids, 1049));
  assert(tailadb_parse_package_uids("package:com.tailscale.ipn uid:10495\n"
                                  "package:com.tailscale.ipn uid:1010495\n", &uids));
  assert(uids.count == 2);
  const char *invalid[] = {"", "package:com.tailscale.ipn.debug uid:10495\n",
                           "package:com.tailscale.ipn uid:10495,\n",
                           "package:com.tailscale.ipn uid:10495x\n",
                           "package:com.tailscale.ipn uid:-1\n",
                           "package:com.tailscale.ipn uid:2147483648\n",
                           "package:com.tailscale.ipn uid:10495\npackage:com.tailscale.ipn uid:x\n"};
  for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
    struct tailadb_package_uids empty = {0};
    assert(!tailadb_parse_package_uids(invalid[i], &empty) && !empty.count);
  }
  puts("Package UIDs: PASS (multi-profile formats, exact identity, malformed output rejected)");
}
