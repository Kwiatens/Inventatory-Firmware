# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Changed
- The `›i` logo has no dot on any screen. The update screen shows its status in words and the progress bar, and a boot error shows only its text.
- The time left on battery, or to full, now shows from the first reading. Until the device has observed a rate it uses a typical one and marks the time with "~", for example "~6h 20m left". It replaces "Estimating time" on the dock card. The typical rates are `kBatteryPriorDischargeMsPerPercent` and `kBatteryPriorChargeMsPerPercent` and should be calibrated on the device.
- The power-off warning and the power-off ending are simpler. The warning is a centred title, a countdown bar and "Release * to cancel". The ending is the `›i` sinking behind a short rule, with "Press * to wake" under it.
- On the quantity screen the item name and its code no longer touch, and "Change quantity by:" is now "Change by:" so a four-digit quantity fits.
- The backlight picker says "Off after 5 min idle" instead of "Dims after 5 min idle", because the backlight only switches on and off.
- Screen layout numbers that were written inline in the renderer are now named in `display_style.h`, and centred lines share one helper.

### Added
- A DigiKey bag's Data Matrix sends its DigiKey part number and manufacturer part number, and a TME bag's QR label sends its TME part number and manufacturer part number, so the desktop records which vendor supplied the part. A DigiKey bag is looked up by its DigiKey part number, so a part already in inventory is found. A TME label needs no product page. Any other Data Matrix is refused with a message, and the part is added by hand. A TME bag's QR code was refused before.
- Holding `*` on an idle screen shows a countdown bar after one second, then powers the device off into deep sleep at four seconds. The `›i` sinks behind the glass. Releasing `*` before then cancels. Pressing `*` wakes it and the firmware restarts.
- The LiPo voltage is read on GPIO1 through a resistor divider and logged at boot.
- Operator-confirmed repair of an unreadable offline queue. Readable scans are kept.
- A faulty barcode scanner no longer stops the device from starting.
- An exclamation mark shows in the top bar while any problem stands: the barcode scanner is not replying, the scan queue cannot be read (`STO-03`), a scan is blocked (`SYN-01`), or the battery voltage cannot be read (`PWR-01`). The main screen and the menu then offer `# Fault info`, which shows the code (`SCN-01`), what it means and what to check. While the fault shows, the main screen reads "There is a problem" with the code below it, in place of "Ready to scan".
- Every problem the scanner reports has an error code, such as `SCN-01` or `STO-02`. The codes appear on the failure screens, and each one is explained at kwiatens.github.io/Inventatory-Site/docs/scanner/error-codes/.
- When several problems stand at once, the main screen and the fault screen show each code in turn, three seconds each.
- New firmware is confirmed only after 30 seconds of healthy running. A crash or reset before that, including powering the device off straight after an update, returns it to the previous firmware.
- Holding `#` opens the re-pair confirmation (idle only); `C` returns to Ready.
- The battery level shows in the top bar, in turn with the status and the time left. While the dock supplies power it shows "Charging" with a bolt and the time to full.
- A card appears when the dock is connected or removed.
- A backlight setting under Device settings / Display: Off, On, or Auto after 1, 3, 5 or 10 minutes idle. Warnings and pairing keep it lit.
- Energy saving under Device settings, off by default. When on, the device sleeps after 1, 3, 5, 10 or 30 minutes idle, then turns off after 1, 5, 10 or 30 minutes or 1 hour asleep, or never. A 30-second countdown on screen comes before each step, and a key press or scan cancels it. Asleep, the screen is dark, sync pauses and the scanner is off, and a short key press wakes it. After the automatic power off, only `*` wakes the device.
- A low-battery warning counts down 60 seconds, then powers the device off. A cell too low to run is refused at boot with a notice, and the device sleeps again.
- The operator can discard a queue that this firmware cannot read. The repair menu offers it after a confirmation, and the device works again straight away.

