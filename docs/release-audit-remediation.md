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
| FW-003 | P1 | Implemented | Every handled OTA failure clears OTA state, restores Wi-Fi sleep, resumes the network worker, and returns UI state to idle. | Inject each ArduinoOTA error on hardware and verify scanning/reconnection. |
| FW-004 | P1 | Verified statically | Tests now assert protocol v1 paths/vectors and current display clipping behavior; firmware and embedded test images compile. | Run Unity assertions on a connected ESP32-S3. |
| FW-005 | P1 | Implemented | Root GPL-3.0-only license, clean-checkout configuration bootstrap, and build/upload documentation added. | Repeat on a fresh public clone in CI. |
| FW-006 | P2 | Implemented | Inventory and quick-label result statuses use explicit allowlists; malformed/unknown statuses invalidate the whole response and display code fails closed. Regression cases compile. | Execute Unity cases on hardware and compatibility test against desktop app. |
| FW-007 | P2 | Implemented | BLE provisioning, persistent-state load/save/commit, and network-client startup all require exactly 32 bytes encoded as 64 hexadecimal characters. Token tests compile. | Pair/re-pair on hardware with valid and invalid payloads. |
| FW-008 | P2 | Implemented; hardware verification pending | HTTP headers (1,024 bytes), bodies (8,192 bytes), and total response bytes are bounded before append; decimal `Content-Length` is mandatory and exact; transfer encoding is rejected; HMAC remains before parsing/state mutation. The desktop now shares the 8 KiB body limit, bounds optional response text, rejects any callback response that still exceeds the limit with an authenticated retryable error, and covers realistic/maximum serialized responses. Authenticated desktop 5xx failures retain a pending quick-label request so an idempotent cached result can be delivered on retry. Boundary tests compile. | Execute the firmware parser against the released desktop and representative maximum inventory data; add live rogue-endpoint coverage when hardware is available. |
| FW-009 | P2 | Implemented | Scanner bytes accumulated during setup are flushed immediately before normal runtime starts. | Hardware pairing test with injected setup-mode barcode bytes. |
| FW-010 | P2 | Partially implemented | Cross-core flags and the task handle use atomics; failed worker startup releases resources before clearing the handle; runtime retries preserve ready-worker state and wait for pause acknowledgement before BLE/NVS transitions. Provisioning reset also pauses before clearing shared NVS. USB firmware build passes. | FreeRTOS stress/TSAN-equivalent review and reset-during-sync hardware test. |
| FW-011 | P2 | Implemented | GM65 boot-profile write failures now propagate from `begin()` and stop startup with an explicit scanner error. | Disconnect/fault GM65 UART on hardware and verify recovery behavior. |
| FW-012 | P2 | Implemented | Raw-byte, assembled barcode, code-bearing debug, and event-identifier serial logs were removed while scan/outbox/sync data flows remain intact. | Capture USB/UART during valid, invalid, and oversized scans. |
| FW-013 | P2 | Implemented | PlatformIO platform and direct libraries are exact-version pinned; USB is the default; OTA is isolated and reads its password from ignored configuration. | Reproduce USB and OTA image builds on clean Linux and Windows hosts. |
| FW-014 | P2 | Implemented | CI builds USB/OTA firmware, the test image, and display example; an opt-in self-hosted job executes hardware tests. README documents hardware, dependencies, licenses, and workflows. | First CI run and configured self-hosted hardware runner. |

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

Open items that need hardware or desktop behaviour: unreadable-queue recovery
at boot (a bad slot blocks scanning until NVS is erased), verifying GM65 replies
to configuration writes (boot success still derives from UART write counts),
OTA rollback (implemented with the SDK's app rollback: a new OTA image stays pending until it runs 30 s, but a crash-before-confirm rollback has not been exercised on hardware), and
the GM65 TX/RX wiring names against the physical board.

## Build and test evidence

- USB firmware: `pio run -e tenstar_esp32s3_zero_usb` — passed.
- Embedded test image compile-only command: `pio test -e tenstar_esp32s3_zero_usb
  --without-uploading --without-testing`: compiles the Unity image; it does not
  execute assertions.
- Hardware assertion command: `pio test -e tenstar_esp32s3_zero_usb`: uploads the
  image and executes assertions on an attached board; this command was not run.
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
