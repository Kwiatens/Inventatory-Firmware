# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- Signed firmware updates over Wi-Fi. The scanner pulls the update from the paired desktop over its authenticated connection and installs it only if an ECDSA P-256 signature by a trusted key covers it, it is for this product and hardware, its security counter is not lower than the scanner's minimum, and the downloaded image matches the signed hash. When the desktop offers an update, the Ready screen says "Update available"; D opens it, and nothing starts on its own. It shows the new version and installs only after A is pressed. Below 30% battery and off the charger it asks to charge first. The format and protocol are in `docs/ota-protocol.md`. The trusted public keys are in `src/config/ota_keys.h`; with none, updates come over USB only. This needs a desktop that offers signed updates, or `tools/ota_serve.py` for testing.
- `tools/sign_firmware.py` makes the signing key, signs a `firmware.bin` into a `.ota` package, and checks a package. Signing is done on the owner's PC, never in CI. CI publishes the unsigned `firmware.bin` and its SHA-256 for that step.
- A Mouser bag's Data Matrix is read as a vendor bag. The scanner sends its line, invoice and customer part number, plus the manufacturer when it fits, and the desktop adds or finds the part and looks it up on Mouser. Before, a Mouser bag was refused. This needs a desktop with Mouser bag support.

### Changed
- The Energy saving screen under Device settings is named Energy, and its row in Device settings reads Energy.
- A low cell is refused at boot on the battery percent the device shows, and on the voltage only at the 3.30 V floor. A load sags the cell's voltage, so a sagged reading at boot no longer refuses a cell the gauge holds above 10%. The serial log writes a `Boot battery:` line with the estimate, the refusal percent and the charger LED state.
- The `›i` logo has no dot on any screen. The update screen shows its status in words and the progress bar, and a boot error shows only its text.
- The battery percentage no longer follows the cell voltage. The device counts the charge out by the load it draws, and corrects that count from the voltage only after a long rest. While charging it counts the charge in, rises steadily, never falls, and reads 100% once the cell is full with the charger's LED dark. A radio burst, a scan or the charger's raised voltage no longer moves it.
- The charger is read through its red LED only; the green LED pad is no longer read. With the LED dark and the cell at 4.10 V or more, the cell is charged: the top bar reads "Charged" at 100%, with the bolt, and it holds there while the voltage stays at 4.00 V or more. Below that the estimate restarts from the voltage.
- On the charger the device is not refused at boot, warned or shut down for a low or empty cell, and energy saving does not sleep or turn it off. Dock mode is the only automatic power off while it is on the charger.
- The charger input is read on GPIO2 through the ADC: the midpoint of a 1 MOhm / 1 MOhm divider on the 5 V input, about 2.5 V plugged in and 0 V unplugged. The backlight moved from GPIO2 to GPIO38. Every 10 s the serial log writes the cell voltage, the charger midpoint and the LED state, for the bench check. The charge state does not use the charger input yet.
- The charger LED signal on GPIO4 is read with the ADC, not as a digital input, and its pull-down is gone. The divider midpoint is about 2.2 V when lit, which a digital input reads as low. The charging icon and the charging state follow the reading. Dock mode does not sleep the device yet, and a plug-in does not wake a device that is off, until the divider puts at least about 2.5 V on the pin.
- The battery voltage is corrected by a gain measured on the board: the reading was 3.9% low (3553 mV read for a 3696 mV cell), so a cell at 3.7 V no longer reads as 10% and refuses to boot. `hardware::kBatteryReadingScalePpm` holds the gain. It is fitted to one point and needs a second check near 4.1 V.
- The time left on battery, or to full, uses the same estimate. It shows from the first reading, marked "~" until enough load has been observed, for example "~6h 20m left". It replaces "Estimating time" on the dock card. The load, rest and charge values in `include/InventatoryScanBattery.h` are placeholders until they are calibrated on the device.
- The power-off warning and the power-off ending are simpler. The warning is a centred title, a countdown bar and "Release * to cancel". The ending is the `›i` sinking behind a short rule, with "Press * to wake" under it.
- On the quantity screen the item name and its code no longer touch, and "Change quantity by:" is now "Change by:" so a four-digit quantity fits.
- The backlight picker says "Off after 5 min idle" instead of "Dims after 5 min idle", because the backlight only switches on and off.
- Screen layout numbers that were written inline in the renderer are now named in `display_style.h`, and centred lines share one helper.
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
- The device log is no longer broadcast to the local network by default. Any host on the network could read it, including the line that says an OTA update is waiting. Set `kLogOverAirEnabled` in `src/log/device_log.h` to turn it on for bench work. `tools/device_log_listener.py` shows control bytes in a received line as `.`, so a datagram from another host cannot drive the terminal.

