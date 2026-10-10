# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- Operator-confirmed repair of an unreadable offline queue (readable scans are kept).
- GM65 profile writes are verified against the module's acknowledgement; a scanner fault no longer stops the device from booting.
- OTA images are confirmed only after 30 s of healthy runtime; a crash or reset before that (including powering the device off right after an update) rolls back to the previous image.
- First-party keypad matrix scanner with two-sample debounce and per-key hold timing; the `Keypad` library dependency is removed.
- Holding `#` opens the re-pair confirmation (idle only); `C` returns to Ready.
- CI logging gate fails closed and has real patterns; jobs have timeouts.
- Host unit-test build (`host-tests/`) runs the shared Unity suite on a desktop; CI runs it with AddressSanitizer and UndefinedBehaviorSanitizer.
- Outbox durability tests inject write failures and power loss at each NVS write, then reboot the store.

### Changed
- Result dwell timer is cancelled by every screen change.
- Passkey rotates after every attempt, three failures or five minutes lock pairing until `A`.
- OTA ignored in setup, two minute wait timeout, `C` cancels.
- Counters are reserved in blocks of 32.
- `null` optional sync objects are accepted as absent.
- Legacy migration only without a current header; bounded legacy string copies.
- A typed `0` can no longer produce a hidden default quantity.
- Operator-confirmed discard of a stuck head event.
- Contributing guide's logging-gate patterns now match the CI workflow.

### Fixed
- Local save failures preempt stale results.
- Bonds removed, stack calls stay on Core 1 for BLE setup.
- Placeholder password rejected at build time.
- Timing helpers use fixed-width timestamps, so millis() wrap-around is handled the same on any host.
- Outbox clears records by assignment instead of `memset`, keeping the member initialisers.
- OTA password build error explains that quotes and backslashes are not supported.

### Security
- Transport HTTP status is published only after the response MAC verifies; a missing counter key fails closed.

### Known issues
- If power is lost while the operator repairs an unreadable offline queue, an event can be queued twice. Repair needs a power-safe commit before release; the host test `test_host_outbox_power_cut_during_repair_does_not_duplicate_events` documents the defect.
- A damaged queue header (not a missing record) blocks new scans and cannot be repaired from the menu yet.

## [0.1.0]
*(Not yet released)*
