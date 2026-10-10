# Inventatory Scan R1 firmware

This repository contains the public firmware for the Inventatory Scan R1
scanner. It targets the Tenstar ESP32-S3-Zero board and uses PlatformIO with
the Arduino framework.

## Clean-checkout setup

Install PlatformIO Core 6.1.19 (the CI workflow uses this exact version), then
create the private build configuration from the tracked, non-secret example:

```powershell
Copy-Item src/config/config.example.h src/config/config.h
```

Edit `src/config/config.h` for the board being built. This file is ignored by
Git and must never be committed. The example contains no usable credentials;
replace its placeholder `OTA_PASSWORD` before enabling OTA. Wi-Fi credentials
and the Inventatory device token are provisioned at runtime over the paired
setup flow and are not stored in this repository.

The first build downloads the pinned PlatformIO packages. A normal USB build
is:

```powershell
pio run -e tenstar_esp32s3_zero_usb
```

Run the available PlatformIO tests on a connected board with:

```powershell
pio test -e tenstar_esp32s3_zero_usb
```

The test suite is embedded Unity firmware and therefore requires an ESP32-S3
board and serial test port. It does not require Wi-Fi, Inventatory credentials,
or OTA credentials. A build-only check for environments without hardware is:

```powershell
pio test -e tenstar_esp32s3_zero_usb --without-uploading --without-testing
```

## USB and OTA workflows

USB upload remains available through the explicit USB environment. Replace
`COM7` with the port assigned by the operating system:

```powershell
pio run -e tenstar_esp32s3_zero_usb -t upload --upload-port COM7
pio device monitor -e tenstar_esp32s3_zero_usb --baud 115200
```

OTA uses the separate `tenstar_esp32s3_zero_ota` environment. The upload host
is supplied at invocation time, while `tools/ota_auth.py` reads the OTA
password only from the ignored `src/config/config.h`:

```powershell
pio run -e tenstar_esp32s3_zero_ota -t upload --upload-port <device-hostname-or-ip>
```

The OTA hostname must match `DEVICE_ID`/`ArduinoOTA.setHostname()` (for
example, the device's `.local` name). Keep the password outside source
control, CI logs, release artifacts, and command history. CI never performs an
OTA upload.

## Hardware notes

The custom board definition describes a Tenstar ESP32-S3-Zero with 4 MB flash,
no PSRAM, 240 MHz CPU, Wi-Fi, Bluetooth, and native USB CDC enabled at boot.
The project uses the tracked `boards/tenstar_esp32s3_zero.json` definition and
`partitions.csv`; do not substitute a board with a different flash layout.

The production wiring is:

| Function | GPIO |
| --- | ---: |
| LCD SCL / clock | 9 |
| LCD RS / data-command | 10 |
| LCD RSE / reset | 11 |
| LCD CS | 12 |
| LCD SI / data | 13 |
| ESP32 TX to GM65 RX (`GM65_TX_PIN`) | 43 |
| ESP32 RX from GM65 TX (`GM65_RX_PIN`) | 44 |
| Keypad columns | 0, 1, 7, 6 |
| Keypad rows | 4, 5, 3, 2 |

The GMG12864-06D display is a 3.3 V peripheral and must share ground with the
ESP32-S3. Its standalone SPI smoke test is in
`examples/gmg12864_spi_test`; that example is independent of the production
firmware and uses the same pinned board/platform and U8g2 version.

## Pinned dependencies and licenses

The root `platformio.ini` pins the PlatformIO platform and every direct library
dependency. Do not change these to floating ranges without a reviewed update:

| Package | Version | License / source |
| --- | ---: | --- |
| `espressif32` platform | 7.0.1 | [Apache-2.0](https://github.com/platformio/platform-espressif32/blob/develop/LICENSE) |
| `chris--a/Keypad` | 3.1.1 | [MIT](https://github.com/Chris--A/Keypad/blob/master/LICENSE) |
| `olikraus/U8g2` | 2.35.30 | [BSD-2-Clause](https://github.com/olikraus/u8g2/blob/master/LICENSE) |
| `throwtheswitch/Unity` | 2.6.1 | [MIT](https://github.com/ThrowTheSwitch/Unity/blob/master/LICENSE.txt) |

Inventatory firmware code is distributed under GPL-3.0-only; see the tracked
root `LICENSE`. Upstream dependency notices and license terms remain the
responsibility of any binary or source distribution that includes those
dependencies.

Report suspected vulnerabilities privately as described in `SECURITY.md`.

## Scan R1 controls

The scanner stays ready to read labels while ordinary menus are open. A scan
immediately returns to the inventory workflow; scanning is paused only during
pairing, standby, a destructive confirmation, an OTA update, or active label
printing.

- `C` opens the menu from Ready and acts as Back, Cancel, or Dismiss elsewhere.
- `A` confirms, adds inventory, or lowers display contrast.
- `B` removes inventory or raises display contrast.
- `D` moves the quantity cursor or opens the next Quick Labels page.
- A short `#` press deletes the selected quantity digit; holding `#` on the
  idle screen opens the re-pair confirmation, where `A` confirms.
- Number keys enter quantities or select numbered menu and Quick Label entries.

Quick Labels refresh automatically when opened and print immediately after a
preset is selected. Offline inventory scans remain available and are identified
on the LCD as safely queued work.
