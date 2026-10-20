// SPDX-License-Identifier: MIT
#define _GNU_SOURCE
#include "../native/bounded_command.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
  char *ok[] = {"/bin/sh", "-c", "printf test", NULL};
  char *text = tailadb_capture(ok, 4, 1000, NULL);
  assert(text && !strcmp(text, "test"));
  free(text);
  assert(!tailadb_capture(ok, 3, 1000, NULL) && errno == EOVERFLOW);
  char *failed[] = {"/bin/sh", "-c", "printf test; exit 4", NULL};
  assert(!tailadb_capture(failed, 16, 1000, NULL) && errno == EIO);
  char *hung[] = {"/bin/sleep", "2", NULL};
  int64_t start = tailadb_now_ms();
  assert(!tailadb_capture(hung, 16, 50, NULL) && errno == ETIMEDOUT);
  assert(tailadb_now_ms() - start < 1000);
  volatile sig_atomic_t cancelled = 1;
  assert(!tailadb_capture(ok, 16, 1000, &cancelled) && errno == ECANCELED);
  // No successful or killed child is left as a zombie.
  assert(waitpid(-1, NULL, WNOHANG) < 0 && errno == ECHILD);
  puts("Bounded inspection: PASS (exact bound, overflow, failure, timeout, cancellation, reaping)");
}
