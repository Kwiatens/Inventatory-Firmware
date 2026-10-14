// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

namespace inventatory_scan {

// The codes the LCD shows for a problem. The public reference explains each one at
// kwiatens.github.io/Inventatory-Site/docs/scanner/error-codes/. A code is part of that
// reference, so never renumber one or reuse its number. Retire it and take the next free number.
//
// Format: three letters for the area, a dash, two digits.

// Scanner.
constexpr char kScannerFaultCode[] = "SCN-01";
constexpr char kBarcodeNotRecognisedCode[] = "SCN-02";
constexpr char kScanRejectedCode[] = "SCN-03";

// Offline queue and storage.
constexpr char kQueueFullCode[] = "STO-01";
constexpr char kScanNotSavedCode[] = "STO-02";
constexpr char kQueueUnreadableCode[] = "STO-03";
constexpr char kRepairFailedCode[] = "STO-04";
constexpr char kDiscardFailedCode[] = "STO-05";

// Sync with the PC.
constexpr char kPcTooOldCode[] = "SYN-01";
constexpr char kDeviceRejectedCode[] = "SYN-02";
constexpr char kSyncBlockedCode[] = "SYN-03";

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

// Battery.
constexpr char kBatteryLowCode[] = "PWR-01";
constexpr char kBatteryEmptyCode[] = "PWR-02";
constexpr char kBatteryUnreadableCode[] = "PWR-03";

}  // namespace inventatory_scan
