#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
# SPDX-License-Identifier: GPL-3.0-only
# Syntax-checks hardware-only sources (src/app, keypad, gm65, ...) against stub ESP32 headers.
# Usage: docs/handoff/dev-harness/syntax-check.sh src/app/inventatory_scan_app_input.cpp [more files]
# Proves the code parses and type-checks. It does NOT prove it links or runs on the ESP32.
set -u
here="$(cd "$(dirname "$0")" && pwd)"; fw="$(cd "$here/../../.." && pwd)"
status=0
for f in "$@"; do
  g++ -std=gnu++17 -fsyntax-only -Wall -Wextra -Wno-unused-parameter -include "$here/stubs/extra.h" \
    -I"$here/stubs" -I"$here/stubs/config" -I"$fw/host-tests/shim" -I"$fw/host-tests/shim/config" \
    -I"$fw/include" -I"$fw/src" "$fw/$f" || status=1
done
exit $status
