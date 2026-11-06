#!/bin/sh
# Builds and runs the native (PC) unit tests for the hardware-free components.
set -e
cd "$(dirname "$0")/.."
OUT="${TMPDIR:-/tmp}/companion_test"
g++ -std=c++17 -Wall -Wextra -Werror -Icomponents/face -Icomponents/companion -Icomponents/sfx \
    tests/companion_test.cpp components/companion/Companion.cpp components/companion/Presence.cpp components/face/Face.cpp \
    components/sfx/Sfx.cpp -o "$OUT"
"$OUT"

g++ -std=c++17 -Wall -Wextra -Werror -Icomponents/face -Icomponents/companion -Icomponents/sfx -Icomponents/config \
    tests/config_test.cpp components/config/BuddyConfig.cpp components/companion/Companion.cpp \
    components/companion/Presence.cpp components/face/Face.cpp components/sfx/Sfx.cpp -o "$OUT.config"
"$OUT.config" "$@"

g++ -std=c++17 -Wall -Wextra -Werror -Icomponents/assist \
    tests/assist_test.cpp components/assist/AssistText.cpp components/assist/Wav.cpp -o "$OUT.assist"
"$OUT.assist"
