// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <cstdint>

namespace inventatory_scan {

#define INVENTATORY_FIRMWARE_VERSION "0.1.0"

#define INVENTATORY_FIRMWARE_PRODUCT "Inventatory-Scanner-R1"
#define INVENTATORY_FIRMWARE_HARDWARE "waveshare-esp32s3-nano"

// Raise this only when a release fixes a security vulnerability
#define INVENTATORY_FIRMWARE_SECURITY_COUNTER 1

#define INVENTATORY_FIRMWARE_TEXT_(value) #value
#define INVENTATORY_FIRMWARE_TEXT(value) INVENTATORY_FIRMWARE_TEXT_(value)

#define INVENTATORY_FIRMWARE_IDENTITY                                                              \
  "IVFWID1;version=" INVENTATORY_FIRMWARE_VERSION ";product=" INVENTATORY_FIRMWARE_PRODUCT     \
  ";hardware=" INVENTATORY_FIRMWARE_HARDWARE                                                   \
  ";counter=" INVENTATORY_FIRMWARE_TEXT(INVENTATORY_FIRMWARE_SECURITY_COUNTER) ";"

constexpr char kFirmwareVersion[] = INVENTATORY_FIRMWARE_VERSION;
constexpr char kFirmwareProduct[] = INVENTATORY_FIRMWARE_PRODUCT;
constexpr char kFirmwareHardware[] = INVENTATORY_FIRMWARE_HARDWARE;
constexpr uint32_t kFirmwareSecurityCounter = INVENTATORY_FIRMWARE_SECURITY_COUNTER;

}  // namespace inventatory_scan
