// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

#pragma once
#include <cstdint>
struct esp_partition_t { uint32_t address; uint32_t size; };
inline const esp_partition_t* esp_ota_get_next_update_partition(const esp_partition_t*) {
  static const esp_partition_t partition{0x1a0000, 0x190000};
  return &partition;
}
