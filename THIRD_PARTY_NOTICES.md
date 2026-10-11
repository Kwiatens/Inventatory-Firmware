# Third-party notices

This file lists the third-party software used by the Inventatory Scan R1
firmware, the license terms found in the files installed for this build, and
whether each item is linked into the shipped firmware binary. The firmware's own
code is licensed under GPL-3.0-only (see [LICENSE](LICENSE)).

This is a factual record of license information, not legal advice.

Versions are the pins in [platformio.ini](platformio.ini):
`espressif32@7.0.1`, `olikraus/U8g2@2.35.30`, and `throwtheswitch/Unity@2.6.1`.
The 4x4 keypad matrix is scanned by first-party code (`src/keypad/keypad.cpp`);
no keypad library is used. The Arduino framework is the package the
platform resolves to (`framework-arduinoespressif32` 3.20017.241212, which
is Arduino-ESP32 2.0.17 on ESP-IDF v4.4.7 per `tools/sdk/versions.txt`).

## Summary

| Component | Version | License (SPDX, as found in files) | In shipped firmware | Upstream |
| --- | --- | --- | --- | --- |
| Arduino-ESP32 core and libraries | 3.20017.241212 (Arduino-ESP32 2.0.17) | LGPL-2.1-or-later (core); per-library headers vary | Yes | https://github.com/espressif/arduino-esp32 |
| ESP-IDF v4.4.7 prebuilt SDK libraries | ESP-IDF v4.4.7 | Mixed, per header (see notes) | Yes (Wi-Fi, BLE, TLS, RTOS archives) | https://github.com/espressif/esp-idf |
| U8g2 (library code) | 2.35.30 | BSD-2-Clause | Yes | https://github.com/olikraus/u8g2 |
| U8g2 fonts used by the firmware | bundled with U8g2 | X11-style terms for the Helvetica fonts; ProFont terms not verified | Yes | https://github.com/olikraus/u8g2 |
| Unity | 2.6.1 | MIT | No (test builds only; source vendored in `host-tests/vendor/unity/`) | https://github.com/ThrowTheSwitch/Unity |
| espressif32 platform | 7.0.1 | Apache-2.0 | No (build tooling) | https://github.com/platformio/platform-espressif32 |
| Xtensa GCC toolchain (libc, libgcc, libstdc++ runtime) | 8.4.0+2021r2-patch5 | GPL and LGPL components, with GCC Runtime Library Exception | Runtime libraries only | https://github.com/espressif/crosstool-NG |
| esptool | 2.41100.260830 | GPL-2.0-or-later | No (upload tool) | https://github.com/espressif/esptool |

## Notes per dependency

### Arduino-ESP32 core and libraries (LGPL-2.1-or-later)

- Core: `cores/esp32/Arduino.h` states "either version 2.1 of the License, or
  (at your option) any later version"; the package's `package.json` declares
  `"license": "LGPL-2.1-or-later"`.
- The installed framework package has no top-level LICENSE file. License
  headers vary by library: `libraries/WiFi` carries LGPL headers;
  `libraries/Preferences` and `libraries/Update` carry Apache-2.0 headers
  (`Update` uses an SPDX tag); `libraries/ESPmDNS` carries an MIT notice;
  `libraries/BLE/LICENSE` is the Apache-2.0 text; `libraries/ArduinoOTA` has no
  per-file license header.
- Linked: `Arduino.h`, `WiFi.h`, `ESPmDNS.h`, `Preferences.h`, `ArduinoOTA.h`,
  `Update`, and the BLE headers used by `src/setup/ble_provisioning.cpp`.
- LGPL-2.1 section 6 (text at `share/licenses/gcc/COPYING.LIB` in the
  toolchain package) applies to distributed binaries that link the library: the
  work must carry a prominent notice that the library is used and that it is
  covered by the LGPL, a copy of the license must be supplied, and one of the
  options in section 6(a) to (e) must be provided for the library.

### ESP-IDF v4.4.7 prebuilt SDK libraries

- `tools/sdk/esp32s3/lib/*.a` are precompiled archives. They have no license
  files on disk. The SPDX tags in `tools/sdk/esp32s3/include` are:
  Apache-2.0 (most headers), `Apache-2.0 OR GPL-2.0-or-later` (96 mbedTLS
  headers), BSD-2-Clause (libcoap), BSD-3-Clause (argtable3 and one lwIP
  port header), and ISC (two headers).
- Linked: the example build's link map
  (`examples/gmg12864_spi_test/.pio/build/waveshare_esp32s3_nano/firmware.map`)
  pulls in archives including `libbt`, `libbtdm_app`, `libwpa_supplicant`,
  `libnet80211`, `libesp_wifi`, `liblwip`, `libmbedcrypto`, `libmbedtls_2`,
  `libfreertos`, and `libmesh`. The main firmware uses Wi-Fi, BLE, and mbedTLS
  (`include/InventatoryScanSecurity.h` includes `mbedtls/md.h`), so the same
  families are expected in the main binary. The main firmware map was not
  generated, so the exact set is not verified.
