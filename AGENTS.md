# Inventatory Scan R1 firmware coding-agent guide

This document is the authoritative repository-level operating guide for coding
agents (Codex, Gemini CLI, Claude, and autonomous tools) working on the
Inventatory Scan R1 firmware.

The goal of this guide is to enable autonomous agents to make safe, verified, and
minimal changes without compromising firmware correctness, mutual transport
security, offline data durability, or existing user work.

---

## 1. Agent autonomy boundaries

To prevent irreversible hardware damage, data loss, or repository pollution,
coding agents must operate within explicit operational boundaries:

### Permitted autonomously
- Inspecting source code, configuration headers, tests, and documentation.
- Editing files strictly within the user-assigned task scope.
- Executing non-destructive static checks and firmware compilations (`pio run`,
  `pio test --without-uploading --without-testing`).
- Running non-destructive embedded tests on attached hardware only when explicitly
  requested by the user.

### Prohibited without explicit authorization
- **Never flash firmware to connected hardware** (`pio run -t upload` or `esptool.py`).
- **Never trigger over-the-air updates** (`pio run -e tenstar_esp32s3_zero_ota -t upload`).
- **Never erase NVS partitions or trigger provisioning resets** (`SetupStateStore::clear()`).
- **Never perform destructive electrical or hardware testing**.
- **Never push git branches, open public PRs, create git tags, or publish releases**.
- **Never create git commits** unless the user prompt explicitly requests a commit.
- **Never discard, overwrite, or revert unrelated working-tree modifications**.

> [!CAUTION]
> A general user request to "build", "test", or "verify" firmware grants authority
> to compile binaries and run non-flashing static checks. It does **not** grant
> authority to flash connected microcontrollers or reset stored credentials.

---

## 2. Authoritative documentation references

Before proposing or modifying firmware behavior, consult the authoritative
specifications in the tree:

- `README.md`: Hardware wiring pinout, platform dependencies, and clean-checkout setup.
- `SECURITY.md`: Vulnerability reporting process and transport security boundaries.
- `docs/release-audit-remediation.md`: Authoritative remediation ledger (FW-001
  through FW-014), verification status, and hardware release gates.
