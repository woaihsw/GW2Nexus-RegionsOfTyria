#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
out="${1:-$root/tests/addon_units}"
g++ -std=c++20 -pthread -Wall -Wextra \
  -I "$root/src" -I "$root/tests/third_party" \
  "$root/tests/addon_units.cpp" "$root/src/service/MapInventory.cpp" \
  -o "$out"
"$out"

font="${ROT_TEST_CJK_FONT:-/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf}"
latin_font="${ROT_TEST_LATIN_FONT:-/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf}"
if [[ ! -f "$font" ]]; then
  echo "Install fonts-droid-fallback or set ROT_TEST_CJK_FONT to a CJK TTF/TTC." >&2
  exit 1
fi
font_test_dir="$(mktemp -d)"
trap 'rm -rf "$font_test_dir"' EXIT
python3 - "$root/src/maps/maps.zip" "$font_test_dir/seed.txt" <<'PY'
import sys, zipfile
from pathlib import Path
with zipfile.ZipFile(sys.argv[1]) as archive:
    Path(sys.argv[2]).write_bytes(archive.read('cjk_seed.txt'))
PY
font_flags=(-O2)
if [[ "${ROT_TEST_SANITIZERS:-0}" == 1 ]]; then
  font_flags=(-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer)
fi
g++ -std=c++20 "${font_flags[@]}" -pthread -Wall -Wextra \
  -I "$root/src" -I "$root/tests/third_party" \
  "$root/tests/map_font_units.cpp" "$root/src/MapGlyphAtlas.cpp" \
  "$root/src/service/MapInventory.cpp" "$root/src/service/MapFontService.cpp" \
  "$root/src/imgui/imgui.cpp" "$root/src/ImGuiFontBuild.cpp" \
  "$root/src/imgui/imgui_tables.cpp" "$root/src/imgui/imgui_widgets.cpp" \
  -o "$font_test_dir/map_font_units"
"$font_test_dir/map_font_units" "$font" "$font_test_dir/seed.txt" "$latin_font" "${ROT_TEST_CJK_SECONDARY_FONT:-$font}"
if [[ -n "${ROT_TEST_MS_FONT_DIR:-}" ]]; then
  "$font_test_dir/map_font_units" "$ROT_TEST_MS_FONT_DIR/msyh.ttc" "$font_test_dir/seed.txt" "$latin_font" "$ROT_TEST_MS_FONT_DIR/simsun.ttc"
fi