- The Wi-Fi, Bluetooth, and PHY binaries (for example `libbtdm_app`,
  `libnet80211`, `libphy`, `libpp`, `libwpa_supplicant`) are vendor-supplied
  binaries. Their license terms are not on disk and were not verified.

### U8g2 2.35.30 (BSD-2-Clause code, mixed font terms)

- `.pio/libdeps/waveshare_esp32s3_nano_usb/U8g2/LICENSE` and
  `library.properties` (`license=BSD-2-Clause`) state the library code is under
  the two-clause BSD license (Copyright (c) 2016, olikraus@gmail.com). Binary
  redistribution must reproduce the copyright notice, the conditions, and the
  disclaimer in the documentation or other materials provided.
- Fonts used by the firmware (`src/display/display_style.h`, lines 97 to 100):

  | Font symbol | Font source (from the font header comment in `u8g2_fonts.c`) | Terms |
  | --- | --- | --- |
  | `u8g2_font_helvR08_tf` | Adobe Helvetica Medium R 11-80 (HELVR08.BDF) | X11-style Adobe / DEC notice, per the U8g2 `LICENSE` |
  | `u8g2_font_helvB08_tf` | Adobe Helvetica Bold 11-80 (HELVB08.BDF) | Same as above |
  | `u8g2_font_helvB10_tr` | Adobe Helvetica Bold 14-100 (HELVB10.BDF) | Same as above |
  | `u8g2_font_profont15_tf` | ProFont Distribution 2.2 (Fontographer 4.1.5) | Not verified; ProFont is not named in U8g2 `LICENSE` |

- The X11-style notice in U8g2 `LICENSE` requires the copyright and permission
  notices to appear in supporting documentation. It also limits the Adobe
  trademark use to "the images described in this file."
- `u8g2_font_6x12_tf` is used only by `examples/gmg12864_spi_test`, which is a
  separate build. Its header says "Public domain terminal emulator font." It is
  not in the main firmware.
- The `sys/raspi_gpio` example in U8g2 uses bcm2835 under GPL-3.0. It is not
  built here.
- The U8g2 `LICENSE` points to the font licensing wiki
  (`https://github.com/olikraus/u8g2/wiki/fntgrp`) for per-font terms. The wiki
  is not on disk, so per-font terms beyond the files above were not verified.

### Unity 2.6.1 (MIT, test only)

- `host-tests/vendor/unity/LICENSE.txt` is the MIT text that ships with the vendored
  `unity.c`, `unity.h` and `unity_internals.h` (Copyright (c) 2007-25 Mike Karlesky,
  Mark VanderVoord, & Greg Williams). The three sources are byte-identical to the
  2.6.1 release that PlatformIO installs for `pio test`.
- Used by `test/` through `pio test` and by the host build in `host-tests/`. Not in
  the shipped firmware.

### espressif32 platform 7.0.1 (Apache-2.0, build tooling)

- `~/.platformio/platforms/espressif32/LICENSE` is the Apache License 2.0 text.
- Build-time only. Not in the firmware binary.

### Xtensa GCC toolchain, newlib, and runtime libraries

- Package `toolchain-xtensa-esp32s3` 8.4.0+2021r2-patch5, licenses under
  `share/licenses/`: `gcc/COPYING.RUNTIME` (GCC Runtime Library Exception 3.1),
  `gcc/COPYING3`, `gcc/COPYING.LIB`, `binutils/COPYING3`, and
  `newlib/COPYING.NEWLIB` (newlib is a mix of sources).
- Linked runtime archives in the example link map include `libc.a`,
  `libnewlib.a`, `libgcc.a`, and `libstdc++.a`. Per-file headers in those
  archives were not checked.

### esptool 2.41100.260830 (GPL-2.0-or-later, upload tool)

- Package `tool-esptoolpy` has `"license": "GPL-2.0-or-later"`. Source files
  such as `espefuse.py` and `espsecure.py` carry the SPDX tag
  `GPL-2.0-or-later`. Its `LICENSE` is the GPL-2.0 text.
- Used only for flashing. Not in the firmware binary.

## Open items (not verified)

1. ProFont 2.2 terms for `u8g2_font_profont15_tf` are not on disk.
2. Terms for the vendor-supplied Wi-Fi, Bluetooth, and PHY binaries in
   `tools/sdk/esp32s3/lib` are not on disk.
3. The main firmware link map was not generated. The linked set is inferred
   from the example build's map and the sources' `#include` lines.
4. The framework package has no top-level LICENSE file. The LGPL-2.1 text was
   taken from the toolchain package's `share/licenses/gcc/COPYING.LIB`.
5. Upstream URLs come from package metadata (`library.properties`,
   `library.json`, `package.json`, `.piopm`). They were not fetched online.
