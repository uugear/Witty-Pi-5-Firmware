#!/usr/bin/env bash
# DST host-side regression tests for Witty Pi 5 firmware.
# Usage (from repository root): bash tests/run_dst_tests.sh
# Requirements: a C11 compiler (CC defaults to cc), a Unix shell, and linker
# dead-code elimination. No Raspberry Pi, Pico SDK, or Witty Pi hardware needed.
# Tests: DST calendar/rules; transition/recovery/alarm integration; .skd scan.
# Binaries are generated in tests/build/ (ignored by the repository .gitignore).
set -euo pipefail
cd "$(dirname "$0")/.."
: "${CC:=cc}"
mkdir -p tests/build

# The script.c test links only the scanner; strip unrelated platform functions.
# GNU ld and Darwin ld use different flags for dead-code elimination.
case "$(uname -s)" in
    Darwin) link_gc=(-Wl,-dead_strip) ;;
    *)      link_gc=(-Wl,--gc-sections) ;;
esac

"$CC" -std=c11 -Wall -Wextra -Werror -Isrc \
    src/dst_time.c tests/test_dst_time.c -o tests/build/test_dst_time
./tests/build/test_dst_time

"$CC" -std=c11 -Wall -Wextra -Werror -DWP5_PRODUCT=1 \
    -Itests/mock -Isrc src/dst_time.c src/dst.c \
    tests/test_dst_integration.c -o tests/build/test_dst_integration
./tests/build/test_dst_integration

# Existing script.c has platform-specific formatting and unrelated warnings.
"$CC" -std=c11 -w -DWP5_PRODUCT=1 -ffunction-sections -fdata-sections \
    -Itests/mock -Isrc src/script.c src/dst_time.c tests/test_dst_script.c \
    "${link_gc[@]}" -o tests/build/test_dst_script
./tests/build/test_dst_script
