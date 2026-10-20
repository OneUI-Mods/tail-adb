#!/usr/bin/env bash
# Run inside a disposable Linux environment, never on Android or a live backend host.
set -euo pipefail
if [[ "$(uname -s)" != Linux || "$(id -u)" != 0 || -e /system/bin/sha256sum ]]; then
  echo 'Use a disposable Linux environment as root; Android is not a test host.' >&2
  exit 1
fi
test_root="$(cd "$(dirname "$0")/.." && pwd)"
test_output="$(mktemp -d)"
trap 'rm -rf "$test_output"' EXIT
for test_name in backend_status auth_policy; do
  "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Wno-unused-function \
    -I"$test_root/tests/host_include" \
    "$test_root/tests/${test_name}_integration.c" -o "$test_output/$test_name" -ldl -pthread
  "$test_output/$test_name"
done