### Changed
- The power-off hold is four seconds of `*`, not 1.8 s. The first second opens the countdown, so a short press does nothing.
- The boot screen is the logo with the name and version and no animated dot. On wake the mark blooms in over the first 0.9 s of the 1.2 s splash hold, and the name appears once it is whole.
- Device settings opens Display, which holds the contrast and the backlight.
- Device settings lists Energy saving in place of Standby. Holding `D` still toggles standby.
- The firmware targets the Waveshare ESP32-S3-Nano (16 MB flash) instead of the Tenstar ESP32-S3-Zero. The LCD, keypad and scanner pins moved to match.
- A result message no longer closes a screen that you have moved on to.
- The pairing passkey changes after every attempt. Three failed attempts, or five minutes of advertising, lock pairing until `A` is pressed on the device.
- OTA updates are ignored during setup. A request that waits longer than two minutes is abandoned, and `C` cancels it.
- Typing `0` as a quantity can no longer produce a hidden default quantity.
- A scan that the server keeps rejecting can be discarded from the menu, after the operator confirms.
- A new outbox starts its event numbering at a random point, so the desktop never receives an event id it has already stored for the device.
- A 5xx response for the head event counts toward the "Sync blocked" warning, as a 4xx response already did.
- A partial barcode is dropped if its end does not arrive, instead of being reported as a scan.
- The scanner failure notice after standby quotes the code `SCN-01`, not `SCANNER ERROR`.
- "Queue unreadable" and "Sync blocked" on the Ready screen now read "There is a problem" with their code, `STO-03` or `SYN-01`, in place of the old words.
- The low-battery and empty-battery warnings show no error code. Only a battery reading that cannot be trusted has one: a voltage outside 2.5 V to 4.5 V for four seconds in a row shows `PWR-01` as a standing problem, with the top-bar mark.
- The failure codes take the numbers of the error-code reference. For example, `STORAGE ERROR` is now `STO-02`.
- The error codes run without gaps in each area, and a code that the device cannot show is not listed.
- A firmware update no longer shows `STO-03` when the queue holds no scans. The device takes over the empty queue and keeps its event sequence. Only a queue with scans waiting in it, from another firmware version, shows `STO-03`.
- The top bar's status gives way to the battery when the title leaves no room for it, as the runtime estimate already did.
- The host test suite covers the queue migrations, the replay counter and the provisioning parser. A coverage report is available with `make -C host-tests coverage`.

### Fixed
- A working barcode scanner no longer shows `SCN-01` at every start-up. The module confirms its save command after about 340 ms, and the firmware waited only 150 ms for that confirmation.
- A Data Matrix that the scanner ends with RS EOT and no line break, as a DigiKey bag's does, is read as a scan. Before, it was dropped without a message.
- A refused scan says why: an unrecognised scan, a label too long to send, or a Data Matrix without a DigiKey part number. Each one showed "Not a supported vendor bag" before.
- The charger's "charging complete" LED is debounced like the charging state, so a flickering LED does not re-show the dock card. With it lit, the estimate reads "Fully charged" to match the top bar.
- A sync result no longer replaces a settings or confirmation screen that the operator has open. Only the Ready, quantity and waiting screens take a result.
- A failed local save is shown instead of an earlier result.
- Bluetooth setup no longer keeps stale pairing data from an earlier attempt.
- A queue written by another firmware version, or a damaged queue header, no longer blocks scanning until the device is erased.
- An out-of-range queue head or count is refused instead of being read as a different queue.
- Text stored in the queue is never cut inside a multi-byte character.
- A stale acknowledgement can no longer let an OTA request start before the network task has paused.
- Values shared between the two cores are atomic, and the network mutex is never freed while it is in use.
- BLE provisioning status is published from the main loop, not from the BLE callback.
- The low-battery warning screen is aligned: the triangle and its `!` are centred, the countdown bar has no frame, and the countdown line sits under the bar.
- Repairing an unreadable offline queue is power-safe. The salvaged scans are written first, the queue header is committed last, and the old slots are erased after that. A power cut during the repair leaves the queue repairable again, and no scan is lost or queued twice.
- A response whose header is exactly 1,024 bytes is accepted, as the limit says. Before, it was refused.
- A failed OTA update stops its update listener, so it no longer stays open until the next reboot.
- Bluetooth setup starts at most once per boot. If the normal runtime fails right after setup, the device restarts into pairing. Before, a second start in the same boot advertised a duplicate service UUID, or did nothing.
- Each setup session no longer allocates new Bluetooth callback objects.
- A BLE disconnect no longer restarts advertising from inside the Bluetooth callback. Core 1 does it on its next pass.

### Security
- The transport HTTP status is published only after the response MAC verifies. A missing counter key fails closed.
- A sync response with a NUL escape or a repeated name is refused, whether or not the field is read. A repeated name counts as the same name after its escapes are decoded, so `"resultId"` repeats `"resultId"`.
- An HTTP response with a repeated framing header is refused, and its body is authenticated by its declared length, so a NUL byte cannot truncate it.
- A provisioning request with a NUL byte in any field is refused.

### Known issues
- Charging and the backlight need wiring that the board does not have yet: the charger status LEDs of the UPS module must reach GPIO4 (red, header A3) and GPIO38 (green, header MOSI), and an external transistor must switch the backlight from GPIO2 (header A1). Until then the charging screens never appear and the backlight modes do nothing.
- The battery curve is approximate and needs calibrating on the device with a full discharge.
- A barcode that is not recognised shows no code on the screen.
- After a queue repair, an older firmware cannot read the queue. It reports the queue as unreadable, and its repair menu discards the stored scans. Sync the scans to the PC before you flash an older build over a repaired queue.
- The BLE setup session has no hardware test on a board yet. The host tests cover the parser, the queue and the transport, not the Bluetooth stack.

## [0.1.0]
*(Not yet released)*
