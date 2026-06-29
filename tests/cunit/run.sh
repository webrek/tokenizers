#!/usr/bin/env bash
set -euo pipefail
name="$1"; shift
root="$(cd "$(dirname "$0")/../.." && pwd)"
pcre2_cflags="$(pkg-config --cflags libpcre2-8 2>/dev/null || echo)"
pcre2_libs="$(pkg-config --libs libpcre2-8 2>/dev/null || echo -lpcre2-8)"
clang -std=c11 -g -O1 -Wall -Wextra -I"$root/src" $pcre2_cflags \
  "$root/tests/cunit/test_${name}.c" "$@" $pcre2_libs -o "/tmp/tk_test_${name}"
"/tmp/tk_test_${name}"
