<div align="center">

<a href="https://kwiatens.github.io/Inventatory-Site/">
  <img src="branding/inventatory-lockup.svg" alt="Inventatory" width="560">
</a>

<sub>Scanner Firmware</sub>

[![Firmware CI](https://github.com/Kwiatens/Inventatory-Firmware/actions/workflows/firmware.yml/badge.svg)](https://github.com/Kwiatens/Inventatory-Firmware/actions/workflows/firmware.yml)
[![License: GPL-3.0-only](https://img.shields.io/badge/license-GPL--3.0--only-blue.svg)](LICENSE)

[**Website**](https://kwiatens.github.io/Inventatory-Site/) &nbsp;·&nbsp;
[Report an issue](https://github.com/Kwiatens/Inventatory-Firmware/issues)

</div>

---

This repository contains the public firmware for the Inventatory Scan R1
scanner, a handheld barcode scanner that records stock changes and syncs them
to the [Inventatory](https://github.com/Kwiatens/Inventatory-Software) desktop
application. It targets the Waveshare ESP32-S3-Nano board and uses PlatformIO
with the Arduino framework.

What it does: reads 1D/2D labels through a GM65 scanner, lets the operator
enter a quantity on a 4x4 keypad and a 128x64 LCD, keeps every scan in a
durable offline queue, pairs with the desktop over BLE, and syncs over Wi-Fi
with a mutually authenticated protocol (HMAC-SHA-256 with directional keys and
replay counters).

> **Status:** pre-release (`0.1.0`, see [CHANGELOG.md](CHANGELOG.md)). The code
> builds in CI, but the on-hardware release gates in
> [docs/release-audit-remediation.md](docs/release-audit-remediation.md) are
> still open. Do not rely on it for production use before they are closed.

## Documentation

- [CONTRIBUTING.md](CONTRIBUTING.md): how to contribute.
- [SECURITY.md](SECURITY.md): private vulnerability reporting and the security scope.
- [AGENTS.md](AGENTS.md): architecture, invariants and verification rules, also the guide for coding agents.
- [docs/project-standards.md](https://github.com/Kwiatens/Inventatory-Software/blob/main/docs/project-standards.md): rules shared with the Inventatory application.
- [docs/releasing.md](docs/releasing.md): release steps and repository settings.
- [CHANGELOG.md](CHANGELOG.md): what changed in each version.
- [docs/scanner-transport-security.md](https://github.com/Kwiatens/Inventatory-Software/blob/main/docs/scanner-transport-security.md): the transport protocol, kept in the application repository.
- [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md): licenses of the dependencies.

## Clean-checkout setup

Install PlatformIO Core 6.1.19 (the CI workflow uses this exact version), then
create the private build configuration from the tracked, non-secret example:

```bash
cp src/config/config.example.h src/config/config.h                  # Linux / macOS
```

```powershell
Copy-Item src/config/config.example.h src/config/config.h           # Windows
```

Edit `src/config/config.h` for the board being built. This file is ignored by
Git and must never be committed. The example contains no usable credentials;
replace its placeholder `OTA_PASSWORD` before enabling OTA. Wi-Fi credentials
and the Inventatory device token are provisioned at runtime over the paired
setup flow and are not stored in this repository.

The first build downloads the pinned PlatformIO packages. A normal USB build
is:

```powershell
pio run -e waveshare_esp32s3_nano_usb
```

Run the available PlatformIO tests on a connected board with:

```powershell
pio test -e waveshare_esp32s3_nano_usb
```

The test suite is embedded Unity firmware and therefore requires an ESP32-S3
board and serial test port. It does not require Wi-Fi, Inventatory credentials,
or OTA credentials. A build-only check for environments without hardware is:

```powershell
pio test -e waveshare_esp32s3_nano_usb --without-uploading --without-testing
```

## USB and OTA workflows

USB upload remains available through the explicit USB environment. Replace
`COM7` with the port assigned by the operating system:

```powershell
pio run -e waveshare_esp32s3_nano_usb -t upload --upload-port COM7
pio device monitor -e waveshare_esp32s3_nano_usb --baud 115200
```

OTA uses the separate `waveshare_esp32s3_nano_ota` environment. The upload host
is supplied at invocation time, while `tools/ota_auth.py` reads the OTA
password only from the ignored `src/config/config.h`:

```powershell
pio run -e waveshare_esp32s3_nano_ota -t upload --upload-port <device-hostname-or-ip>
```

The OTA hostname must match `DEVICE_ID`/`ArduinoOTA.setHostname()` (for
example, the device's `.local` name). Keep the password outside source
control, CI logs, release artifacts, and command history. CI never performs an
OTA upload. A new OTA image is confirmed after 30 seconds of healthy runtime;
if the device crashes or loses power before that, it returns to the previous
image, so leave the scanner powered for half a minute after an update.

## Hardware notes

The board is a Waveshare ESP32-S3-Nano (ESP32-S3R8): 240 MHz CPU, Wi-Fi,
Bluetooth, native USB CDC enabled at boot, 16 MB flash and 8 MB PSRAM. The
tracked `boards/waveshare_esp32s3_nano.json` definition keeps the 4 MB
`partitions.csv` layout, and the build does not enable PSRAM. The Nano is a
3.3 V board. It has no LiPo charger, and its VIN accepts 6-21 V, so the LiPo
needs its own charger and boost stage.

The production wiring uses the Nano's header labels, shown in the table below.
Every keypad line must be an RTC GPIO (0-21) so the `*` key can wake the chip.
GPIO0, GPIO3, GPIO45 and GPIO46 are boot straps or the RGB LED, and GPIO48
drives the board's SCK LED, so none of them are used.

The production wiring is:

| Function | Nano label | GPIO |
| --- | --- | ---: |
| LCD SCL / clock | A6 | 13 |
| LCD RS / data-command | A4 | 11 |
| LCD RSE / reset | A5 | 12 |
| LCD CS | D10 | 21 |
| LCD SI / data | A7 | 14 |
| ESP32 TX to GM65 RX (`GM65_TX_PIN`) | D1 (TXD) | 43 |
| ESP32 RX from GM65 TX (`GM65_RX_PIN`) | D0 (RXD) | 44 |
| Keypad columns 1-4 (column 1 wakes on `*`) | D2, D3, D4, D5 | 5, 6, 7, 8 |
| Keypad rows 1-4 (row 4 holds `*` low in sleep) | D6, D7, D8, D9 | 9, 10, 17, 18 |
| LiPo sense (divider midpoint, ADC1) | A0 | 1 |

The LiPo divider runs from BAT+ through 100 kOhm to GPIO1, then through another
100 kOhm to GND, with 100 nF from GPIO1 to GND. The values live in
`config.example.h`, and equal resistors put 4.2 V on the pin as 2.1 V.

Deep sleep is not a power cut. The GM65 scanner stays on its supply, the 3.3 V
power LED (R8, about 3 mA) and the reset pull-up (R3, about 0.65 mA) keep
drawing current, and the buck converter stays on. Cutting the power LED and
switching the scanner's supply are hardware changes outside this firmware.

Check the GM65 TX logic level before wiring it to GPIO44. Listings give a 5 V
supply for most GM65 modules; a 5 V TX line needs a level shifter.

The GMG12864-06D display is a 3.3 V peripheral and must share ground with the
ESP32-S3. Its standalone SPI smoke test is in
`examples/gmg12864_spi_test`; that example is independent of the production
firmware and uses the same pinned board/platform and U8g2 version.

## Pinned dependencies

The root `platformio.ini` pins the PlatformIO platform and every direct library
dependency. Do not change these to floating ranges without a reviewed update:

| Package | Version | License / source |
| --- | ---: | --- |
| `espressif32` platform | 7.0.1 | [Apache-2.0](https://github.com/platformio/platform-espressif32/blob/develop/LICENSE) |
| `olikraus/U8g2` | 2.35.30 | [BSD-2-Clause](https://github.com/olikraus/u8g2/blob/master/LICENSE) |
| `throwtheswitch/Unity` | 2.6.1 | [MIT](https://github.com/ThrowTheSwitch/Unity/blob/master/LICENSE.txt) |


## Scan R1 controls

The scanner stays ready to read labels while ordinary menus are open. A scan
immediately returns to the inventory workflow; scanning is paused only during
pairing, standby, a destructive confirmation, an OTA update, or active label
printing.

- `C` opens the menu from Ready and acts as Back, Cancel, or Dismiss elsewhere.
- `A` confirms, adds inventory, or lowers display contrast.
- `B` removes inventory or raises display contrast.
- `D` moves the quantity cursor or opens the next Quick Labels page.
- Hold `*` on the Ready screen (about 1.8 s) to power off into deep sleep. Release
  it, then press `*` to wake the device; the firmware restarts.
- A short `#` press deletes the selected quantity digit; holding `#` on the
  idle screen opens the re-pair confirmation, where `A` confirms.
- Number keys enter quantities or select numbered menu and Quick Label entries.

Quick Labels refresh automatically when opened and print immediately after a
preset is selected. Offline inventory scans remain available and are identified
on the LCD as safely queued work.

## License

Inventatory firmware is licensed under GPL-3.0-only; see [LICENSE](LICENSE).
Dependency licenses, including the LGPL components, are recorded in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). Upstream notices and license
terms remain the responsibility of any binary or source distribution that
includes those dependencies.
