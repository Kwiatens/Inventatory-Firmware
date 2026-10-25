# Inventory Scanner firmware audit remediation ledger

## Baseline

- Audited revision: `f5c0eb105836058be85ca8bcddd4f9966da930d1`
- Security scan: `a3b90110-7f18-4579-abc2-d90bf091f692`
- Phase 2 implementation branch: `firmware-release-hardening` (merged into
  `main`). Build evidence was captured at firmware source revision `5a35df1`;
  later cleanup commits changed only a comment and this ledger.
- The sealed Phase 1 report and scan artifacts are retained outside this
  worktree. This ledger records dispositions without replacing that baseline.

Status meanings: **implemented** means the source change and relevant build
checks are complete; **verified** additionally requires the listed regression
evidence; **blocked** means a release-gate check still requires physical
hardware or production provisioning evidence.

| ID | Severity | Disposition | Implementation and regression evidence | Remaining verification |
| --- | --- | --- | --- | --- |
| FW-001 | P1 | Implemented | Outbox slot/header mutations are staged and committed without destructive namespace clearing; failed pop retains the event. `prepareOutboxPop` regression coverage and test-image compilation pass. | Execute NVS write/read/power-loss fault matrix on hardware. |
| FW-002 | P1 | Implemented | One scan completed during `WaitingForResult` is retained in `deferredScan_` and processed after the result. Only one scan is kept: further scans during the wait are drained from the UART and ignored with a serial note. | Hardware UART test with result delay, repeated scans, and buffer saturation. |
| FW-003 | P1 | Implemented | Every handled OTA failure clears OTA state, restores Wi-Fi sleep, resumes the network worker, and returns UI state to idle. | Run the signed-update failures in board-day-checklist section 12 and verify scanning/reconnection. |
| FW-004 | P1 | Verified statically | Tests now assert protocol v1 paths/vectors and current display clipping behavior; firmware and embedded test images compile. | Run Unity assertions on a connected ESP32-S3. |
| FW-005 | P1 | Implemented | Root GPL-3.0-only license, clean-checkout configuration bootstrap, and build/upload documentation added. | Repeat on a fresh public clone in CI. |
| FW-006 | P2 | Implemented | Inventory and quick-label result statuses use explicit allowlists; malformed/unknown statuses invalidate the whole response and display code fails closed. Regression cases compile. | Execute Unity cases on hardware and compatibility test against desktop app. |
| FW-007 | P2 | Implemented | BLE provisioning, persistent-state load/save/commit, and network-client startup all require exactly 32 bytes encoded as 64 hexadecimal characters. Token tests compile. | Pair/re-pair on hardware with valid and invalid payloads. |
| FW-008 | P2 | Implemented; hardware verification pending | HTTP headers (1,024 bytes), bodies (8,192 bytes), and total response bytes are bounded before append; decimal `Content-Length` is mandatory and exact; transfer encoding is rejected; HMAC remains before parsing/state mutation. The desktop now shares the 8 KiB body limit, bounds optional response text, rejects any callback response that still exceeds the limit with an authenticated retryable error, and covers realistic/maximum serialized responses. Authenticated desktop 5xx failures retain a pending quick-label request so an idempotent cached result can be delivered on retry. Boundary tests compile. | Execute the firmware parser against the released desktop and representative maximum inventory data; add live rogue-endpoint coverage when hardware is available. |
| FW-009 | P2 | Implemented | Scanner bytes accumulated during setup are flushed immediately before normal runtime starts. | Hardware pairing test with injected setup-mode barcode bytes. |
| FW-010 | P2 | Partially implemented | Cross-core flags and the task handle use atomics; failed worker startup releases resources before clearing the handle; runtime retries preserve ready-worker state and wait for pause acknowledgement before BLE/NVS transitions. Provisioning reset also pauses before clearing shared NVS. USB firmware build passes. | FreeRTOS stress/TSAN-equivalent review and reset-during-sync hardware test. |
| FW-011 | P2 | Implemented | A GM65 boot-profile write failure shows "Scanner error" on the boot screen for two seconds, and startup continues. This matches the later remediation entry below. | Disconnect/fault GM65 UART on hardware and verify recovery behavior. |
| FW-012 | P2 | Implemented | Raw-byte, assembled barcode, code-bearing debug, and event-identifier serial logs were removed while scan/outbox/sync data flows remain intact. | Capture USB/UART during valid, invalid, and oversized scans. |
| FW-013 | P2 | Implemented | PlatformIO platform and direct libraries are exact-version pinned; USB is the default; OTA is isolated and reads its password from ignored configuration. | Reproduce USB and OTA image builds on clean Linux and Windows hosts. |
| FW-014 | P2 | Implemented | CI builds USB/OTA firmware, the test image, and display example; an opt-in self-hosted job executes hardware tests. README documents build, test, documentation, and license; the hardware pinout and dependency pins are in AGENTS.md and platformio.ini. | First CI run and configured self-hosted hardware runner. |

