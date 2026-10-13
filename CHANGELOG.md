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
- The battery level shows in the top bar, in turn with the status and the time left. While the dock supplies power it shows "Charging" with a bolt and the time to full.
- A card appears when the dock is connected or removed.
- A backlight setting under Device settings / Display: Off, On, or Auto after 1, 3, 5 or 10 minutes idle. Warnings and pairing keep it lit.
- A low-battery warning counts down 60 seconds, then powers the device off. A cell too low to run is refused at boot with a notice, and the device sleeps again.

### Changed
- The boot screen is a static logo with the name and version. The animated dot and the build-up are gone; the splash holds for about a second.
- Device settings opens Display, which holds the contrast and the backlight.
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
- Charging and the backlight need wiring that the board does not have yet: the dock must drive GPIO38 (header MOSI), and an external transistor must switch the backlight from GPIO2 (header A1). Until then the charging screens never appear and the backlight modes do nothing.
- The battery curve is approximate and needs calibrating on the device with a full discharge.
- If power is lost while the operator repairs an unreadable offline queue, an event can be queued twice. Repair needs a power-safe commit before release; the host test `test_host_outbox_power_cut_during_repair_does_not_duplicate_events` documents the defect.
- A damaged queue header (not a missing record) blocks new scans and cannot be repaired from the menu yet.

## [0.1.0]
*(Not yet released)*
