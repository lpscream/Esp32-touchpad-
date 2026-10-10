#!/bin/sh
# Тесты на ПК: движок жестов + драйвер/скетч с эмулятором чипа. Нужен g++ (C++11).
set -e
cd "$(dirname "$0")"
OUT="${TMPDIR:-/tmp}/mxt_tests"
mkdir -p "$OUT"
g++ -std=c++11 -Wall -Wextra -O1 -o "$OUT/gestures" test_gestures.cpp
g++ -std=c++11 -Wall -Wextra -O1 -Istubs -x c++ -o "$OUT/system" test_system.cpp
"$OUT/gestures"
"$OUT/system"
