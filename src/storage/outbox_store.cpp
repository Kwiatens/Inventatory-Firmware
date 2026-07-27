#include "storage/outbox_store.h"

#include "InventatoryScanCore.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace inventatory_scan {

namespace {

constexpr char kHeaderKey[] = "hdr";
constexpr char kLegacyStateKey[] = "state";

constexpr std::size_t kLegacyCodeLen = 96;
constexpr std::size_t kLegacyV4CodeLen = 65;
constexpr std::size_t kLegacyV4ManufacturerLen = 48;
constexpr std::size_t kLegacyV4NameLen = 64;

struct LegacyRecord {
  uint32_t sequence = 0;
  char code[kLegacyCodeLen]{};
  int32_t delta = 0;
};

struct LegacyState {
  uint32_t magic = 0;
  uint16_t version = 0;
  uint16_t count = 0;
  uint16_t head = 0;
  uint16_t reserved = 0;
  uint32_t nextSequence = 1;
  LegacyRecord records[OutboxStore::kCapacity]{};
};

// Version 4 kept the whole queue in one 4112-byte blob.
struct LegacyRecordV4 {
  uint32_t sequence = 0;
  int32_t value = 0;
  uint8_t kind = 0;
  uint8_t reserved[3]{};
  char code[kLegacyV4CodeLen]{};
  char manufacturer[kLegacyV4ManufacturerLen]{};
  char manufacturerPartNumber[kLegacyV4CodeLen]{};
  char encodedPartName[kLegacyV4NameLen]{};
};

struct LegacyStateV4 {
  uint32_t magic = 0;
  uint16_t version = 0;
  uint16_t count = 0;
  uint16_t head = 0;
  uint16_t reserved = 0;
  uint32_t nextSequence = 1;
  LegacyRecordV4 records[OutboxStore::kCapacity]{};
};

}  // namespace

bool OutboxStore::begin(const char* namespaceName, const char* deviceId) {
  namespaceName_ = namespaceName == nullptr ? String() : String(namespaceName);
  deviceId_ = deviceId == nullptr ? String() : String(deviceId);
  storageReady_ = false;
  return ensureStorage();
}

bool OutboxStore::ensureStorage() {
  // A namespace that could not be opened at boot is retried, so a scan is not
  // refused for the rest of the session because of one transient NVS failure.
  if (storageReady_) return true;
  if (namespaceName_.length() == 0 || namespaceName_.length() > kMaxNamespaceLength) return false;
  storageReady_ = prefs_.begin(namespaceName_.c_str(), false);
  return storageReady_;
}

void OutboxStore::end() {
  prefs_.end();
  storageReady_ = false;
}

void OutboxStore::copyString(char* dest, std::size_t capacity, const String& value) {
  if (capacity == 0) return;
  const auto trimmed = trimCopy(value);
  const auto limit = std::min(capacity - 1U, static_cast<std::size_t>(trimmed.length()));
  memcpy(dest, trimmed.c_str(), limit);
  dest[limit] = '\0';
}

String OutboxStore::readString(const char* value) { return value == nullptr ? String() : String(value); }

void OutboxStore::slotKey(std::size_t slot, char (&out)[8]) {
  snprintf(out, sizeof(out), "e%u", static_cast<unsigned>(slot));
}

DeviceEventRecord OutboxStore::fromRecord(const Record& record) const {
  DeviceEventRecord event;
  event.sequence = record.sequence;
  event.eventId = makeRequestId(deviceId_, record.sequence);
  event.kind = static_cast<DeviceEventKind>(record.kind);
  event.code = readString(record.code);
  event.manufacturer = readString(record.manufacturer);
  event.manufacturerPartNumber = readString(record.manufacturerPartNumber);
  event.encodedPartName = readString(record.encodedPartName);
  event.value = record.value;
  return event;
}

bool OutboxStore::writeHeader() {
  Header header;
  header.magic = kMagic;
  header.version = kVersion;
  header.count = static_cast<uint16_t>(count_);
  header.head = static_cast<uint16_t>(head_);
  header.nextSequence = nextSequence_;
  return prefs_.putBytes(kHeaderKey, &header, sizeof(header)) == sizeof(header);
}

bool OutboxStore::writeSlot(std::size_t slot) {
  char key[8];
  slotKey(slot, key);
  return prefs_.putBytes(key, &records_[slot], sizeof(Record)) == sizeof(Record);
}