- [Shared transport v1 protocol specification](https://github.com/Kwiatens/Inventatory-Software/blob/main/docs/scanner-transport-security.md): replay counter
  mechanics and 8 KiB response-body bounds.
- [Shared project standards](https://github.com/Kwiatens/Inventatory-Software/blob/main/docs/project-standards.md): commit, changelog, licensing, secrets and verification rules that both repositories follow.

Do not guess protocol or hardware requirements when an authoritative document exists.

---

## 3. Project overview and hardware specifications

Inventatory Scan R1 is an embedded C++17 firmware application running on the
Tenstar ESP32-S3-Zero microcontroller board.

### Target microcontroller
- **MCU**: Espressif ESP32-S3 (dual-core Xtensa LX7, 240 MHz).
- **Internal RAM**: ~320 KB internal SRAM.
- **External PSRAM**: None (0 MB). Dynamic heap memory is strictly constrained.
- **Flash**: 4 MB SPI flash on Quad I/O (`qio`, 80 MHz).
- **USB**: Native USB CDC enabled at boot (`ARDUINO_USB_CDC_ON_BOOT=1`).
- **Wireless**: Wi-Fi 4 (802.11 b/g/n, 2.4 GHz) and Bluetooth Low Energy (BLE 5.0).

### Flash partition layout (`partitions.csv`)
| Partition | Type | SubType | Flash Offset | Size (Hex) | Size (Dec / Human) | Purpose |
| --- | --- | --- | ---: | ---: | ---: | --- |
| `nvs` | `data` | `nvs` | `0x009000` | `0x005000` | 20,480 B (20 KiB) | Preferences & durable outbox |
| `otadata` | `data` | `ota` | `0x00e000` | `0x002000` | 8,192 B (8 KiB) | OTA boot state & slot selection |
| `app0` | `app` | `ota_0` | `0x010000` | `0x190000` | 1,638,400 B (~1.56 MB) | Primary application firmware |
| `app1` | `app` | `ota_1` | `0x1a0000` | `0x190000` | 1,638,400 B (~1.56 MB) | Secondary OTA update slot |
| `spiffs` | `data` | `spiffs` | `0x330000` | `0x0d0000` | 851,968 B (~832 KiB) | Data partition |

*Historical build reference*: At git revision `5a35df1`, a release build consumed
1,450,997 bytes of flash (88.6% of `app0`) and 77,300 bytes of boot RAM (23.6% of
internal SRAM). These figures represent point-in-time measurements, not fixed limits.

### Pinned hardware wiring and peripherals
Authoritative pin assignments are defined in `src/config/hardware.h` and
`src/config/config.example.h`:

| Peripheral | Signal | GPIO | Notes |
| --- | --- | ---: | --- |
| **GMG12864-06D SPI LCD** | SCL (Clock) | `9` | 128x64 ST7565/UC1608 controller, 3.3V |
| | RS / DC (Command) | `10` | Driven via U8g2 4-wire SPI |
| | RESET | `11` | Must share electrical ground with ESP32-S3 |
| | CS (Chip Select) | `12` | Active low |
| | SI / MOSI (Data) | `13` | SPI data out |
| **GM65 Barcode Scanner** | ESP32 TX (`GM65_TX_PIN`) | `43` | ESP32 transmit -> GM65 RX |
| | ESP32 RX (`GM65_RX_PIN`) | `44` | ESP32 receive <- GM65 TX |
| **4x4 Keypad Matrix** | Columns 1-4 | `0, 1, 7, 6` | Scanned by first-party code in `src/keypad/keypad.cpp` |
| | Rows 1-4 | `4, 5, 3, 2` | Scanned and debounced on Core 1 |

---

## 4. Mandatory invariants vs. implementation choices

To maintain architectural integrity while allowing bug fixes and improvements,
agents must distinguish between immutable invariants and mutable implementation details:

### Mandatory invariants (Must never be broken)
1. **Zero sensitive logging**: Passwords, Wi-Fi credentials, pairing tokens,
   HMAC keys, raw GM65 UART receive buffers, and decoded barcode payloads must
   never be output to Serial or network debug messages. Enforced by CI greps.
2. **Mutual authentication & directional keys**: Every `POST /api/v1/device/sync`
   request must be signed with HMAC-SHA-256 using the derived `client-to-server`
   key. Every response must verify the MAC using the derived `server-to-client`
   key.
3. **Replay resistance & monotonic counters**: Request counters must increment
   monotonically and be reserved in NVS before transmission.
4. **Strict response bounds**: Response bodies must not exceed 8,192 bytes
   (`kMaxHttpBodyBytes`). Exact decimal `Content-Length` is mandatory; chunked
   transfer encoding is rejected. Headers must not exceed 1,024 bytes.
5. **Fail-closed parsing**: Any signature mismatch, malformed JSON, truncated
   body, or unknown status string invalidates the entire response.
6. **Data durability**: Committed outbox events must survive power loss, resets,
   and network failures. An event must remain queued until the server's acceptance
   is confirmed and the queue index update is durably committed to NVS.
7. **Concurrency boundaries**: No U8g2 display rendering or keypad polling may
   occur from Core 0 (Network worker). No blocking network calls may occur from
   Core 1 (UI loop), except the ArduinoOTA upload exception in section 6.
8. **NVS namespace safety**: NVS namespace identifiers must not exceed 15
   characters (plus null terminator). Calling `clear()` on the outbox namespace
   is prohibited.
9. **Memory safety**: Bounded buffers on all external inputs (raw GM65 UART
   frames <= 1,024 bytes, accepted decoded barcode payloads <= 512 bytes, and
   manufacturer part numbers <= 64 chars). No unbounded heap allocations.

### Implementation choices (May evolve with proper justification)
- The internal breakdown of helper methods across `inventatory_scan_app_*.cpp`.
- The specific choice of synchronization primitives (e.g. FreeRTOS mutexes vs. queues).
- The exact layout of individual UI screens, provided they fit the 128x64 display.
- Optimization of NVS slot representations, provided backward compatibility and
  atomic commitments are preserved.

---

## 5. Subsystem architecture and repository map

```text
├── boards/                     # Custom PlatformIO board definitions
│   └── tenstar_esp32s3_zero.json
├── docs/                       # Architectural documentation and audit ledgers
│   └── release-audit-remediation.md
├── examples/                   # Standalone smoke-test examples
│   └── gmg12864_spi_test/      # Independent SPI display verification
├── include/                    # Protocol definitions and core parsing algorithms
│   ├── InventatoryScanCore.h   # Barcode parsing, AIM stripping, DataMatrix decoding
│   ├── InventatoryScanHttp.h   # HTTP status constants and bounding limits
│   ├── InventatoryScanOutbox.h # Outbox serialization structures and queue models
│   ├── InventatoryScanSecurity.h # HMAC-SHA-256 and directional key derivation
│   ├── InventatoryScanSync.h   # JSON sync request/response data models
│   └── InventatoryScanUiPolicy.h # Display truncation and scan preemption policies
├── partitions.csv              # Flash partition map (NVS, app0, app1, spiffs)
├── platformio.ini              # Pinned build environments and direct dependencies
├── src/
│   ├── main.cpp                # Boot entrypoint, CDC init, lifecycle orchestration
│   ├── app/                    # Central application state machine & workers
│   │   ├── inventatory_scan_app.h/.cpp       # State machine, power modes, orchestration
│   │   ├── inventatory_scan_app_input.cpp   # Keypad dispatch, quantity composition
│   │   ├── inventatory_scan_app_network.cpp # FreeRTOS network task & HTTP client
│   │   ├── inventatory_scan_app_ota.cpp     # ArduinoOTA lifecycle and recovery
│   │   └── inventatory_scan_app_ui.cpp      # View-model mapping and LCD updates
│   ├── config/                 # Pinned pins, versioning, and configuration
│   │   ├── config.example.h    # Tracked template for private settings
│   │   ├── config.h            # Private, uncommitted build configuration (ignored)
│   │   ├── hardware.h          # LCD GPIO, contrast, and recovery constants
│   │   └── version.h           # Firmware version strings
│   ├── display/                # Graphic LCD driver subsystem (U8g2)
│   │   ├── display_controller.h/.cpp # Contrast persistence & frame scheduling
│   │   ├── display_model.h     # UI view models, menu trees, connectivity states
│   │   ├── display_renderer.h/.cpp # U8g2 rendering routines, layout, fonts
│   │   └── display_style.h     # UI typography, icons, and layout geometry
│   ├── gm65/                   # GM65 1D/2D imaging scanner UART driver
│   │   └── gm65_scanner.h/.cpp # UART serial polling, configuration, hardware trigger
│   ├── keypad/                 # Keypad matrix driver
│   │   └── keypad.h/.cpp       # 4x4 matrix scanning, debouncing, key event dispatch
│   ├── net/                    # HTTP client and transport
│   │   └── inventatory_client.h/.cpp # Endpoint discovery, HMAC signing, HTTP transport
│   ├── setup/                  # BLE provisioning
│   │   ├── ble_provisioning.h/.cpp   # Arduino BLE (Bluedroid) service, passkey pairing
│   │   └── setup_state_store.h/.cpp  # NVS store for Wi-Fi and pairing secrets
│   └── storage/                # Persistent NVS storage engines
│       ├── device_settings_store.h/.cpp # Contrast, display options, counters
│       └── outbox_store.h/.cpp # 16-slot durable FIFO outbox queue (FW-001)
├── test/
│   └── test_inventatory_scan_core.cpp # Embedded Unity unit test suite
└── tools/
    └── ota_auth.py             # Pre-build script reading OTA password from config.h
```

---

## 6. Concurrency and FreeRTOS task boundaries

The firmware utilizes both ESP32-S3 cores using FreeRTOS:

### Core 1: Application and UI thread (Main loop)
- Runs standard Arduino `setup()` and `loop()`.
- Owns `InventatoryScanApp` state transitions.
- Polls GM65 UART and parses barcode symbologies.
- Scans and debounces the 4x4 keypad matrix.
- Drives U8g2 display rendering and contrast updates.
- Runs the ArduinoOTA upload (`ArduinoOTA.handle()` in `serviceOta()`) to completion
  inside `loop()`. This is the only blocking network exception on Core 1. It runs only
  when an OTA was requested with setup complete and saved Wi-Fi, `networkPauseAcknowledged_`
  is true, and Wi-Fi is connected. Progress is drawn from the `onProgress` callback.

### Core 0: Background network worker task
- Runs `networkTaskEntry` pinned to Core 0 with dedicated stack (8,192 bytes).
- Manages background Wi-Fi connection and reconnect backoff.
- Resolves mDNS service endpoints (`_inventatory._tcp`).
- Executes blocking HTTP sync requests (`client_.postSync`).
- Dispatches offline outbox events.

### Cross-core synchronization rules
1. **Display isolation**: Core 0 (Network worker) must **never** call `display_`,
   U8g2 methods, or keypad polling. All visual updates and hardware input remain
   on Core 1.
2. **Non-blocking UI**: Core 1 must **never** initiate blocking network calls,
   except the ArduinoOTA upload under Core 1 above. Jobs are passed to Core 0 via
   `queueSyncJob()`.
3. **Shared state protection**: Multi-field structures between Core 0 and Core 1
   are protected by `networkMutex_`. Lock contention must be minimal; do not hold
   `networkMutex_` during network I/O or display rendering.
4. **Atomic flags**: Single-variable state (`networkStarted_`, `networkPaused_`,
   `networkPauseAcknowledged_`, `powerMode_`, `wifiStarted_`) uses
   `std::atomic<T>`.
5. **Task lifecycle handshake**: When entering BLE provisioning or ArduinoOTA,
   Core 1 signals `networkPaused_ = true` and must await `networkPauseAcknowledged_`
   before touching shared NVS namespaces or modifying radio states.

---

## 7. Data durability, failure handling, and NVS persistence

Offline durability is managed by `OutboxStore` using the ESP32 Non-Volatile
Storage (NVS) partition (`0x5000` / 20 KiB).

### NVS storage layout & namespace rules
- **Namespaces**:
  - `inv_outbox`: Offline scan queue.
  - `inv_setup`: Provisioned Wi-Fi credentials and 32-byte pairing token.
  - `inv_device`: Persisted display contrast and device preferences.
- **15-character namespace limit**: NVS namespace strings must not exceed 15
  characters plus null terminator. Longer strings fail silently with `KEY_TOO_LONG`.
- **Per-record keys**: To avoid multi-page NVS chunk churn that rapidly exhausts
  the 20 KiB partition, each event is stored under its own key (`e0` through `e15`),
  coordinated by a small header (`hdr`).

### Outbox failure handling and persistence guarantees
Agents must observe the three distinct failure domains:

1. **Rejecting invalid operations**:
   - An event with an empty code, a code exceeding 64 characters, or a quantity
     delta of `0` is rejected immediately (`EnqueueResult::Rejected`) without
     mutating NVS or in-memory state.
2. **Handling scans that cannot be persisted**:
   - If the queue is at capacity (`kCapacity = 16`), enqueue returns
     `EnqueueResult::QueueFull`.
   - If NVS rejects a write, enqueue returns `EnqueueResult::StorageFailed`.
   - In both failure cases, `queueEvent()` displays an explicit failure screen
     on the LCD (`QUEUE FULL` or `STORAGE ERROR`) and aborts.
   - **Requirement**: A scan that cannot be durably stored must **never** be
     presented to the user as successfully queued.
3. **Retaining previously committed events**:
   - Outbox popping uses a two-phase staged commit:
     1. The server response is validated (correct HMAC, matching request counter,
        valid JSON).
     2. `prepareOutboxPop()` calculates the staged head and count.
     3. `writeHeader()` commits the updated queue header to NVS **before** memory
        state is updated or the slot payload is erased.
     4. If the NVS header write fails, the pop aborts and the event remains queued.
   - **Requirement**: Committed events must survive Wi-Fi drops, server 4xx/5xx
     errors, authentication rejections, malformed server responses, unexpected
     MCUs resets, and NVS write errors.
   - **Blocked head**: if the desktop keeps rejecting the oldest event (authenticated
     4xx, or the head never appears in `acceptedEventIds`, five times in a row), the
     Ready screen shows "Sync blocked". Only an operator-confirmed Menu action
     (`3  Discard stuck scan`, then `A`) removes it, through the same staged `pop()`.
     Events are never dropped automatically.
4. **Power-loss recovery**:
   - Enqueue writes the record slot **first** and the header **second**. A power cut
     leaves an unreferenced slot that is overwritten later; it never produces a
     header pointing to unwritten data.
   - Pop writes the header **first** and erases the slot **second**. A power cut
     leaves an orphaned slot outside the queue range, safely ignored on reboot.
   - Calling `prefs_.clear()` on `inv_outbox` is strictly prohibited.

---

## 8. Transport v1 security and protocol invariants

The firmware interacts with the desktop application via `POST /api/v1/device/sync`.
The security architecture is defined in `InventatoryScanSecurity.h` and must be
preserved:

### Mutual authentication & key derivation
- **Root pairing secret**: A 32-byte cryptographic random secret (represented as
  64 hexadecimal characters) provisioned via BLE. Pairing uses a passkey shown on the device LCD that changes after every attempt; three failed attempts or a five-minute advertising window lock pairing until `A` is pressed on the device.
- **Directional keys**:
  - Request key: `HMAC-SHA256(rootToken, "Inventatory Scan R1 transport v1 client-to-server")`
  - Response key: `HMAC-SHA256(rootToken, "Inventatory Scan R1 transport v1 server-to-client")`
- **Request MAC calculation**: Computed over:
  `"Inventatory Scan R1/v1\nrequest\n" + method + '\n' + path + '\n' + deviceId + '\n' + counter + '\n' + body`
- **Response MAC calculation**: Computed over:
  `"Inventatory Scan R1/v1\nresponse\n" + counter + '\n' + status + '\n' + body`
- **Constant-time comparison**: Response MAC verification must use constant-time
  comparison (`transportMacMatches`) to prevent timing side-channels.

### Replay protection & request counters
- Request counters are unsigned 64-bit integers (`uint64_t`).
- The device reserves counters in NVS (`SetupStateStore::reserveTransportCounter`)
  before transmission, in blocks of 32: the persisted value is a ceiling at or above
  every counter handed out, so a reset skips counters but never reuses one. Failed
  requests are retried with newly incremented counters.
- The desktop rejects any request with a counter less than or equal to the highest
  accepted counter.

### Bounded response contract
- **Max HTTP response body**: Exactly 8,192 bytes (`kMaxHttpBodyBytes`).
- **Max HTTP response headers**: Exactly 1,024 bytes (`kMaxHttpHeaderBytes`).
- **Transfer encoding**: Chunked transfer encoding is rejected. Decimal
  `Content-Length` header is mandatory and must match the received body length.
- **Fail-closed response handling**: Responses violating size limits or containing
  invalid HMACs, unknown status strings, or malformed JSON are dropped. The outbox
  event is retained for subsequent retry.

---

## 9. Display and UI interaction model

- **Display**: 128x64 GMG12864-06D SPI LCD driven by U8g2.
- **Noise mitigation**: High-frequency SPI noise can corrupt ST7565 column/page
  pointers. The display controller re-applies top-of-frame recovery commands
  every 250 ms (`kLcdStartLineRecoveryIntervalMs`) without re-transmitting the
  full framebuffer.
- **Text layout**: The screen width is 128 pixels. Text must be clipped or
  truncated using `InventatoryScanUiPolicy.h` helpers (`displayClippedPrefixLength`).
- **Keypad controls**:
  - `A`: Confirm action, Add stock (+qty), or lower display contrast.
  - `B`: Remove stock (-qty), or raise display contrast.
  - `C`: Menu toggle from Ready; Back, Cancel, or Dismiss elsewhere.
  - `D`: Move quantity cursor; advance to next Quick Labels page.
  - `#`: Short press deletes selected quantity digit; holding `#` (from the idle screen only) opens
    the "Pair with new PC?" confirmation; `A` confirms the reset.
  - `D` hold (about 1.8 s, `*` not pressed): toggles standby; ignored unless on the idle screen.
  - `*` + `D` hold (about 1.8 s): requests an OTA update; ignored if setup is incomplete.
  - `0`-`9`: Numeric digit entry or direct menu selection.
- **Scan responsiveness**: The scanner remains active while ordinary menus are open;
  scanning immediately aborts menu navigation and returns to the inventory workflow.
  Scanning is paused only during BLE pairing, standby, destructive confirmation
  dialogs, OTA updates, or active label printing.

---

## 10. Build, test, and verification workflows

### Clean-checkout setup
Bootstrap the uncommitted private build configuration:
```powershell
Copy-Item src/config/config.example.h src/config/config.h
```

### Build firmware targets
```powershell
# Build standard USB firmware (default environment)
pio run -e tenstar_esp32s3_zero_usb

# Build isolated OTA firmware
pio run -e tenstar_esp32s3_zero_ota
```

### Unity test image compile-only check (no device required)
Compiles the embedded Unity test image to verify types, syntax, and links without
requiring connected hardware:
```powershell
pio test -e tenstar_esp32s3_zero_usb --without-uploading --without-testing
```

Hardware assertion execution uploads and runs the Unity assertions on an
attached Scan R1 board:
```powershell
pio test -e tenstar_esp32s3_zero_usb
```

### Host unit tests (no device required)
Builds the same Unity suite for the desktop, with stand-ins for the Arduino, NVS and
mbedtls APIs (`host-tests/shim/`), and runs it. The NVS stand-in injects write failures
and power loss, so the outbox durability tests run here:
```bash
make -C host-tests test
make -C host-tests SANITIZE=address,undefined test   # what CI runs
```
This executes assertions on a 64-bit host. It does not exercise the ESP32 toolchain,
real NVS partitions or hardware timing.

### Standalone display smoke test
```powershell
pio run -d examples/gmg12864_spi_test -e tenstar_esp32s3_zero
```

### CI security audit checks
Verify the zero sensitive logging policy. Note that in PowerShell and standard
shells, `git grep` returns exit code `1` when no matches are found. A return of
zero matches indicates compliance:
```powershell
# Must return no matches (exit code 1)
git grep -n -E "GM65 rx|GM65 scan:" src
git grep -n -E "sendDebugMessage\(.*(scannedCode_|trimmed|forwardedCode|manufacturerPartNumber)" src
```

### Verification hierarchy (Reporting rules)
When reporting verification results, agents must distinguish between verification tiers:
1. **Compilation / Static build**: Proves syntax and link validity. Does **not**
   prove runtime behavior or assertion success.
2. **Automated Unity compilation**: Proves test harness compiles. Does **not**
   mean tests ran unless executed on hardware or on the host.
2a. **Host execution** (`make -C host-tests test`): Executes the shared suite and the
   host-only durability tests on the desktop. It proves the logic, not the device
   build, real NVS or hardware timing.
3. **Embedded hardware execution** (`pio test -e tenstar_esp32s3_zero_usb`):
   Executes assertions on a physical ESP32-S3 over serial.
4. **End-to-end integration**: Validates live HTTP/HMAC exchange against the
   Inventatory desktop application.

Agents must state explicitly which level was executed and must never claim a test
"passed" if it was only compiled.

---

## 11. Release-audit status and release gates

Before modifying security-sensitive or protocol-related subsystems, inspect
`docs/release-audit-remediation.md`. The ledger tracks audit findings (FW-001
through FW-014):

- **Completed remediations**: Outbox staged commits (FW-001), deferred scan
  retention (FW-002), OTA error recovery (FW-003), protocol allowlists (FW-006),
  token validation (FW-007), scanner boot flush (FW-009), GM65 error propagation
  (FW-011), serial log sanitization (FW-012), and dependency pinning (FW-013).
- **Outstanding hardware release gates**:
  1. Execution of embedded Unity suite on physical Scan R1 hardware.
  2. NVS write/power-loss fault injection testing (FW-001, FW-010).
  3. Live delayed-result scanning and OTA error injection on hardware.
  4. End-to-end 8 KiB body boundary validation with the released desktop application.
  5. Documenting manufacturing provisioning controls (secure boot, flash encryption,
     eFuses).

Agents must not declare the firmware "release-ready" based solely on compilation
success or static analysis.

---

## 12. Change and commit standards

The commit format, changelog rules and AI co-author trailers are in the
[shared project standards](https://github.com/Kwiatens/Inventatory-Software/blob/main/docs/project-standards.md).
The firmware-specific rules are:

- **Allowed types**: `feat`, `fix`, `refactor`, `perf`, `style`, `test`, `docs`,
  `build`, `ci`, `chore`, `revert`.
- **Allowed scopes**: the shared scopes `build`, `docs`, `project` and `release`,
  and the firmware scopes `app`, `display`, `keypad`, `net`, `ota`, `scan`,
  `security`, `setup` and `storage`.
- **Formatting**: Imperative present tense, lowercase start, no trailing period,
  72 characters or fewer.
- **Content**: Describe the change. Never include prompts, conversation IDs,
  credentials, device identifiers, or local paths. Do not commit build artifacts
  (`.pio/`, `src/config/config.h`).

---

## 13. Task completion and reporting requirements

When concluding any task, the agent must provide a factual summary containing:
1. **Summary of changes**: What was changed, why, and the rationale.
2. **Modified files**: Clickable file links to all touched paths.
3. **Verification executed**: Exact commands executed and their output (e.g. build
   succeeded, RAM/flash usage).
4. **Unexecuted checks**: Explicit list of checks that could not be run (e.g.
   physical hardware tests, live desktop sync) and why.
5. **Known discrepancies or remaining risks**: Any residual architectural
   concerns or unverified release-gate items.

---

## 14. Pre-commit checklist

- [ ] Authority: Checked `README.md`, `SECURITY.md`, and `docs/release-audit-remediation.md`.
- [ ] Autonomy: No physical flashing, OTA deployment, or credential resets performed.
- [ ] Memory safety: Zero dynamic `String` loops, bounded buffers, zero PSRAM respected.
- [ ] Concurrency: No display calls from Core 0 worker; no blocking network calls on Core 1.
- [ ] Persistence: NVS namespaces <= 15 chars; outbox pop staged before commit; pop failure retains event.
- [ ] Durability: Newly captured scans that fail NVS write show error banner and are not queued.
- [ ] Security: HMAC mutual auth, directional keys, monotonic counter reservation preserved.
- [ ] Zero logging: Passed CI grep rules (no raw barcodes, UART buffers, or secrets logged).
- [ ] Verification: `make -C host-tests test` passes, `pio test ... --without-uploading --without-testing` passes, and firmware builds pass.
- [ ] Working tree: No modified `config.h`, `.pio/`, `.bin` files, or unrelated user edits in diff.
- [ ] Commit format: Follows section 12 and the shared standards. New source files carry the SPDX header.
