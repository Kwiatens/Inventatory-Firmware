# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- Holding `*` on the Ready screen powers the device off into deep sleep. Pressing `*` wakes it and the firmware restarts.
- The LiPo voltage is read on GPIO1 through a resistor divider and logged at boot.
- Operator-confirmed repair of an unreadable offline queue. Readable scans are kept.
- A faulty barcode scanner no longer stops the device from starting.
- New firmware is confirmed only after 30 seconds of healthy running. A crash or reset before that, including powering the device off straight after an update, returns it to the previous firmware.
- Holding `#` opens the re-pair confirmation (idle only); `C` returns to Ready.

### Changed
- The firmware targets the Waveshare ESP32-S3-Nano (16 MB flash) instead of the Tenstar ESP32-S3-Zero. The LCD, keypad and scanner pins moved to match.
- A result message no longer closes a screen that you have moved on to.
- The pairing passkey changes after every attempt. Three failed attempts, or five minutes of advertising, lock pairing until `A` is pressed on the device.
- OTA updates are ignored during setup. A request that waits longer than two minutes is abandoned, and `C` cancels it.
- Typing `0` as a quantity can no longer produce a hidden default quantity.
- A scan that the server keeps rejecting can be discarded from the menu, after the operator confirms.

### Fixed
- A failed local save is shown instead of an earlier result.
- Bluetooth setup no longer keeps stale pairing data from an earlier attempt.

### Security
- The transport HTTP status is published only after the response MAC verifies. A missing counter key fails closed.

### Known issues
- If power is lost while the operator repairs an unreadable offline queue, an event can be queued twice. Repair needs a power-safe commit before release; the host test `test_host_outbox_power_cut_during_repair_does_not_duplicate_events` documents the defect.
- A damaged queue header (not a missing record) blocks new scans and cannot be repaired from the menu yet.

## [0.1.0]
*(Not yet released)*
