# Handoff: what is left before the open-source release

Written at the end of a long review session so another agent (or person) can continue without the
conversation. Read this, then `AGENTS.md` (the repository's rules, which still apply), then pick a task
from section 4. Delete this directory once the work is done.

**The firmware is not release-ready.** `AGENTS.md` section 11 and `docs/release-audit-remediation.md`
list hardware gates that nobody has run. Nothing below changes that. Never write "release-ready" or
"verified" for something that was only compiled or run on the host (`AGENTS.md` section 10, "Verification
hierarchy").

## 1. State of the code

All of this session's work is on `main`. Verified with: `make -C host-tests SANITIZE=address,undefined test`
(98 shared + 75 host-only tests pass), `python3 tools/check_error_codes.py`, the zero-logging `git grep`
rules, and the stub syntax check in section 5. **No ESP32 build was ever run** (no PlatformIO in the
session), **nothing was flashed**, and nothing ran on a board.

What changed, in rough order (see `git log` for the commits):

- **LCD:** the power-off warning is a centred title, a countdown bar and "Release * to cancel". The
  power-off ending is the `›i` sinking behind a short rule with "Press * to wake". The quantity screen's
  item and code lines no longer touch, and "Change quantity by:" became "Change by:" so a four-digit
  value fits. Layout numbers are named constants in `display_style.h`; centred lines share `drawCentered`.
- **Logo dot removed everywhere:** the mark is the chevron and stem only. The update screen shows words,
  the percentage and a progress bar. `DisplayBrandDot`, the dot animation helpers and their tests are gone.
- **Battery time left:** an estimate shows from the first reading, marked `~` until a rate is learned
  (typical rates `kBatteryPriorDischargeMsPerPercent` / `kBatteryPriorChargeMsPerPercent`, **guesses**).
- **Charger LEDs** (from the owner's branch `wip/battery-and-charge-leds`, merged): red LED = charging,
  green = done, replacing the single dock-sense pin. The done LED is now debounced in `BatteryTracker`.
  The new low-battery warning layout (bitmap triangle) came with it.
- **Bug fix:** a sync result could replace a settings or confirm screen while the key handler still
  acted on the old screen (`processNetworkResult`). Only Ready, quantity and waiting states accept one now.
- **Refactor:** `handleKey` is split into `handleDigit`, `handleCancel`, `handleAddSubtract` and the
  confirm handlers; `localFailure()` builds local failure results; setting labels are lookup tables;
  the GM65 `begin()`/`resume()` share `start()`; the settings store shares `saveCached()`.
- **Dead code removed:** `buildQuantityRequestJson`, `buildScanRequestJson`, `QuantityRequest`,
  `chargerPresent()`.
- **Behaviour change to know about:** digits no longer leak into quantity entry from the re-pair,
  confirm and printing screens. The backlight picker says "Off after N min idle" (it never dimmed).
- **Docs added:** `docs/board-day-checklist.md` (the ordered hardware procedure and evidence log) and this
  folder.

## 2. Hard rules (from `AGENTS.md`, summarised)

- Never flash, OTA, erase NVS or reset pairing without the owner asking for it.
- No display or keypad calls from Core 0; no blocking network calls from Core 1 (OTA excepted).
- Never log barcode text, Wi-Fi credentials, tokens or raw scanner bytes.
- Commit format: `type(scope): lowercase imperative, no full stop, 72 characters or fewer`. Types and
  scopes are in `AGENTS.md` section 12. New source files carry the SPDX header. Update `CHANGELOG.md`
  for user-visible changes. After any error-code change run `python3 tools/check_error_codes.py`.
- Do not open a PR or push to a branch the owner did not name.

## 3. Open questions that need the owner or a board

| # | Question | Where | What decides it |
| --- | --- | --- | --- |
| 1 | The scanner boot profile writes `0x00` to the Code128 register (`0x0033`) but the comment says "on". Is Code128 meant to be off? | `src/gm65/gm65_scanner.cpp` `kBootProfile` | Scan a Code128 label on a real GM65 and read the GM65 manual. Fix the value or the comment. |
| 2 | Are the charger LED pads active-high? The code reads "lit" as a high level. | `hardware::kChargeLedOnLevel`, `docs/wiring.md` | Measure the pads with the LED lit and off. Open-drain outputs would need level 0 and a pull-up. |
| 3 | The `~` battery estimate rates are guesses (1000 mAh, 80 mA in use, 500 mA charging). | `include/InventatoryScanBattery.h` | A discharge and a charge run on the real cell. |
| 4 | Print failure messages are the only ALL-CAPS text on the LCD: `PC OR PRINTER OFFLINE`, `REFRESH PRESETS FIRST`, `PC OFFLINE`, `PRINT RESULT UNKNOWN`. | `src/app/inventatory_scan_app_network.cpp`, `..._ui.cpp`, `docs/screens/README.md` | Owner preference. Changing them means re-rendering `docs/screens/PRT-0*.png` with the real display library. |
| 5 | The app is at about 91% of the 1.56 MB OTA slot on a 16 MB chip. | `partitions.csv` | Owner decision. Larger slots need a USB reflash (OTA cannot resize partitions). |
| 6 | Scan data (codes, quantities, not secrets) goes to the PC over plaintext HTTP to whichever host answers mDNS. Responses are authenticated, requests are not encrypted. | `src/net/inventatory_client.cpp` | Decide whether to document it in `SECURITY.md` as out of scope. |

## 4. Remaining work, in priority order

### 4.1 Run the real build (first, 30 minutes, needs PlatformIO)

Everything here has only been syntax-checked against stubs. Run `pio run -e waveshare_esp32s3_nano_usb`,
`pio run -e waveshare_esp32s3_nano_ota` (set a throwaway `OTA_PASSWORD`) and
`pio test -e waveshare_esp32s3_nano_usb --without-uploading --without-testing`. Fix whatever breaks. Note
the RAM and flash figures and update `docs/release-audit-remediation.md` if they changed.

### 4.2 Review the code that was never read

The session read the app, display, keypad, scanner, client, BLE, storage and the Core, Battery and most of
the UiPolicy headers. It did **not** read these. They are the most security-sensitive code, and the test
suite passing is not a review:

- `include/InventatoryScanSecurity.h` (HMAC, key derivation, constant-time compare)
- `include/InventatoryScanSync.h` (788 lines: the JSON request builder and the strict response parser)
- `include/InventatoryScanOutbox.h`, `InventatoryScanGm65Frame.h`, `InventatoryScanBleRequest.h`,
  `InventatoryScanHttp.h`, `InventatoryScanErrorCodes.h`, `InventatoryScanBlockedHead.h`
- The tail of `InventatoryScanUiPolicy.h` (energy-saving state machine, power-off timing)
- `src/display/backlight.cpp` (only syntax-checked)
- All of `test/` and `host-tests/tests/` (read only the parts that were edited), `tools/*.py`, `examples/`
- `README.md`, `CONTRIBUTING.md`, `SECURITY.md`, `THIRD_PARTY_NOTICES.md`
- The git history (the release checklist asks for a secret scan of it)

For each file: look for correctness bugs, bounds and integer problems, dead code, and drift from
`AGENTS.md` and the shared protocol spec
(`Inventatory-Software/docs/scanner-transport-security.md`). Add a failing test first for any bug.

### 4.3 Hardware release gates

Follow `docs/board-day-checklist.md` start to finish and fill in its evidence log. Then update the
ledger in `docs/release-audit-remediation.md` ("hardware pending" rows become recorded results) and close
the five gates in its "Release gate" section.

### 4.4 Regenerate generated assets (needs the real display library)

The session could not render with the real U8g2 fonts. These are stale or may be:

- `docs/screens/*.png`: only `SCN-01-boot.png` was hand-edited (the old broken dot was blanked). If
  open question 4 changes any text, re-render the `PRT-*` images too.
- The website's `Inventatory-Site/src/data/scan-r1-lcd.json`: regenerate with
  `Inventatory-Site/scripts/scan-r1-lcd/build.sh <path to this repo>` after a `pio run`. The quantity
  screen spacing and prompt changed, so those frames are out of date.

### 4.5 Smaller improvements, none urgent

- Test `handleKey` and its handlers. They are the largest piece of logic with no tests because the app
  class needs FreeRTOS and Arduino. Extracting a pure "key to action" function into `include/` would make
  it testable on the host.
- Persist the learned battery rate in NVS (`inv_device`) so the `~` estimate survives a reboot.
- Add `-Wall -Wextra` to `build_flags` in `platformio.ini` and fix what it reports.
- `BleProvisioning::begin()` allocates callback objects and a `BLESecurity` with `new` and never frees
  them (a few hundred bytes leaked per pairing restart).
- `DisplayController::showSplash()` is public but only used inside the class. Make it private.
- Possible CI step: run `docs/handoff/dev-harness/syntax-check.sh` so hardware-only files are type-checked
  without PlatformIO. Only worth it if the stubs are kept up to date.

### 4.6 Release steps (after 4.1 to 4.4)

Follow `docs/releasing.md`: version bump in `src/config/version.h`, dated changelog, secret scan of the
whole history, tag `vX.Y.Z`, build `firmware.bin` from the tagged commit with a placeholder-free private
`config.h`, attach it with its SHA-256, and set the GitHub repository settings listed there (private
vulnerability reporting, branch protection, secret scanning, read-only workflow token).

## 5. Dev aids in this folder (optional, delete if you do not want them)

`dev-harness/` exists because the session had no PlatformIO. Both tools use stub headers, so they prove
less than a real build.

- **`dev-harness/syntax-check.sh <file>...`**: type-checks hardware-only sources (everything in `src/app`,
  `keypad`, `gm65`, `power`, `storage/device_settings_store.cpp`) with `-Wall -Wextra` against stub
  FreeRTOS, WiFi, OTA and Arduino headers. It does not cover `ble_provisioning.cpp` or
  `inventatory_client.cpp` (host tests build the client). Example:
  `docs/handoff/dev-harness/syntax-check.sh src/app/inventatory_scan_app_input.cpp`.
- **`dev-harness/lcd/build.sh [out.png]`**: compiles the real `display_controller.cpp` and
  `display_renderer.cpp` against a stub U8g2, drives every screen from `lcd/driver.cpp` and writes a PNG
  contact sheet. **The fonts are approximations**, so text widths are off by a few pixels and wide text may
  look clipped when it is not. Trust positions, spacing and layout, not exact glyph fit. Needs Pillow.
  Handy trick: run it before and after a refactor and diff `lcd/ops.txt`. Identical drawing operations
  mean no pixel changed.

## 6. Where things are

| Need | Look at |
| --- | --- |
| Rules, pinout, invariants, key map | `AGENTS.md` |
| What is verified and what is pending | `docs/release-audit-remediation.md` |
| Hardware test procedure | `docs/board-day-checklist.md` |
| How tests are organised | `docs/testing.md` |
| Release steps | `docs/releasing.md` |
| Wiring notes (GM65 cable colours, charger LEDs) | `docs/wiring.md` |
| All LCD geometry | `src/display/display_style.h` |
| Key handling | `src/app/inventatory_scan_app_input.cpp` |
| Battery maths | `include/InventatoryScanBattery.h` |
