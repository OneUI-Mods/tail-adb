// SPDX-License-Identifier: MIT
// Linux host integration tests only. Never include this directory in APK builds.
#ifndef TAILADB_HOST_LOG_H
#define TAILADB_HOST_LOG_H
#include <stdlib.h>
#define ANDROID_LOG_WARN 5
#define ANDROID_LOG_INFO 4

static inline int __android_log_print(int priority, const char *tag, const char *format, ...) {
  (void)priority;
  (void)tag;
  (void)format;
#ifdef TAILADB_HOST_AUTH_TEST
  return 0;
#else
  abort(); // Status tests must not enter the Android broker/network code.
#endif
}
#endif
