# Error-code screens

One screen per error code on the Scan R1 LCD. The images are drawn from the firmware's display
code (U8g2, ST7565, 128 x 64 pixels) on the host and scaled three times, black on white. They are
not photographs of the panel.

| File | Code | Screen |
| --- | --- | --- |
| `SCN-01-boot.png` | SCN-01 | Start-up: "Scanner error" |
| `SCN-01-ready.png` | SCN-01 | Main screen: "There is a problem" |
| `SCN-01-fault-detail.png` | SCN-01 | Fault info |
| `SCN-01-after-standby.png` | SCN-01 | Result: "Check scanner cable" |
| `STO-01-queue-full.png` | STO-01 | Result: "Sync PC before scanning" |
| `STO-02-scan-not-saved.png` | STO-02 | Result: "Scan storage write failed" |
| `STO-03-ready.png` | STO-03 | Main screen: "There is a problem" |
| `STO-03-menu.png` | STO-03 | Menu: "3 Repair scan queue" |
| `STO-03-fault-detail.png` | STO-03 | Fault info |
| `STO-04-repair-failed.png` | STO-04 | Result: "Could not repair queue" |
| `STO-05-discard-failed.png` | STO-05 | Result: "Could not discard scan" |
| `PAI-01-pairing-locked.png` | PAI-01 | "Pairing locked" |
| `PAI-02-save-failed.png` | PAI-02 | "Could not save setup" |
| `PAI-03-wifi-failed.png` | PAI-03 | "Pairing failed": "Check Wi-Fi details" |
| `PAI-04-clear-failed.png` | PAI-04 | Result: "Could not clear pairing" |
| `SYS-01-network-error.png` | SYS-01 | "Pairing failed": "Try pairing again" |
| `SYS-02-bluetooth-error.png` | SYS-02 | Start-up: "Bluetooth error" |
| `OTA-01-update-failed.png` | OTA-01 | Result: "Update failed" |
| `OTA-02-no-update.png` | OTA-02 | Result: "No update received" |
| `PRT-01-no-reply.png` | PRT-01 | "Print failed": "PC OR PRINTER OFFLINE" |
| `PRT-02-pc-offline.png` | PRT-02 | "Print failed": "PC OFFLINE" |
| `PRT-03-presets-not-loaded.png` | PRT-03 | "Print failed": "REFRESH PRESETS FIRST" |
| `PWR-01-standing-print-failed.png` | PRT-02 | "Print failed" with the top-bar mark, while the battery problem PWR-01 stands |

## Not yet re-rendered

The SCN-02, SYN-01 and PWR-01 screens need new renders. The older renders still showed the codes
from before the renumbering (SCN-03, SYN-03, PWR-03), so they were moved out of this folder. The
missing screens are the scan-rejected result (SCN-02), the sync-blocked main and fault screens
(SYN-01), and the battery-not-read main and fault screens (PWR-01).
