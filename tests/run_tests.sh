#!/bin/sh
# Builds and runs the native (PC) unit tests for the hardware-free components.
set -e
cd "$(dirname "$0")/.."
OUT="${TMPDIR:-/tmp}/companion_test"
g++ -std=c++17 -Wall -Wextra -Werror -Icomponents/face -Icomponents/companion \
    tests/companion_test.cpp components/companion/Companion.cpp components/face/Face.cpp -o "$OUT"
"$OUT"
