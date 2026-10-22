# Board-day release-gate checklist

Run this on the Scan R1 assembly to close the open release gates in
[release-audit-remediation.md](release-audit-remediation.md). It follows the order that costs the
least if something fails early: safe checks first, then anything that writes flash or erases data.

How to use it:

- Tick a box only when you saw the expected result. Write the failure, not a tick, in the evidence
  log at the end.
- Keep a USB serial capture open for the whole session (`pio device monitor`, saved to a file).
- Use a **test** desktop database and a throwaway pairing. Nothing here needs live inventory.
- Steps marked **destructive** erase pairing or the scan queue, or flash the board.
- Stop and fix a failure before going on when the step says **blocker**.

## 0. Before the board is powered (no hardware)

- [ ] Clean checkout of the commit to release. `cp src/config/config.example.h src/config/config.h`.
- [ ] `pio run -e waveshare_esp32s3_nano_usb` builds. Note RAM and flash use (last record: 91.3% of the
      application partition).
- [ ] `pio run -e waveshare_esp32s3_nano_ota` builds (set a throwaway `OTA_PASSWORD` first).
- [ ] `pio test -e waveshare_esp32s3_nano_usb --without-uploading --without-testing` compiles.
- [ ] `make -C host-tests SANITIZE=address,undefined test` passes. `python3 tools/check_error_codes.py` passes.
- [ ] `python3 tools/check_logging.py` passes (the zero-logging check in `AGENTS.md` section 10).

**Blocker:** if a build fails, nothing below is worth running.

## 1. Wiring and power, with a meter (before first power-on)

- [ ] GM65 cable checked wire by wire, ground first (see [wiring.md](wiring.md)). Fill in the table there.
- [ ] GM65 supply voltage matches its label. GM65 TX is 3.3 V logic at GPIO 44.
- [ ] Charger LED signal (R+) measured against GND with the LED **lit and off**. Record both levels.
      **Expect:** about 4.4 V on the pad when lit. Through the divider, GPIO 4 reads about 2200 mV lit and about
      0 mV dark, with no pull resistor. The serial log's "LED lit" or "LED dark" follows it.
- [ ] GPIO 4 pin, through the 1 MOhm / 1 MOhm divider, reads about 2200 mV with the LED lit and about 0 mV
      dark. It is read with the ADC. A digital read would see about 2.2 V as low.
- [ ] The backlight transistor's gate is on GPIO 38 (header MOSI), with its pull-down. GPIO 2 is not used by it.
- [ ] Charger input divider midpoint is on GPIO 2 (header A1), with no pull resistor. Read the serial log every
      10 s: about 2500 mV plugged in, 0 mV unplugged. The "Charger input" line marks each change.
- [ ] Battery reading: the serial log's "Battery" value matches a multimeter on the cell. Record both values.
- [ ] Battery divider midpoint reads about half of the cell voltage on GPIO 1.
- [ ] Backlight transistor has its gate pull-down; backlight stays off with the chip in reset.

## 2. First flash and boot (**destructive**: writes flash)

- [ ] `pio run -e waveshare_esp32s3_nano_usb -t upload` succeeds.
- [ ] The serial capture shows boot, `Reset reason`, battery millivolts, and `Outbox ready`.
- [ ] Boot splash: the `›i` blooms in, then the name. **No dot** over the `i`.
- [ ] Unpaired board shows "Pair this scanner" with a 6-digit code.

## 3. Unity suite on the board

- [ ] `pio test -e waveshare_esp32s3_nano_usb` runs to the end. **Expect:** every test passes, no
      reset or stack overflow. Save the log. (This closes gate 1.)

## 4. LCD pass (look at every screen on the real panel)

Fonts and spacing were only previewed with stand-in fonts, so judge each screen on the glass. Look
for clipped text, text touching the footer, and misalignment. Also check contrast at 0, 45 (default)
and 63.

- [ ] Ready, Ready with queued scans, Ready with a standing problem (`There is a problem` + code).
- [ ] Quantity: receive and change. Type `9999` in change mode: **Expect:** "Change by:" and four digits
      fit; item name and code line do not touch.
- [ ] A long item name wraps to two lines without hitting the footer.
- [ ] Sending, Done, Check result, Needs attention.
- [ ] Menu (with and without item 3), Quick labels (one page and several), Device settings, Display,
      Contrast, Backlight, Energy, both delay pickers.
- [ ] Re-pair, discard and repair confirmations.
- [ ] Fault detail for each code that can stand: `SCN-01`, `STO-03`, `SYN-01`, `PWR-01`.
- [ ] Pairing screens: ready, locked, connecting, failed.
- [ ] **Power warning (hold `*`):** centred title, a bar that drains, "Release * to cancel".
- [ ] **Power-off ending:** `›i` sinks behind the short rule, "Press * to wake", then the panel goes dark.
- [ ] Low-battery warning, battery empty notice, and both energy-saving countdowns (triangle, text
      edge, bar, countdown line all clear of the screen edge).
