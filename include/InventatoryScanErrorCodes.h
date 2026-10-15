// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

namespace inventatory_scan {

// The codes the LCD shows for a problem. The public reference explains each one at
// kwiatens.github.io/Inventatory-Site/docs/scanner/error-codes/.
//
// Format: three letters for the area, a dash, two digits.
//
// Inside each area the numbers run 01, 02, ... with no gap, in this order. Only a code the
// device can show is listed. To remove one, delete it, renumber the rest of its area, and update
// the public reference, AGENTS.md and CHANGELOG.md. The host test fails on a gap.
// tools/check_error_codes.py also fails on a code that no firmware file uses, on a code missing
// from the host test list, and on a public reference that differs. No device in the field shows
// these codes yet, so renumbering is safe until the first hardware release. After that, ask first.

// Scanner.
constexpr char kScannerFaultCode[] = "SCN-01";
constexpr char kScanRejectedCode[] = "SCN-02";

// Offline queue and storage.
constexpr char kQueueFullCode[] = "STO-01";
constexpr char kScanNotSavedCode[] = "STO-02";
constexpr char kQueueUnreadableCode[] = "STO-03";
constexpr char kRepairFailedCode[] = "STO-04";
constexpr char kDiscardFailedCode[] = "STO-05";

// Sync with the PC.
constexpr char kSyncBlockedCode[] = "SYN-01";

// Pairing and Wi-Fi setup.
constexpr char kPairingLockedCode[] = "PAI-01";
constexpr char kSetupNotSavedCode[] = "PAI-02";
constexpr char kWifiNotJoinedCode[] = "PAI-03";
constexpr char kPairingClearFailedCode[] = "PAI-04";

// Start-up.
constexpr char kNetworkStartFailedCode[] = "SYS-01";
constexpr char kBluetoothStartFailedCode[] = "SYS-02";

// Firmware update over Wi-Fi.
constexpr char kUpdateFailedCode[] = "OTA-01";
constexpr char kNoUpdateCode[] = "OTA-02";

// Quick-label printing.
constexpr char kPrintNoReplyCode[] = "PRT-01";
constexpr char kPrintPcOfflineCode[] = "PRT-02";
constexpr char kPresetsNotLoadedCode[] = "PRT-03";

// Battery. A low or empty battery is a warning, not a fault, so it has no code. Only a reading
// that cannot be trusted does.
constexpr char kBatteryUnreadableCode[] = "PWR-01";

}  // namespace inventatory_scan