### Added
- Battery capacity under Device settings / Energy, on page 2 of that screen; D moves between the two pages, as on Quick labels. Type the cell's capacity in mAh, from 100 to 9999: A saves it, and C leaves it as it was. The default is 3400 mAh. The battery estimate counts in the capacity, and changing it keeps the percent on the screen.
- The serial log is also sent over the air. Each line is broadcast as a UDP datagram on port 5140 to the local network, so `python3 tools/device_log_listener.py` on a PC on the same Wi-Fi shows it while the device runs. The newest 48 lines are kept while Wi-Fi is down.
- Dock mode, row 4 of the Energy saving screen, off by default and independent of the energy saving switch. With it on, the device goes to standby about 5 s after the charger is plugged in: the screen is dark and the scanner and network are paused. Taking the charger off brings it back at once. With dock mode off, a device on the charger stays on.
- A DigiKey bag's Data Matrix sends its DigiKey part number and manufacturer part number, and a TME bag's QR label sends its TME part number and manufacturer part number, so the desktop records which vendor supplied the part. A DigiKey bag is looked up by its DigiKey part number, so a part already in inventory is found. A TME label needs no product page. Any other Data Matrix is refused with a message, and the part is added by hand. A TME bag's QR code was refused before.
- Holding `*` on an idle screen shows a countdown bar after one second, then powers the device off into deep sleep at four seconds. The `›i` sinks behind the glass. Releasing `*` before then cancels. Pressing `*` wakes it and the firmware restarts.
- The LiPo voltage is read on GPIO1 through a resistor divider and logged at boot.
- Operator-confirmed repair of an unreadable offline queue. Readable scans are kept.
- A faulty barcode scanner no longer stops the device from starting.
- An exclamation mark shows in the top bar while any problem stands: the barcode scanner is not replying, the scan queue cannot be read (`STO-03`), a scan is blocked (`SYN-01`), or the battery voltage cannot be read (`PWR-01`). The main screen and the menu then offer `# Fault info`, which shows the code (`SCN-01`), what it means and what to check. While the fault shows, the main screen reads "There is a problem" with the code below it, in place of "Ready to scan".
- Every problem the scanner reports has an error code, such as `SCN-01` or `STO-02`. The codes appear on the failure screens, and each one is explained at kwiatens.github.io/Inventatory-Site/docs/scanner/error-codes/.
- When several problems stand at once, the main screen and the fault screen show each code in turn, three seconds each.
- New firmware is confirmed after 30 seconds of healthy running, or earlier when the device is powered off or restarted on purpose once it has started. A crash or reset before that returns it to the previous firmware, and so does a low-battery refusal at start-up. The serial log says when an update was rolled back.
- Holding `#` opens the re-pair confirmation (idle only); `C` returns to Ready.
- The battery level shows in the top bar, in turn with the status and the time left. While the dock supplies power it shows "Charging" with a bolt and the time to full.
- A card appears when the dock is connected or removed.
- A backlight setting under Device settings / Display: Off, On, or Auto after 1, 3, 5 or 10 minutes idle. Warnings and pairing keep it lit.
- Energy saving under Device settings, off by default. When on, the device sleeps after 1, 3, 5, 10 or 30 minutes idle, then turns off after 1, 5, 10 or 30 minutes or 1 hour asleep, or never. A 30-second countdown on screen comes before each step, and a key press or scan cancels it. Asleep, the screen is dark, sync pauses and the scanner is off, and a short key press wakes it. After the automatic power off, only `*` wakes the device.
- A low-battery warning counts down 60 seconds, then powers the device off. A cell too low to run is refused at boot with a notice, and the device sleeps again.
- The operator can discard a queue that this firmware cannot read. The repair menu offers it after a confirmation, and the device works again straight away.

