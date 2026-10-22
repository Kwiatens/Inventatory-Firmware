<!-- SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski -->
<!-- SPDX-License-Identifier: GPL-3.0-only -->

# Development harness

Two aids for work on a machine without PlatformIO. Both use stub ESP32 headers in `stubs/`, so they prove
less than a real build: a pass here is not a firmware build.

- **`syntax-check.sh <file>...`** type-checks hardware-only sources with `-Wall -Wextra` against stub
  FreeRTOS, Wi-Fi, OTA and Arduino headers. It covers `src/app` (except `inventatory_scan_app.cpp`), `keypad`,
  `gm65`, `power`, `display` and `storage`. It does not cover `ble_provisioning.cpp`, `device_log.cpp` or
  `main.cpp`, which need headers the stubs do not provide. The host tests build `inventatory_client.cpp`.

  ```bash
  tools/dev-harness/syntax-check.sh src/app/inventatory_scan_app_input.cpp
  ```

- **`lcd/build.sh [out.png]`** compiles the real `display_controller.cpp` and `display_renderer.cpp` against a
  stub U8g2, draws every screen from `lcd/driver.cpp` and writes a PNG contact sheet. The fonts are
  approximations, so trust positions, spacing and layout, not exact glyph widths. It needs Pillow. Running it
  before and after a refactor and comparing `lcd/ops.txt` shows whether any drawing operation changed.