- [ ] Dock card (charging, charged, on battery) and the top-bar cycle: status, battery %, time left.
- [ ] Update screen (during section 12). Compare the error screens with `docs/screens/*.png`.

## 5. Keypad

- [ ] Every key `0-9`, `A-D`, `*`, `#` registers once per press, no double entries.
- [ ] `D` short press moves the quantity cursor; `#` short press deletes a digit.
- [ ] Typing `0` first does nothing; a four-digit limit holds.
- [ ] `D` hold ~1.8 s toggles standby from Ready only. `#` hold opens "Pair with new PC?"; `C` returns.
- [ ] `*` hold: warning at 1 s, power off at 4 s; releasing, or pressing `D` or `#`, cancels.
- [ ] `*` held **while powering on** does not power off until released and pressed again.

## 6. Scanner (GM65)

- [ ] Boot with the scanner connected: no "Scanner error".
- [ ] Scan a QR inventory ID, a 4-digit code, and a Data Matrix part label: each opens quantity entry.
- [ ] **Open question:** scan a Code128 label. The boot profile writes `0x00` to the Code128 register
      but its comment says "on". Record whether Code128 is read and whether that is intended. Fix the
      profile or the comment before release.
- [ ] Scan something unsupported (EAN-13): ignored, nothing queued.
- [ ] Scan during a pending result: one scan is kept, a second is reported on serial.
- [ ] Unplug the GM65 and reboot: "Scanner error" `SCN-01` for 2 s, then Ready with the `!` mark and
      "There is a problem". Re-plug, reboot: clears. (Gate 3: scanner-profile failure.)

## 7. Battery and charger

- [ ] Percent tracks the cell within a few percent of a multimeter reading. Record the offset.
- [ ] Plug the charger: card shows "Charging" on Ready only, not over a menu or quantity entry.
- [ ] Red LED lit = Charging. LED dark with the cell at 4.10 V or more = Charged, 100%. LED dark below that =
      on battery. The top bar and card agree.
- [ ] Bounce the charger contact: the card does not re-appear on every flicker.
- [ ] Time left shows a `~` estimate straight away, and the `~` goes once enough load has been observed.
      **Calibrate** the load values (`kBatteryLoad*Ma`), `kBatteryReferenceCapacityUah`, `kBatteryRestDwellMs`,
      `kBatteryPriorChargeMsPerPercent` and `kBatteryFloorMv` from the bench run in the battery plan
      (assumed: 3.7 V nominal LiPo of 1300 mAh, about 80 mA in use, 93.6 s per percent while charging at 500 mA).
      Also calibrate `kBatteryChargedMv` (4.10 V) and `kBatteryChargedReleaseMv` (4.00 V) against a full charge.
- [ ] Charging: the percent rises, never falls, stops at 99 and shows 100 once the LED goes dark with the cell full.
- [ ] Unplug a full cell: it reads Charged until its voltage falls below 4.00 V, then the percent restarts from
      the voltage. This is the known limit of the voltage-only charged state.
- [ ] A WiFi sync or a scan does not move the percent.
- [ ] Low battery (10% or less, not docked): warning counts down 60 s, then powers off. Docking cancels it.
- [ ] Empty (3% or less): "Battery empty" notice, then power off. Boot on a low cell, undocked: refused
      with "Charge to power on".
- [ ] Disconnect the divider: after 4 samples `PWR-01` stands and nothing powers off.

## 8. Power, standby and energy saving

- [ ] Standby: screen off, backlight off, scanner off, Wi-Fi paused; a short `D` hold wakes it.
- [ ] `*` hold power off. After the chip sleeps, **only `*` wakes it** and the firmware restarts.
      Repeat from Ready, from standby, and during setup. Measure sleep current if you can.
- [ ] Energy saving on, sleep 1 min, off 1 min: 30 s countdown, then standby, then a second countdown,
      then power off. A key press or scan cancels a countdown; a key wakes standby (not power-off).
- [ ] A menu, a quantity being typed, an error, a warning, or an update keeps the device awake.
- [ ] Backlight: Off, On, Auto 1 min. Warnings and pairing stay lit in every mode.
- [ ] Settings survive a reboot: contrast, backlight, energy saving, both delays and the battery capacity.
- [ ] Energy: D moves to page 2/2 and back. Battery, then a typed capacity: 99 is refused on A, 2500 saves and
      shows "2500 mAh", # deletes a digit, C leaves without a change. The percent stays the same after the change.
- [ ] Dock mode off (Energy, row 4): on the charger the device stays on with energy saving on, and is not
      put to sleep. Plug in while it is off: it wakes, shows the charging card, and stays on.
- [ ] Dock mode on: plug in, the charging card shows, and the device turns off about 5 s later. Take the charger
      off: it wakes and stays on. Plug in while it is off: it wakes, shows the card, and turns off again.
- [ ] Dock mode on: plug in, the screen goes dark about 5 s later. Take the charger off: it comes back at once.
      The serial log shows "Charger input plugged in" and "Charger input unplugged".
- [ ] A `*` power off while on the charger wakes only with `*`. A plug-in does not wake a device that is off.

