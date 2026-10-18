#!/usr/bin/env bash
set -euo pipefail
native_ndk="$1"
native_output="$2"
case "$(uname -s)" in
  Darwin) native_host=darwin-x86_64 ;;
  Linux) native_host=linux-x86_64 ;;
  *)
    echo 'Unsupported native build host' >&2
    exit 1
    ;;
esac
native_compilers="$native_ndk/toolchains/llvm/prebuilt/$native_host/bin"
for native_abi in arm64-v8a x86_64; do
  case "$native_abi" in
    arm64-v8a) native_target=aarch64-linux-android ;;
    x86_64) native_target=x86_64-linux-android ;;
  esac
  mkdir -p "$native_output/$native_abi"
  "$native_compilers/${native_target}31-clang" \
    -O2 -Wall -Wextra -Werror -fstack-protector-strong -D_FORTIFY_SOURCE=2 \
    -Wl,-z,relro,-z,now native/supervisor.c \
    -o "$native_output/$native_abi/libtailadb_exec.so" -landroid -llog
  "$native_compilers/${native_target}31-clang" \
    -O2 -Wall -Wextra -Werror -fstack-protector-strong -D_FORTIFY_SOURCE=2 \
    -shared -fPIC -Wl,-z,relro,-z,now native/policy.c \
    -o "$native_output/$native_abi/libtailadb_policy.so" -ldl -llog
  "$native_compilers/${native_target}31-clang" \
    -O2 -Wall -Wextra -Werror -fstack-protector-strong -D_FORTIFY_SOURCE=2 \
    -Wl,-z,relro,-z,now native/su_client.c \
    -o "$native_output/$native_abi/libtailadb_su.so"
done
