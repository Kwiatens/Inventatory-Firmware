#pragma once

#include <Arduino.h>
#include <Preferences.h>

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

  bool begin(const char* namespaceName = "inventatory_scan", const char* deviceId = nullptr);
  void end();
  bool load();
  bool save();

  bool enqueue(DeviceEventKind kind, const String& code, int value, DeviceEventRecord* created = nullptr,
               const String& manufacturer = String(), const String& manufacturerPartNumber = String(),
               const String& encodedPartName = String());
  bool peek(std::size_t offset, DeviceEventRecord& event) const;
  bool pop(DeviceEventRecord* event = nullptr);
  void clear();

  std::size_t size() const { return count_; }
  bool empty() const { return count_ == 0; }
  bool full() const { return count_ == kCapacity; }
  uint32_t nextSequence() const { return nextSequence_; }

 private:
  static constexpr uint32_t kMagic = 0x48534f51UL;
  static constexpr uint16_t kVersion = 4;
  static constexpr std::size_t kCodeLen = 65;  // 64-character MPN plus null terminator.
  static constexpr std::size_t kManufacturerLen = 48;
  static constexpr std::size_t kNameLen = 64;

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

  struct PersistedState {
    uint32_t magic = kMagic;
    uint16_t version = kVersion;
    uint16_t count = 0;
    uint16_t head = 0;
    uint16_t reserved = 0;
    uint32_t nextSequence = 1;
    Record records[kCapacity]{};
  };

  static_assert(std::is_trivially_copyable<Record>::value, "Record must be trivially copyable");
  static_assert(std::is_trivially_copyable<PersistedState>::value, "State must be trivially copyable");

  static void copyString(char* dest, std::size_t capacity, const String& value);
  static String readString(const char* value);
  DeviceEventRecord fromRecord(const Record& record) const;
  void syncFromState();
  void syncToState();
  bool migrateVersion2();

  Preferences prefs_;
  String deviceId_;
  PersistedState state_{};
  std::size_t count_ = 0;
  std::size_t head_ = 0;
  std::size_t tail_ = 0;
  uint32_t nextSequence_ = 1;
};

}  // namespace inventatory_scan