### Fixed
- The battery percent is checked against the cell voltage at boot. The device kept the percent it saved at power off, even when the cell had fallen far below it while the device was off: 98% at 3.65 V, for example. A saved charge more than 20 points above the cell's voltage is replaced by the voltage's charge, and a saved full charge is kept only while the cell is on the charger.
- Powering off also puts the GM65 barcode module to sleep, with its lamps off. Before, the chip slept but the module stayed powered and its lamps kept flashing, which drained the battery while the device was off. The module is woken at the next boot, before its profile is applied.
- Waking from standby no longer waits for the barcode scanner's setup to be sent again. The module keeps its settings while it is powered, so the screen comes back at once and key presses during the wait are no longer lost.
- After a power off, the barcode scanner is woken and then probed until it answers, for up to three seconds, before its setup is sent. Before, the setup was sent after a fixed half second, which a module still restarting may not answer. The device then showed `SCN-01` and read no codes until the module was power-cycled. A wake that gets no answer is logged as `GM65 did not wake`.
- The barcode scanner's lights go off when the device powers off. Before, the head lamp could stay on for about 20 seconds, because the module kept reading. The module now stops reading before it sleeps, and the boot profile sets its normal read mode again.
- The Scan R1 keeps trying a code for longer and tries again sooner after each read. The same barcode is not sent again within two seconds of its last read, so a code held in view is sent once.
- A code printed next to the scanned one, such as on a TME label, no longer shows "Scan not recognised" over the scan that was read. The scan waiting for its quantity stays on screen.
- A TME label whose manufacturer part number contains a `*`, such as `DS1023-1*40S21`, is read instead of refused.
- Scanning a TME label finds the part by its TME part number, so a part already in inventory is found. Before, the scanner asked by the manufacturer part number, and the part showed as not found.
- A TME label is read when it is scanned. The scanner's line end had been switched off, so the label was dropped.
- A scanner whose event numbers had fallen behind the desktop's records sent scans the desktop silently ignored, so they never showed "Done". The scanner now moves its numbers past the stored ones when its queue is empty at start-up.
- A working barcode scanner no longer shows `SCN-01` at every start-up. The module confirms its save command after about 340 ms, and the firmware waited only 150 ms for that confirmation.
- A Data Matrix that the scanner ends with RS EOT and no line break, as a DigiKey bag's does, is read as a scan. Before, it was dropped without a message.
- A refused scan says why: an unrecognised scan, a label too long to send, or a Data Matrix without a DigiKey part number. Each one showed "Not a supported vendor bag" before.
- The charged state is debounced like the charging state, so a flickering charger LED does not re-show the dock card. Charged, the estimate reads "Fully charged" to match the top bar.
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
- A signed reply from the PC that the scanner cannot read counts toward "Sync blocked". Before, it stalled the queue while the screen read "PC offline".
- An item name, location, message or label preset with a character such as `µ` or `Ω` shows correctly. The screen's fonts are Latin-1 and the desktop sends UTF-8, so "10µF" drew as "10Âµ F". Latin-1 characters now show as themselves, `Ω` as "Ohm", dashes as `-`, and other characters as `?`.
- Plugging in the charger during the energy saving power-off countdown cancels it. Before, the device still turned off on the charger.
- Holding `D` while an OTA update is waiting no longer enters standby. It dropped the request but left the update listener open; `C` cancels the update first.
- A TME label whose quantity is above 9999 is refused, as a Data Matrix already was. Before, the quantity screen cut it to four digits, so `QTY:10000` showed and was added as 1000.

### Security
- The transport HTTP status is published only after the response MAC verifies. A missing counter key fails closed.
- A sync response with a NUL escape or a repeated name is refused, whether or not the field is read. A repeated name counts as the same name after its escapes are decoded, so `"resultId"` repeats `"resultId"`.
- An HTTP response with a repeated framing header is refused, and its body is authenticated by its declared length, so a NUL byte cannot truncate it.
- A provisioning request with a NUL byte in any field is refused.
- The name from an mDNS answer is used in the HTTP `Host` header only when it holds DNS name characters, so a host on the network cannot add header lines to a sync request.
- The BLE pairing passkey is drawn after the Bluetooth radio starts, so the hardware random number generator has its entropy source, and without modulo bias.
- The transport keys derived for each MAC are cleared from memory after use.
- Bluetooth setup accepts only LE Secure Connections pairing. A PC without it fell back to legacy pairing, whose key a nearby listener can recover, and with it the Wi-Fi password and pairing token. Such a PC can no longer set up the scanner.
- The Wi-Fi password is no longer kept a second time by the Wi-Fi driver in its own storage, which re-pairing did not erase. A copy left by an earlier firmware is erased at start-up.
- A host that does not answer TCP no longer holds the network for minutes: a connection attempt now gives up after 1.5 s. Before, re-pairing and OTA updates could fail while the scanner waited. After three exchanges in a row without a signed reply, the scanner asks mDNS for the PC again instead of staying with the host that answered first.
- The CI logging check is `tools/check_logging.py`. It covers `deviceLog`, which every log line now goes through and which the old patterns missed, and it reads each file whole, so a logging call split over several lines is checked. The old multi-line `git grep -z` check never matched across lines.

### Known issues
- Charging and the backlight need wiring that the board does not have yet: the charger LED of the UPS module must reach GPIO4 (red, header A3) through a 1 MOhm / 1 MOhm divider, and an external transistor must switch the backlight from GPIO38 (header MOSI). Until then the charging screens never appear and the backlight modes do nothing.
- A full cell taken off the charger reads "Charged" at 100% until its voltage falls below 4.00 V, and only then does the estimate restart from the voltage. A connector-voltage pin would tell the two cases apart.
- The battery curve is approximate and needs calibrating on the device with a full discharge.
- A barcode that is not recognised shows no code on the screen.
- After a queue repair, an older firmware cannot read the queue. It reports the queue as unreadable, and its repair menu discards the stored scans. Sync the scans to the PC before you flash an older build over a repaired queue.
- The BLE setup session has no hardware test on a board yet. The host tests cover the parser, the queue and the transport, not the Bluetooth stack.
- A signed release is built from `config.example.h`, so it sets the example `DEVICE_ID`, fallback host and pins. Install it over Wi-Fi only on a scanner built with those settings; a scanner with its own id or pins needs its own build and key until these settings move out of the build.

### Removed
- ArduinoOTA, the `waveshare_esp32s3_nano_ota` build, `tools/ota_auth.py` and `OTA_PASSWORD`. Updates no longer use a password: the image is signed instead, so a host on the network can no longer install its own firmware during an update. Remove `OTA_PASSWORD` from your `config.h` when convenient; it is ignored.

## [0.1.0]
*(Not yet released)*
