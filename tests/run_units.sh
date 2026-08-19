#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
out="${1:-$root/tests/addon_units}"
g++ -std=c++20 -pthread -Wall -Wextra \
  -I "$root/src" -I "$root/tests/third_party" \
  "$root/tests/addon_units.cpp" "$root/src/service/MapInventory.cpp" \
  -o "$out"
"$out"
