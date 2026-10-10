// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

// Host stand-in for the ESP32 Preferences (NVS) API.
//
// Namespaces live in process memory, so a new Preferences object sees what an
// earlier one wrote, the same way a reset device sees its flash. Tests use
// host_nvs::faults() to fail writes and to simulate power loss partway through
// a sequence of writes, then host_nvs::reboot() to bring the device back up
// with whatever reached storage.
//
// The name and key limits and the length-query return values follow the NVS
// rules the firmware depends on (see AGENTS.md section 7).
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace host_nvs {

// NVS namespace and key names are limited to 15 characters plus the terminator.
constexpr std::size_t kMaxNameLength = 15;

// Entries the NVS partition can hold. The value is approximate; it only bounds
// freeEntries() so callers that check free space see a sensible number.
constexpr std::size_t kEntryCapacity = 1000;

using Blob = std::vector<std::uint8_t>;
using Namespace = std::map<std::string, Blob>;

struct Faults {
  // Successful writes allowed before power is cut. Negative means never cut.
  // Once the budget is spent, every later write and remove fails.
  long long writesBeforePowerCut = -1;
  // Forces every write and remove to fail, as a full or broken NVS partition would.
  bool failWrites = false;
};

inline std::map<std::string, Namespace>& storage() {
  static std::map<std::string, Namespace> persisted;
  return persisted;
}

inline Faults& faults() {
  static Faults current;
  return current;
}

// Wipes storage and faults, as a freshly flashed device would start.
inline void reset() {
  storage().clear();
  faults() = Faults{};
}

// Clears the injected faults and keeps storage: the device rebooted.
inline void reboot() { faults() = Faults{}; }

// Consumes one write from the power budget. Returns false when the write must fail.
inline bool allowWrite() {
  Faults& current = faults();
  if (current.failWrites) return false;
  if (current.writesBeforePowerCut == 0) return false;
  if (current.writesBeforePowerCut > 0) --current.writesBeforePowerCut;
  return true;
}

inline bool validName(const char* name) {
  return name != nullptr && name[0] != '\0' && std::strlen(name) <= kMaxNameLength;
}

}  // namespace host_nvs

class Preferences {
 public:
  bool begin(const char* name, bool readOnly = false) {
    end();
    if (!host_nvs::validName(name)) return false;
    if (readOnly && host_nvs::storage().count(name) == 0) return false;
    name_ = name;
    readOnly_ = readOnly;
    open_ = true;
    return true;
  }

  void end() {
    open_ = false;
    name_.clear();
  }

  bool clear() {
    if (!writable() || !host_nvs::allowWrite()) return false;
    host_nvs::storage()[name_].clear();
    return true;
  }

  bool remove(const char* key) {
    if (!writable() || !validKey(key) || !host_nvs::allowWrite()) return false;
    return host_nvs::storage()[name_].erase(key) > 0;
  }

  bool isKey(const char* key) const {
    return open_ && validKey(key) && find(key) != nullptr;
  }

  // Entries left in the namespace budget, as the NVS partition reports them.
  std::size_t freeEntries() const {
    std::size_t used = 0;
    const auto space = host_nvs::storage().find(name_);
    if (space != host_nvs::storage().end()) used = space->second.size();
    return used >= host_nvs::kEntryCapacity ? 0 : host_nvs::kEntryCapacity - used;
  }

  std::size_t getBytesLength(const char* key) const {
    const auto* blob = open_ && validKey(key) ? find(key) : nullptr;
    return blob != nullptr ? blob->size() : 0;
  }

  // Returns the number of bytes written, or 0 when the write failed.
  std::size_t putBytes(const char* key, const void* value, std::size_t length) {
    if (!writable() || !validKey(key) || value == nullptr || !host_nvs::allowWrite()) return 0;
    const auto* bytes = static_cast<const std::uint8_t*>(value);
    host_nvs::storage()[name_][key].assign(bytes, bytes + length);
    return length;
  }

  // Returns the number of bytes read, or 0 when the key is missing or the
  // stored value does not fit in maxLength.
  std::size_t getBytes(const char* key, void* buffer, std::size_t maxLength) const {
    const auto* blob = open_ && validKey(key) ? find(key) : nullptr;
    if (blob == nullptr || blob->size() > maxLength || buffer == nullptr) return 0;
    if (!blob->empty()) std::memcpy(buffer, blob->data(), blob->size());
    return blob->size();
  }

 private:
  static bool validKey(const char* key) {
    return key != nullptr && key[0] != '\0' && std::strlen(key) <= host_nvs::kMaxNameLength;
  }

  bool writable() const { return open_ && !readOnly_; }

  const host_nvs::Blob* find(const char* key) const {
    const auto space = host_nvs::storage().find(name_);
    if (space == host_nvs::storage().end()) return nullptr;
    const auto entry = space->second.find(key);
    return entry == space->second.end() ? nullptr : &entry->second;
  }

  std::string name_;
  bool open_ = false;
  bool readOnly_ = false;
};