## 9. BLE pairing (**destructive**: erases pairing; scans are kept)

- [ ] Re-pair from Device settings, `A` to confirm. Board restarts into setup.
- [ ] Pair with the desktop app: the passkey on the LCD is accepted; Wi-Fi joins; Ready appears.
- [ ] Wrong passkey three times: pairing locks, "Pairing locked"; `A` re-arms with a new passkey.
- [ ] Leave pairing idle for five minutes: it locks.
- [ ] A bonded phone or PC from an earlier session cannot reconnect without the passkey.
- [ ] Wrong Wi-Fi password: "Pairing failed", `PAI-03`.
- [ ] Scan during setup, then finish pairing: no stray scan appears in the first Ready screen (FW-009).

## 10. Sync with the desktop app (gate 4)

- [ ] Scan, add, see "Done" with the item name; the desktop shows the change once.
- [ ] Unknown part, new part, and not-found lookups show the right header and text.
- [ ] Quick labels list loads; printing a label works; printer off gives the right `PRT-` code and text.
- [ ] Disable Wi-Fi: scans queue ("N scans safely queued"); re-enable: queue drains in order, no duplicates.
- [ ] Fill the queue to 16, scan once more: `STO-01` "Sync PC before scanning", nothing is lost.
- [ ] Make the desktop refuse the oldest event: after five failures "Sync blocked" `SYN-01`; menu item 3
      discards it only after `A` (**destructive** to that one scan).
- [ ] **8 KiB contract:** with the released desktop and the largest realistic inventory data, a sync
      response near 8,192 bytes parses; one over the limit is refused and the event stays queued.
- [ ] A result arriving while a settings or confirm screen is open does **not** replace that screen.

## 11. NVS faults and power loss (**destructive**, gate 2)

- [ ] Cut power in the middle of a scan being queued (repeat at least 10 times): after reboot the queue
      holds every scan that showed "safely queued" and no half-written one.
- [ ] Cut power during a sync: the event is still queued, or is gone only if the desktop confirmed it.
      The desktop never shows a scan twice.
- [ ] Reset (not power cut) mid-sync: same result.
- [ ] Corrupt a queue slot (a prepared test image, or erase one key): `STO-03`, menu offers repair;
      repair keeps the readable scans.
- [ ] Reboot mid-counter block: after reboot the desktop accepts requests (counter skips, never reuses).
- [ ] Fill NVS until a write fails (if you can): the scan is **not** shown as queued (`STO-02`).

## 12. OTA (gate 3)

- [ ] `pio run -e waveshare_esp32s3_nano_ota -t upload --upload-port <device host>` succeeds. The
      update screen shows progress, then "Restarting", then Ready on the new version.
- [ ] Wrong password: update fails with `OTA-01` and the device returns to normal.
- [ ] Request an update and send nothing: after 2 minutes `OTA-02` "No update received".
- [ ] Request an update and press `C` while waiting: cancels back to Ready.
- [ ] Interrupt an upload (pull Wi-Fi): `OTA-01`, device still works.
- [ ] Flash a deliberately crashing image over OTA: within 30 s the previous image returns.
- [ ] `*` + `D` hold during setup does nothing.

## 13. Serial log audit (zero sensitive logging)

- [ ] Over a valid scan, an invalid scan, an oversized scan, pairing, and sync, grep the capture:
      no barcode text, no Wi-Fi password, no token, no raw scanner bytes.
- [ ] The only diagnostics are state messages ("Queued protocol v1 inventory event", and so on).

## 14. Device provisioning controls (gate 5)

- [ ] Decide and record the production policy: secure boot, flash encryption, JTAG disable, eFuse state.
      The repository cannot prove these.
- [ ] If fused, record which fuses and how a unit is checked on the line.

## 15. After the board day

- [ ] Fill in the evidence log below. Attach the serial captures and the Unity log.
- [ ] Update the ledger in `release-audit-remediation.md`: change "hardware pending" rows to a
      recorded result, and close each gate you have evidence for.
- [ ] Re-render `docs/screens/*.png` and the website's `scan-r1-lcd.json` with the real display library.
- [ ] Fix every failure above, rerun its section, and rerun section 0.
- [ ] Then the checklist in [releasing.md](releasing.md): secret scan of the whole history, version
      bump, dated changelog, tag, `firmware.bin` with its SHA-256, and the GitHub repository settings.

## Evidence log

| Section | Date | Firmware commit | Result (pass / fail) | Notes, log file, photo |
| --- | --- | --- | --- | --- |
| 0 Builds and host tests | | | | |
| 1 Wiring and power | | | | |
| 2 Flash and boot | | | | |
| 3 Unity suite | | | | |
| 4 LCD pass | | | | |
| 5 Keypad | | | | |
| 6 Scanner | | | | |
| 7 Battery and charger | | | | |
| 8 Power and energy saving | | | | |
| 9 BLE pairing | | | | |
| 10 Sync and 8 KiB contract | | | | |
| 11 NVS faults | | | | |
| 12 OTA | | | | |
| 13 Log audit | | | | |
| 14 Provisioning controls | | | | |
