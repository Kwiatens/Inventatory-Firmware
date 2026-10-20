// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

namespace inventatory_scan {

// LCD error codes; public reference at kwiatens.github.io/Inventatory-Site/docs/scanner/error-codes/.
// Format AAA-NN, numbered 01, 02, ... with no gap per area. To remove a code, renumber its area
// and update the public reference, AGENTS.md and CHANGELOG.md. Renumbering is safe only before
// the first hardware release.

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

// Battery. Low or empty is a warning with no code; only an untrustworthy reading has one.
constexpr char kBatteryUnreadableCode[] = "PWR-01";

}  // namespace inventatory_scan
