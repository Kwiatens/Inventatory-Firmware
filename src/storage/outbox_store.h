// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <Arduino.h>
#include <Preferences.h>

#include "InventatoryScanOutbox.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace inventatory_scan {

// Retained for the unversioned client helper used by older integration code.
struct QuantityRequest {
  String deviceId;
  String requestId;
  uint32_t sequence = 0;
  String code;
  int delta = 0;
};

enum class DeviceEventKind : uint8_t {
  InventoryAdjust = 1,
  InventoryReceive = 2,
};

struct DeviceEventRecord {
  String eventId;
  uint32_t sequence = 0;
  DeviceEventKind kind = DeviceEventKind::InventoryAdjust;
  String code;
  String manufacturer;
  String manufacturerPartNumber;
  String encodedPartName;
  int value = 0;
};

class OutboxStore {
 public:
  static constexpr std::size_t kCapacity = 16;

  enum class EnqueueResult : uint8_t {
    Queued,
    QueueFull,       // The queue holds kCapacity events; the PC must sync first.
    Rejected,        // The event itself cannot be represented (empty/oversized code, zero value).
    StorageFailed,   // NVS refused the write; the event was not persisted.
  };

  // ESP32 NVS namespaces are limited to 15 characters plus the terminator. A
  // longer name makes nvs_open fail with KEY_TOO_LONG, and nothing can be saved.
  static constexpr std::size_t kMaxNamespaceLength = 15;

  bool begin(const char* namespaceName = "inv_outbox", const char* deviceId = nullptr);
  void end();
  bool load();

  EnqueueResult enqueue(DeviceEventKind kind, const String& code, int value, DeviceEventRecord* created = nullptr,
                        const String& manufacturer = String(),
                        const String& manufacturerPartNumber = String(),
                        const String& encodedPartName = String());
  bool peek(std::size_t offset, DeviceEventRecord& event) const;
  bool pop(DeviceEventRecord* event = nullptr);
  void clear();

  std::size_t size() const { return count_; }
  bool empty() const { return count_ == 0; }
  bool full() const { return count_ == kCapacity; }
  bool storageAvailable() const { return storageReady_; }
  std::size_t freeStorageEntries() { return storageReady_ ? prefs_.freeEntries() : 0U; }
  uint32_t nextSequence() const { return nextSequence_; }

 private:
  static constexpr uint32_t kMagic = 0x48534f51UL;
  static constexpr uint16_t kVersion = 5;
  static constexpr std::size_t kCodeLen = 65;  // 64-character MPN plus null terminator.
  static constexpr std::size_t kManufacturerLen = 48;
  static constexpr std::size_t kNameLen = 64;

  // Each queued event is persisted under its own NVS key. A single 4 KiB blob
  // for the whole queue exceeded the NVS single-page chunk size, so every scan
  // rewrote a multi-page blob and needed room for two copies at once inside the
  // 20 KiB nvs partition. Per-record keys keep each write to one small chunk.
  struct Record {
    uint32_t sequence = 0;
    int32_t value = 0;
    uint8_t kind = 0;
    uint8_t reserved[3]{};
    char code[kCodeLen]{};
    char manufacturer[kManufacturerLen]{};
    char manufacturerPartNumber[kCodeLen]{};
    char encodedPartName[kNameLen]{};
  };

  struct Header {
    uint32_t magic = kMagic;
    uint16_t version = kVersion;
    uint16_t count = 0;
    uint16_t head = 0;
    uint16_t reserved = 0;
    uint32_t nextSequence = 1;
  };

  static_assert(std::is_trivially_copyable<Record>::value, "Record must be trivially copyable");
  static_assert(std::is_trivially_copyable<Header>::value, "Header must be trivially copyable");
  static_assert(sizeof(Record) <= 512U, "Record must stay inside a single NVS blob chunk");

  static void copyString(char* dest, std::size_t capacity, const String& value);
  static String readString(const char* value);
  static void slotKey(std::size_t slot, char (&out)[8]);

  DeviceEventRecord fromRecord(const Record& record) const;
  bool writeHeader(const OutboxQueueState& state);
  bool writeSlot(std::size_t slot);
  bool writeSlot(std::size_t slot, const Record& record);
  bool readSlot(std::size_t slot);
  void eraseSlot(std::size_t slot);
  enum class MigrationResult : uint8_t { Migrated, NotRecognized, WriteFailed };

  bool ensureStorage();
  bool loadCurrentLayout();
  bool persistQueue();
  MigrationResult migrateLegacyState();
  bool adoptMigratedRecords(std::size_t count, uint32_t nextSequence);

  Preferences prefs_;
  String namespaceName_;
  String deviceId_;
  bool storageReady_ = false;
  bool loaded_ = false;
  Record records_[kCapacity]{};
  std::size_t count_ = 0;
  std::size_t head_ = 0;
  std::size_t tail_ = 0;
  uint32_t nextSequence_ = 1;
};

}  // namespace inventatory_scan