## Shared protocol reference

The desktop and firmware contract is documented in the [shared transport v1
protocol specification](https://github.com/Kwiatens/Inventatory-Software/blob/main/docs/scanner-transport-security.md).

## Code-review remediation (branch `fix/review-remediation`)

A second, model-assisted review found the items below. Each is implemented in
source and compiles; none has been exercised on hardware.

| Area | Change | Remaining verification |
| --- | --- | --- |
| Display | Result dwell timer is cancelled by every screen change; local save failures preempt stale results. | Scan twice within 3.5 s on hardware; fill the queue while a result is deferred. |
| Keypad | Holding `#` opens the re-pair confirmation (idle only); `C` returns to Ready. | Keypad walkthrough. |
| BLE setup | Passkey rotates after every attempt, three failures or five minutes lock pairing until `A`, bonds removed, stack calls stay on Core 1. | Pair with a real central; repeated wrong passkeys; lockout and re-arm. |
| OTA | Ignored in setup, two minute wait timeout, `C` cancels, placeholder password rejected at build time. | Time out and cancel on hardware. |
| Transport | HTTP status is published only after the response MAC verifies; counters are reserved in blocks of 32; a missing counter key fails closed; `null` optional sync objects are accepted as absent. | Live exchange with the desktop; reboot mid-block. |
| OTA rollback | A new OTA image stays pending-verify until the app has run for 30 s; a reset before that returns to the previous image (the Arduino core's automatic confirmation is disabled). | Flash a deliberately crashing OTA image and confirm the previous image returns. |
| Outbox | Operator-confirmed discard of a stuck head event; legacy migration only without a current header; bounded legacy string copies. | NVS fault injection (also FW-001, FW-010). |
| Quantity | A typed `0` can no longer produce a hidden default quantity. | Hardware keypad check. |
| CI | The logging gate fails closed and has real patterns; jobs have timeouts. | First CI run. |

| GM65 scanner | The boot and wake profile verifies the module's 7-byte acknowledgement for every write (retried three times); a missing acknowledgement shows a scanner error but the device keeps running. | Disconnect and fault the GM65 UART on hardware; confirm real modules acknowledge as the manual states. |
| Outbox recovery | An unreadable queue shows "Queue unreadable" and offers an operator-confirmed repair that keeps readable scans. | NVS fault injection with a corrupted slot, including a power cut during repair (a small loss window is documented in the code). |

Open items: confirm the GM65 TX/RX wiring names against the physical board, and
the hardware release gates listed below.

## Build and test evidence

- The board is the Waveshare ESP32-S3-Nano (`waveshare_esp32s3_nano_*` in `platformio.ini`). The
  `tenstar_esp32s3_zero_usb` environment in the 5a35df1 evidence below was removed when the firmware
  moved to that board.
- USB firmware: `pio run -e waveshare_esp32s3_nano_usb` — passed (rebuilt after the review changes in
  the section below).
- Embedded test image compile-only command: `pio test -e waveshare_esp32s3_nano_usb
  --without-uploading --without-testing`: compiles the Unity image; it does not execute assertions.
- Hardware assertion command: `pio test -e waveshare_esp32s3_nano_usb`: uploads the image and
  executes assertions on an attached board; this command was not run.
- Verified USB image from `5a35df1`: 77,300 bytes RAM (23.6%) and 1,450,997
  bytes flash (88.6% of the configured application partition). The OTA image
  uses the same RAM and flash size. Both builds completed successfully without
  flashing hardware; PlatformIO emitted its expected OTA upload-port advisory
  because no device host was supplied.
- No firmware was flashed and no production configuration or live inventory
  data was accessed during remediation.

## Release gate

The source is not yet declared release-ready. Required physical evidence is:

1. Execute the Unity suite on the target ESP32-S3/Scan R1 assembly.
2. Run NVS fault and reset-during-sync tests for FW-001 and FW-010.
3. Exercise delayed-result scanning, every OTA error callback, BLE pairing,
   scanner-profile failure, and serial-log capture on hardware.
4. Validate the 8 KiB authenticated response-body contract with the released
   desktop application and representative maximum inventory data.
5. Record production flash-encryption, secure-boot, JTAG, and eFuse policy; the
   repository cannot prove those device-provisioning controls.

## Review follow-up (test-suite work, October 2026)

A read-only review of the firmware and its test suite, followed by fixes. The
analysis and the plan are in `docs/testing.md`. Verification levels: **host** means
the host suite (`make -C host-tests test`) exercises the change; **compiled** means
the USB and OTA images and the Unity image build for the ESP32-S3; **hardware
pending** means the change needs a board run before the release gate can close.

| Item | Change | Verification |
| --- | --- | --- |
| Outbox header that is another layout or damaged | Refused and kept byte-for-byte, reported as an unreadable queue. The operator's confirmed repair discards it; the device then works again. Before this, scanning stayed refused until erased. | host, compiled |
| Out-of-range outbox head, count or sequence | Refused instead of clamped into a different queue, for the current layout and for legacy blobs. | host, compiled |
| Event id after an erased outbox | A new outbox starts its event sequence at a random point, so ids cannot repeat ids the desktop already holds. Existing queues are unchanged. | host, compiled |
| Legacy outbox layouts v2 and v4 | Migration covered by fixed-layout tests, including a power cut at every write. The migration code is kept. | host |
| UTF-8 in stored text | Text is cut at a character boundary, never inside a multi-byte sequence. | host, compiled |
| Sync response with a NUL escape (`\u0000`) | Refused whether or not the field is read, matching the desktop parser. | host, compiled |
| Repeated name in a sync response object | Refused for every member, not only the members the parser reads. | host, compiled |
| GM65 partial frame after a pause | Dropped and never reported as a scan. The frame logic is in `include/InventatoryScanGm65Frame.h`. | host, compiled, hardware pending |
| Pause acknowledgement for OTA | Cleared before a new pause is requested, so a stale acknowledgement cannot satisfy it. | compiled, hardware pending |
| Shared state across cores | Sync timestamp and mutex handle are atomic. The mutex is never freed while the app runs. | compiled, hardware pending |
| BLE provisioning status | Written only from Core 1; the BLE callback queues the text. | compiled, hardware pending |
| BLE provisioning request | Parsed by length; a NUL byte in any field is refused. | host, compiled |
| "Sync blocked" rule | 5xx responses for the head event count toward it, as 4xx responses and unacknowledged 200 responses already did. | host, compiled |
| HTTP response framing | The body is copied by its declared length; a repeated framing header is refused. The real client runs on the host against a scripted server. | host, compiled, hardware pending |
| Replay counter (provisioning store) | Covered by host tests for monotonic, skip-not-reuse, power cut and missing-key behaviour. No code change. | host |

Image size after this review (`pio run -e waveshare_esp32s3_nano_usb`): RAM 79,476 bytes
(24.3% of 327,680) and flash 1,495,753 bytes (91.3% of the 1,638,400-byte application
partition). The earlier figures in this file were for `5a35df1`. The flash headroom is
now about 143 KB, which the release notes should mention when the OTA partitions are reviewed.


## Release check (October 2026)

A read-through of the firmware, its tools, CI and documentation before the public release, followed by
fixes. Verification levels are those of the section above. The ESP32 images were **not** built in this
review (PlatformIO could not be installed), so no row is "compiled"; **stub** means the file was
type-checked against the stub headers in `tools/dev-harness/`.

| Item | Change | Verification |
| --- | --- | --- |
| OTA with the example password | A build made from `config.example.h` accepted OTA uploads signed with the public placeholder. The firmware now refuses OTA while `OTA_PASSWORD` is the placeholder or shorter than 12 characters (`isUsableOtaPassword`), the rule `tools/ota_auth.py` already applied to the OTA build. | host, stub, hardware pending |
| Logging gate | The CI patterns matched `Serial` and `sendDebugMessage` only, but every log line now goes through `deviceLog`. The multi-line check used `git grep -z`, which does not join lines, so it never matched. Replaced by `tools/check_logging.py`, which covers `deviceLog` and reads each file whole. No current log line names a sensitive value. | script run, planted-line probe caught |
| Device log broadcast | On by default, readable by any host on the network, including the "OTA ready" line. Now off by default (`kLogOverAirEnabled`); the listener shows control bytes as `.`. | stub (listener run) |
| mDNS name in the Host header | An mDNS answer could put CR LF into the request's `Host` header. Names are now limited to DNS characters, else the address is used (`isSafeHttpHostName`). | host |
| BLE passkey entropy | The first passkey was drawn before the Bluetooth radio started, when `esp_random()` is only pseudo-random. It is now drawn after `BLEDevice::init`, without modulo bias. | stub (throwaway BLE stubs), hardware pending |
| Derived transport keys | Cleared from the stack after each MAC (`wipeSecret`). | host |
| TME quantity above 9999 | `QTY:10000` was cut to four digits on the quantity screen and added as 1000. The label is now refused, as a Data Matrix already was. | host (failing test first) |
| Public repository | The agent handoff note `docs/handoff/HANDOFF.md` was removed; the stub harness moved to `tools/dev-harness/`. `CHANGELOG.md` had two Changed and two Fixed sections under Unreleased, now merged. `SECURITY.md` states the OTA, mDNS, log and flash-encryption assumptions. | review |

Repository scan: no credentials, private keys, personal paths, host names or addresses were found in the
tree or in the history of every branch, `src/config/config.h` was never committed, and the PNG and SVG
files carry no personal metadata.

Still open, and not changed here: the OTA image is not signed (ArduinoOTA checks only the password; since
replaced by signed updates, see docs/ota-protocol.md); every
build from the example configuration shares the device id `Inventatory-SCANNER-R1`, so two such scanners on one
network share an mDNS name; `bootNonce_` and the first outbox sequence are drawn before the radio starts,
which affects only their uniqueness, not a secret.

### Security deep dive (same review)

A separate read-only security pass over the firmware, the desktop side of the protocol and the exact
Arduino-ESP32 sources the build uses. Dispositions:

| Finding | Disposition | Verification |
| --- | --- | --- |
| Self-hosted hardware runner reachable from fork pull requests | Fixed in CI: the self-hosted `hardware-tests` job is removed, so every job runs on GitHub-hosted runners, and hardware tests run on the bench PC by hand. A pull request could still add a workflow for a runner that stays registered, so `docs/releasing.md` says to remove it. | owner action pending: unregister the runner |
| ArduinoOTA does not authenticate the image; a LAN man-in-the-middle can install firmware during a real update | Fixed: ArduinoOTA is removed. The scanner pulls a signed package over the authenticated channel and installs it only after the ECDSA P-256 signature, the product, the security counter and the image hash check out (docs/ota-protocol.md). | host (signed vectors), hardware pending (board-day-checklist section 12) |
| Legacy BLE pairing accepted when the central lacks LE Secure Connections | Fixed: `ESP_BLE_SM_ONLY_ACCEPT_SPECIFIED_SEC_AUTH` refuses it. | stub, hardware pending (pair with an SC-capable PC, and confirm a legacy-only central is refused) |
| `WiFiClient::setTimeout(250)` is seconds, so a SYN-dropping host stalls the worker | Fixed: `connect()` with 1500 ms; the host shim has no `setTimeout`, so a call fails the host build. | host, hardware pending |
| OTA password crackable offline, fleet-wide, on the command line | Resolved: the OTA password and `espota` are removed with ArduinoOTA; signed packages need no shared secret. | host |
| Wi-Fi driver kept its own SSID, password and PMK in `nvs.net80211` | Fixed: `WiFi.persistent(false)` before any `WiFi.mode()`, the namespace erased at boot and on re-pair. | host, hardware pending (read the NVS partition of a provisioned unit) |
| First mDNS answer pinned even when it never authenticates | Fixed: dropped after three exchanges in a row without a signed reply. Only the first mDNS answer is still tried. | host |
| Signed but unreadable 200 stalled the queue silently | Fixed: counts toward "Sync blocked". The desktop should also replace invalid UTF-8 in `jsonEscape`. | stub; desktop change open |
| OTA rollback after a deep sleep or restart within 30 s of an update | Fixed: a pending image is confirmed before a deliberate power off or re-pair restart once `loop()` has run (`src/ota/firmware_validity.cpp`). A refusal during start-up, such as the low-battery notice, leaves it pending, since it proves nothing about the image. A rollback is logged at boot. | stub, hardware pending (OTA, then power off with `*` within 30 s: the new image must stay) |
| Logging gate blind spots (`;` in literals, log macros, `printf`) | Fixed in `tools/check_logging.py`; probe cases caught. | script run |
| Label request nonce drawn before the radio starts | Fixed: drawn at the first label print. | stub |
| `DEVICE_ID` unvalidated; advertised setup name shared by default builds | Documented in `config.example.h`. | open |
| A signed 409 counts as a refusal of the head event | Open: a separate state for 409 needs the desktop's replay rules. | open |
