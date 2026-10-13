// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

// Parser for the BLE provisioning write (protocol version 1). Pure logic, so it is
// tested on the host with no BLE stack. Input is untrusted: every length is checked
// before any byte is read, and each field is copied by length, never through a
// C-string read.
#pragma once

#include "InventatoryScanSecurity.h"
#include "setup/setup_state_store.h"

#include <Arduino.h>
#include <cstddef>
#include <cstdint>

namespace inventatory_scan {

constexpr uint8_t kProvisioningProtocolVersion = 1;
constexpr std::size_t kProvisioningMaxSsidBytes = 32;
constexpr std::size_t kProvisioningMaxPasswordBytes = 63;

// Layout: version, SSID length, password length, token length, then the exact bytes
// of the SSID, password and token. Nothing else may follow. A NUL byte in any field is
// refused, because the values are later passed to C APIs that stop at NUL.
inline bool parseProvisioningRequest(const uint8_t* data, std::size_t size, ProvisionedSettings& settings) {
  if (data == nullptr || size < 4U || data[0] != kProvisioningProtocolVersion) return false;
  const std::size_t ssidLength = data[1];
  const std::size_t passwordLength = data[2];
  const std::size_t tokenLength = data[3];
  if (ssidLength == 0U || ssidLength > kProvisioningMaxSsidBytes || passwordLength > kProvisioningMaxPasswordBytes ||
      tokenLength != 64U) {
    return false;
  }
  if (size != 4U + ssidLength + passwordLength + tokenLength) return false;

  const uint8_t* ssid = data + 4U;
  const uint8_t* password = ssid + ssidLength;
  const uint8_t* token = password + passwordLength;
  for (std::size_t index = 4U; index < size; ++index) {
    if (data[index] == 0U) return false;
  }

  ProvisionedSettings parsed;
  for (std::size_t index = 0; index < ssidLength; ++index) parsed.wifiSsid += static_cast<char>(ssid[index]);
  for (std::size_t index = 0; index < passwordLength; ++index) {
    parsed.wifiPassword += static_cast<char>(password[index]);
  }
  for (std::size_t index = 0; index < tokenLength; ++index) parsed.deviceToken += static_cast<char>(token[index]);
  if (!isValidTransportToken(parsed.deviceToken)) return false;

  settings = parsed;
  return true;
}

}  // namespace inventatory_scan
