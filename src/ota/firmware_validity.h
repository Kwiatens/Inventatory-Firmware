// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

// OTA rollback: a new image stays pending verification until it is confirmed. A reset before that,
// a crash or a deep sleep alike, returns the device to the previous image.
#pragma once

#include <cstdint>

namespace inventatory_scan {

// Confirms the running image if it is still pending verification; does nothing otherwise. Called after
// 30 s of healthy running, and before a deliberate power off or restart once the app loop has run, so
// powering off soon after an update does not quietly undo it.
void confirmRunningFirmware(const char* reason);

// The lowest security counter a signed update may carry: the running image's own counter, or the larger
// one stored once an update was confirmed. An image that rolled back never raises it.
uint32_t minimumFirmwareSecurityCounter();

// Logs once per boot when an earlier update was rolled back, so a reverted update is not silent.
void reportFirmwareRollback();

}  // namespace inventatory_scan
