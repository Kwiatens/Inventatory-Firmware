#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
# SPDX-License-Identifier: GPL-3.0-only
# Renders every screen the DisplayController can show to a PNG contact sheet (default: sheet.png here).
# Fonts are APPROXIMATE (Liberation / DejaVu stand-ins for the U8g2 bitmap fonts): trust the layout,
# not exact glyph widths. Needs g++ and python3 with Pillow.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"; fw="$(cd "$here/../../../.." && pwd)"; stubs="$here/../stubs"
g++ -std=gnu++17 -O0 -w -include "$stubs/extra.h" -I"$stubs" -I"$stubs/config" -I"$fw/host-tests/shim" \
  -I"$fw/host-tests/shim/config" -I"$fw/include" -I"$fw/src" \
  "$here/driver.cpp" "$here/fonts.cpp" "$fw/src/display/display_controller.cpp" "$fw/src/display/display_renderer.cpp" \
  -o "$here/lcd_driver"
"$here/lcd_driver" > "$here/ops.txt"
python3 "$here/render.py" "$here/ops.txt" "${1:-$here/sheet.png}" 3
echo "wrote ${1:-$here/sheet.png}"