bool OutboxStore::readSlot(std::size_t slot) {
  char key[8];
  slotKey(slot, key);
  if (prefs_.getBytesLength(key) != sizeof(Record)) return false;
  return prefs_.getBytes(key, &records_[slot], sizeof(Record)) == sizeof(Record);
}

void OutboxStore::eraseSlot(std::size_t slot) {
  char key[8];
  slotKey(slot, key);
  if (prefs_.isKey(key)) prefs_.remove(key);
}

bool OutboxStore::loadCurrentLayout() {
  if (prefs_.getBytesLength(kHeaderKey) != sizeof(Header)) return false;
  Header header{};
  if (prefs_.getBytes(kHeaderKey, &header, sizeof(header)) != sizeof(header) || header.magic != kMagic ||
      header.version != kVersion) {
    return false;
  }

  count_ = std::min<std::size_t>(header.count, kCapacity);
  head_ = std::min<std::size_t>(header.head, kCapacity - 1U);
  tail_ = (head_ + count_) % kCapacity;
  nextSequence_ = header.nextSequence == 0 ? 1U : header.nextSequence;
  for (std::size_t offset = 0; offset < count_; ++offset) {
    if (!readSlot((head_ + offset) % kCapacity)) {
      // A queued event lost its record; treat the whole queue as unreadable
      // rather than syncing an event with blank fields.
      clear();
      return false;
    }
  }
  return true;
}

bool OutboxStore::persistQueue() {
  for (std::size_t offset = 0; offset < count_; ++offset) {
    if (!writeSlot((head_ + offset) % kCapacity)) return false;
  }
  return writeHeader();
}

bool OutboxStore::rebuildStorage() {
  // Last resort when NVS refuses a write. The whole queue is mirrored in RAM,
  // so the namespace can be erased and written again from scratch. This is what
  // reclaims the old monolithic blob and any orphaned slot on a partition that
  // is too full to hold the old and the new layout side by side.
  if (!prefs_.clear()) return false;
  return persistQueue();
}

bool OutboxStore::adoptMigratedRecords(std::size_t count, uint32_t nextSequence) {
  count_ = count;
  head_ = 0;
  tail_ = count_ % kCapacity;
  nextSequence_ = nextSequence == 0 ? 1U : nextSequence;
  if (persistQueue()) {
    // The old monolithic blob is dropped only once the new layout is durable.
    prefs_.remove(kLegacyStateKey);
    return true;
  }
  if (rebuildStorage()) return true;

  clear();
  return false;
}

OutboxStore::MigrationResult OutboxStore::migrateLegacyState() {
  const auto storedLength = prefs_.getBytesLength(kLegacyStateKey);
  if (storedLength != sizeof(LegacyState) && storedLength != sizeof(LegacyStateV4)) {
    return MigrationResult::NotRecognized;
  }

  // The version-4 blob is 4 KiB, so it is read through the heap rather than the
  // task stack.
  const auto buffer = static_cast<uint8_t*>(malloc(storedLength));
  if (buffer == nullptr) return MigrationResult::WriteFailed;
  const bool read = prefs_.getBytes(kLegacyStateKey, buffer, storedLength) == storedLength;

  auto outcome = MigrationResult::NotRecognized;
  if (read && storedLength == sizeof(LegacyStateV4)) {
    const auto* legacy = reinterpret_cast<const LegacyStateV4*>(buffer);
    if (legacy->magic == kMagic && legacy->version == 4) {
      memset(records_, 0, sizeof(records_));
      const auto count = std::min<std::size_t>(legacy->count, kCapacity);
      const auto legacyHead = std::min<std::size_t>(legacy->head, kCapacity - 1U);
      for (std::size_t offset = 0; offset < count; ++offset) {
        const auto& source = legacy->records[(legacyHead + offset) % kCapacity];
        auto& record = records_[offset];
        record.sequence = source.sequence;
        record.value = source.value;
        record.kind = source.kind;
        copyString(record.code, sizeof(record.code), String(source.code));
        copyString(record.manufacturer, sizeof(record.manufacturer), String(source.manufacturer));
        copyString(record.manufacturerPartNumber, sizeof(record.manufacturerPartNumber),
                   String(source.manufacturerPartNumber));
        copyString(record.encodedPartName, sizeof(record.encodedPartName), String(source.encodedPartName));
      }
      outcome = adoptMigratedRecords(count, legacy->nextSequence) ? MigrationResult::Migrated
                                                                 : MigrationResult::WriteFailed;
    }
  } else if (read) {
    const auto* legacy = reinterpret_cast<const LegacyState*>(buffer);
    if (legacy->magic == kMagic && legacy->version == 2) {
      memset(records_, 0, sizeof(records_));
      const auto count = std::min<std::size_t>(legacy->count, kCapacity);
      const auto legacyHead = std::min<std::size_t>(legacy->head, kCapacity - 1U);
      for (std::size_t offset = 0; offset < count; ++offset) {
        const auto& source = legacy->records[(legacyHead + offset) % kCapacity];
        auto& record = records_[offset];
        record.sequence = source.sequence;
        record.value = source.delta;
        record.kind = static_cast<uint8_t>(DeviceEventKind::InventoryAdjust);
        copyString(record.code, sizeof(record.code), String(source.code));
      }
      outcome = adoptMigratedRecords(count, legacy->nextSequence) ? MigrationResult::Migrated
                                                                 : MigrationResult::WriteFailed;
    }
  }

  free(buffer);
  return outcome;
}

