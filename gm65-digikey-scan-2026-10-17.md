# GM65 DigiKey scan: handover

- Slug: gm65-digikey-scan-2026-10-17
- Owner: Claude (interactive session with the owner)
- Status: active
- Last updated: 2026-10-17 14:34
- Branch: main (from origin/main f6b637f; commits 3c58f68 and d362de4)
- Worktree: /home/pawel/Dokumenty/Github/Inventatory-Firmware (primary checkout)

## Files claimed
- src/gm65/gm65_scanner.cpp, src/gm65/gm65_scanner.h
- include/InventatoryScanGm65Frame.h
- src/app/inventatory_scan_app.h, src/app/inventatory_scan_app_input.cpp
- test/test_gm65_frame.cpp, test/test_inventatory_scan_core.cpp
- CHANGELOG.md

## Done
- SCN-01 at every start-up: the module acknowledges the save command after about 340 ms, and the firmware waited 150 ms. The save wait is now 1 s (3c58f68). The owner confirmed on the board: no SCN-01.
- A DigiKey Data Matrix arrives with no CR or LF. It ends with RS EOT RS EOT. A frame now ends at CR, LF or EOT, and a frame made only of separators is dropped (3c58f68).
- Each refused scan shows its own reason (d362de4).
- Host replay of a captured DigiKey bag (scratch check, not in the repo): one frame, DigiKey part number IRLML6344TRPBFCT-ND, manufacturer part number IRLML6344TRPBF, quantity 3, bag code 46 bytes.

## Left
- On the board, the DigiKey scan still shows "Scan not recognised". The serial log for that scan was not captured: the monitor stopped at about 13:14, and the scan came later.
- Hypothesis, not verified: poll() stamps each byte with millis() at read time, and a partial frame is dropped after 30 ms idle (kGm65FrameIdleTimeoutMs). If the main loop stalls for more than 30 ms during a scan, the frame splits. The first part is dropped, and the rest is reported at the EOT as an unrecognised scan. To check, capture the monitor log during a scan. Fix options: a longer idle timeout, or measuring the loop stall.
- TME labels and inventory QR codes: their terminators are not checked on this firmware. The old firmware read QR codes, so they probably end in CR or LF.
- The hard power-off question from the owner's first message is not examined. Neither symptom came from it.

## Overlaps found (and what the user was told)
- 0582f53 and f6b637f touch src/app/inventatory_scan_app_input.cpp. They are the owner's vendor-bag work, which these commits continue. No active registry entry.

## Notes for the next agent
- The board runs the firmware built from d362de4. It was in standby (energy saving) at handover; hold D to wake.
- The serial monitor is the scratch script monitor_any.py. It follows /dev/ttyACM* and reconnects. Do not run it while uploading.
- A probe that prints the raw scanner bytes is in the session scratch directory, not in the repo. It may be gone. It holds the ACK-timing probe and the raw listen loop.
- Firmware must not log barcode contents (AGENTS.md invariant 1). Raw bytes belong in a scratch probe only.
- Flash with a plain upload: pio run -e waveshare_esp32s3_nano_usb -t upload --upload-port /dev/ttyACM0. Do not erase flash, which holds the stored Wi-Fi and pairing data. The PlatformIO Python needs PYTHONPATH=$HOME/.platformio/penv/lib/python3.13/site-packages.