bool OutboxStore::load() {
  if (!storageReady_) {
    clear();
    return false;
  }
  if (loadCurrentLayout()) return true;
  if (prefs_.isKey(kLegacyStateKey)) {
    const auto migration = migrateLegacyState();
    if (migration == MigrationResult::Migrated) return true;
    if (migration == MigrationResult::WriteFailed) {
      // The old queue is still readable, so it is kept for the next boot rather
      // than discarded because storage was momentarily unwritable.
      clear();
      return false;
    }
  }

  clear();
  if (!prefs_.clear()) return false;
  return writeHeader();
}

OutboxStore::EnqueueResult OutboxStore::enqueue(DeviceEventKind kind, const String& code, int value,
                                                DeviceEventRecord* created, const String& manufacturer,
                                                const String& manufacturerPartNumber,
                                                const String& encodedPartName) {
  if (full()) return EnqueueResult::QueueFull;
  const auto trimmed = trimCopy(code);
  if (trimmed.length() == 0 || trimmed.length() >= kCodeLen || value == 0) return EnqueueResult::Rejected;
  if (!ensureStorage()) return EnqueueResult::StorageFailed;

  const auto slot = tail_;
  const auto previous = records_[slot];
  Record record{};
  record.sequence = nextSequence_;
  record.value = value;
  record.kind = static_cast<uint8_t>(kind);
  copyString(record.code, sizeof(record.code), trimmed);
  copyString(record.manufacturer, sizeof(record.manufacturer), manufacturer);
  copyString(record.manufacturerPartNumber, sizeof(record.manufacturerPartNumber), manufacturerPartNumber);
  copyString(record.encodedPartName, sizeof(record.encodedPartName), encodedPartName);
  records_[slot] = record;
  const auto previousTail = tail_;
  tail_ = (tail_ + 1U) % kCapacity;
  ++count_;
  ++nextSequence_;

  // The record is stored before the header so a power cut can only leave an
  // unreferenced slot behind, never a queued event without its payload.
  if (!(writeSlot(slot) && writeHeader()) && !rebuildStorage()) {
    tail_ = previousTail;
    --count_;
    --nextSequence_;
    records_[slot] = previous;
    return EnqueueResult::StorageFailed;
  }

  if (created != nullptr) *created = fromRecord(record);
  return EnqueueResult::Queued;
}

bool OutboxStore::peek(std::size_t offset, DeviceEventRecord& event) const {
  if (offset >= count_) return false;
  event = fromRecord(records_[(head_ + offset) % kCapacity]);
  return true;
}

bool OutboxStore::pop(DeviceEventRecord* event) {
  if (empty()) return false;
  const auto slot = head_;
  if (event != nullptr) *event = fromRecord(records_[slot]);
  head_ = (head_ + 1U) % kCapacity;
  --count_;
  if (count_ == 0) head_ = tail_ = 0;
  // The header is committed first: a power cut between the two writes leaves an
  // acknowledged event's payload behind, which is harmless, whereas the reverse
  // order could resend it.
  const bool persisted = writeHeader();
  if (storageReady_) eraseSlot(slot);
  return persisted;
}

void OutboxStore::clear() {
  memset(records_, 0, sizeof(records_));
  count_ = head_ = tail_ = 0;
  nextSequence_ = 1;
}

}  // namespace inventatory_scan
