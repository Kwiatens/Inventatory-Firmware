// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

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

// Version 2 and 4 layouts keep the shared header fields at the same offsets, so their count and
// sequence can be read.
static_assert(offsetof(LegacyState, version) == 4 && offsetof(LegacyState, count) == 6 &&
                  offsetof(LegacyState, head) == 8 && offsetof(LegacyState, nextSequence) == 12,
              "the version 2 layout must keep the shared header fields at their offsets");
static_assert(offsetof(LegacyStateV4, version) == 4 && offsetof(LegacyStateV4, count) == 6 &&
                  offsetof(LegacyStateV4, head) == 8 && offsetof(LegacyStateV4, nextSequence) == 12,
              "the version 4 layout must keep the shared header fields at their offsets");

// Legacy char arrays may be full with no NUL; read at most `capacity` bytes so the copy cannot overrun.
String boundedString(const char* data, std::size_t capacity) {
  if (data == nullptr) return String();
  std::size_t length = 0;
  while (length < capacity && data[length] != '\0') ++length;
  String value;
  value.reserve(length);
  for (std::size_t index = 0; index < length; ++index) value += data[index];
  return value;
}

}  // namespace

bool OutboxStore::begin(const char* namespaceName, const char* deviceId, uint32_t freshSequenceStart) {
  namespaceName_ = namespaceName == nullptr ? String() : String(namespaceName);
  deviceId_ = deviceId == nullptr ? String() : String(deviceId);
  freshSequenceStart_ = freshSequenceStart == 0 ? 1U : freshSequenceStart;
  storageReady_ = false;
  loaded_ = false;
  return ensureStorage();
}

bool OutboxStore::ensureStorage() {
  // Retry a namespace that failed to open, so one transient NVS failure does not
  // refuse scans for the rest of the session.
  if (storageReady_) return true;
  if (namespaceName_.length() == 0 || namespaceName_.length() > kMaxNamespaceLength) return false;
  storageReady_ = prefs_.begin(namespaceName_.c_str(), false);
  return storageReady_;
}

void OutboxStore::end() {
  prefs_.end();
  storageReady_ = false;
  loaded_ = false;
}

void OutboxStore::copyString(char* dest, std::size_t capacity, const String& value) {
  if (capacity == 0) return;
  const auto trimmed = trimCopy(value);
  auto limit = std::min(capacity - 1U, static_cast<std::size_t>(trimmed.length()));
  // Do not cut a UTF-8 sequence: back up while the first uncopied byte is a continuation byte.
  while (limit > 0 && limit < trimmed.length() && (static_cast<unsigned char>(trimmed[limit]) & 0xC0U) == 0x80U) {
    --limit;
  }
  memcpy(dest, trimmed.c_str(), limit);
  dest[limit] = '\0';
}

void OutboxStore::slotKey(uint8_t generation, std::size_t slot, char (&out)[8]) {
  if (generation == 0) {
    snprintf(out, sizeof(out), "e%u", static_cast<unsigned>(slot));
  } else {
    snprintf(out, sizeof(out), "f%u", static_cast<unsigned>(slot));
  }
}

uint8_t OutboxStore::generationOf(uint16_t version) {
  return version == kVersionAltSlots ? 1U : 0U;
}

DeviceEventRecord OutboxStore::fromRecord(const Record& record) const {
  DeviceEventRecord event;
  event.sequence = record.sequence;
  event.eventId = makeRequestId(deviceId_, record.sequence);
  event.kind = static_cast<DeviceEventKind>(record.kind);
  // Fixed arrays may lack a trailing NUL, so each field is read with its own capacity.
  event.code = boundedString(record.code, sizeof(record.code));
  event.manufacturer = boundedString(record.manufacturer, sizeof(record.manufacturer));
  event.manufacturerPartNumber = boundedString(record.manufacturerPartNumber, sizeof(record.manufacturerPartNumber));
  event.encodedPartName = boundedString(record.encodedPartName, sizeof(record.encodedPartName));
  event.value = record.value;
  return event;
}

bool OutboxStore::writeHeader(const OutboxQueueState& state, uint8_t generation) {
  Header header;
  header.magic = kMagic;
  header.version = generation == 0 ? kVersion : kVersionAltSlots;
  header.count = static_cast<uint16_t>(state.count);
  header.head = static_cast<uint16_t>(state.head);
  header.nextSequence = state.nextSequence;
  return prefs_.putBytes(kHeaderKey, &header, sizeof(header)) == sizeof(header);
}

bool OutboxStore::writeSlot(std::size_t slot) {
  return writeSlot(generation_, slot, records_[slot]);
}

bool OutboxStore::writeSlot(uint8_t generation, std::size_t slot, const Record& record) {
  char key[8];
  slotKey(generation, slot, key);
  return prefs_.putBytes(key, &record, sizeof(Record)) == sizeof(Record);
}

bool OutboxStore::readSlot(std::size_t slot) {
  char key[8];
  slotKey(generation_, slot, key);
  if (prefs_.getBytesLength(key) != sizeof(Record)) return false;
  return prefs_.getBytes(key, &records_[slot], sizeof(Record)) == sizeof(Record);
}

void OutboxStore::eraseSlot(uint8_t generation, std::size_t slot) {
  char key[8];
  slotKey(generation, slot, key);
  if (prefs_.isKey(key)) prefs_.remove(key);
}

OutboxStore::HeaderState OutboxStore::readHeader(Header& header) {
  if (!prefs_.isKey(kHeaderKey)) return HeaderState::Absent;
  header = Header{};
  if (prefs_.getBytesLength(kHeaderKey) != sizeof(Header) ||
      prefs_.getBytes(kHeaderKey, &header, sizeof(header)) != sizeof(header) || header.magic != kMagic) {
    return HeaderState::Invalid;
  }
  // Version 5 uses "e<n>" slots, version 6 "f<n>" slots. Any other version is another layout.
  if (header.version != kVersion && header.version != kVersionAltSlots) return HeaderState::OtherVersion;
  if (!outboxHeaderFieldsValid(header.head, header.count, header.nextSequence, kCapacity)) {
    return HeaderState::Invalid;
  }
  return HeaderState::Valid;
}

// Replaces an empty header of another layout. The event sequence carries over, so no id is reused.
bool OutboxStore::adoptEmptyOtherVersionHeader(uint32_t nextSequence) {
  if (!writeHeader(makeOutboxQueueState(0, 0, 0, nextSequence), 0)) return false;
  head_ = 0;
  count_ = 0;
  tail_ = 0;
  generation_ = 0;
  nextSequence_ = nextSequence;
  unreadable_ = false;
  incompatible_ = false;
  return true;
}

bool OutboxStore::loadQueue(const Header& header) {
  generation_ = generationOf(header.version);
  count_ = header.count;
  head_ = header.head;
  tail_ = (head_ + count_) % kCapacity;
  nextSequence_ = header.nextSequence;
  for (std::size_t offset = 0; offset < count_; ++offset) {
    if (!readSlot((head_ + offset) % kCapacity)) {
      // A queued event lost its record; treat the whole queue as unreadable
      // rather than syncing an event with blank fields. Keep the NVS contents
      // intact so a transient read failure can be retried on the next boot.
      unreadable_ = true;
      return false;
    }
  }
  return true;
}

// A failed header write keeps the old sequence; the next boot tries again.
void OutboxStore::raiseLowSequenceOfEmptyQueue() {
  if (count_ != 0 || nextSequence_ >= kLowSequenceFloor || freshSequenceStart_ < kLowSequenceFloor) return;
  const auto previous = nextSequence_;
  nextSequence_ = freshSequenceStart_;
  if (!writeHeader(makeOutboxQueueState(head_, count_, tail_, nextSequence_), generation_)) {
    nextSequence_ = previous;
  }
}

bool OutboxStore::persistQueue() {
  for (std::size_t offset = 0; offset < count_; ++offset) {
    if (!writeSlot((head_ + offset) % kCapacity)) return false;
  }
  return writeHeader(makeOutboxQueueState(head_, count_, tail_, nextSequence_), generation_);
}

bool OutboxStore::adoptMigratedRecords(std::size_t count, uint32_t nextSequence) {
  count_ = count;
  head_ = 0;
  tail_ = count_ % kCapacity;
  nextSequence_ = nextSequence == 0 ? 1U : nextSequence;
  generation_ = 0;
  if (persistQueue()) {
    // Drop the old blob only once the new layout is durable.
    prefs_.remove(kLegacyStateKey);
    return true;
  }
  // Keep the legacy blob in place so a failed migration can be retried on the next boot.
  clear();
  return false;
}

OutboxStore::MigrationResult OutboxStore::migrateLegacyState() {
  const auto storedLength = prefs_.getBytesLength(kLegacyStateKey);
  if (storedLength != sizeof(LegacyState) && storedLength != sizeof(LegacyStateV4)) {
    return MigrationResult::NotRecognized;
  }

  // The version 4 blob is 4 KiB, so it is read from the heap, not the task stack.
  const auto buffer = static_cast<uint8_t*>(malloc(storedLength));
  if (buffer == nullptr) return MigrationResult::WriteFailed;
  const bool read = prefs_.getBytes(kLegacyStateKey, buffer, storedLength) == storedLength;

  auto outcome = MigrationResult::NotRecognized;
  if (read && storedLength == sizeof(LegacyStateV4)) {
    const auto* legacy = reinterpret_cast<const LegacyStateV4*>(buffer);
    if (legacy->magic == kMagic && legacy->version == 4 &&
        outboxHeaderFieldsValid(legacy->head, legacy->count, legacy->nextSequence, kCapacity)) {
      resetRecords();
      const auto count = std::min<std::size_t>(legacy->count, kCapacity);
      const auto legacyHead = std::min<std::size_t>(legacy->head, kCapacity - 1U);
      for (std::size_t offset = 0; offset < count; ++offset) {
        const auto& source = legacy->records[(legacyHead + offset) % kCapacity];
        auto& record = records_[offset];
        record.sequence = source.sequence;
        record.value = source.value;
        record.kind = source.kind;
        copyString(record.code, sizeof(record.code), boundedString(source.code, sizeof(source.code)));
        copyString(record.manufacturer, sizeof(record.manufacturer),
                   boundedString(source.manufacturer, sizeof(source.manufacturer)));
        copyString(record.manufacturerPartNumber, sizeof(record.manufacturerPartNumber),
                   boundedString(source.manufacturerPartNumber, sizeof(source.manufacturerPartNumber)));
        copyString(record.encodedPartName, sizeof(record.encodedPartName),
                   boundedString(source.encodedPartName, sizeof(source.encodedPartName)));
      }
      outcome = adoptMigratedRecords(count, legacy->nextSequence) ? MigrationResult::Migrated
                                                                 : MigrationResult::WriteFailed;
    }
  } else if (read) {
    const auto* legacy = reinterpret_cast<const LegacyState*>(buffer);
    if (legacy->magic == kMagic && legacy->version == 2 &&
        outboxHeaderFieldsValid(legacy->head, legacy->count, legacy->nextSequence, kCapacity)) {
      resetRecords();
      const auto count = std::min<std::size_t>(legacy->count, kCapacity);
      const auto legacyHead = std::min<std::size_t>(legacy->head, kCapacity - 1U);
      for (std::size_t offset = 0; offset < count; ++offset) {
        const auto& source = legacy->records[(legacyHead + offset) % kCapacity];
        auto& record = records_[offset];
        record.sequence = source.sequence;
        record.value = source.delta;
        record.kind = static_cast<uint8_t>(DeviceEventKind::InventoryAdjust);
        copyString(record.code, sizeof(record.code), boundedString(source.code, sizeof(source.code)));
      }
      outcome = adoptMigratedRecords(count, legacy->nextSequence) ? MigrationResult::Migrated
                                                                 : MigrationResult::WriteFailed;
    }
  }

  free(buffer);
  return outcome;
}

bool OutboxStore::load() {
  loaded_ = false;
  unreadable_ = false;
  incompatible_ = false;
  if (!storageReady_) {
    clear();
    return false;
  }

  Header header{};
  const auto state = readHeader(header);
  if (state == HeaderState::Valid) {
    if (loadQueue(header)) {
      raiseLowSequenceOfEmptyQueue();
      loaded_ = true;
      return true;
    }
    clear();
    return false;
  }

  if (state == HeaderState::OtherVersion && header.count == 0 && header.nextSequence != 0 &&
      adoptEmptyOtherVersionHeader(header.nextSequence)) {
    // An empty queue left by an update from another layout. Nothing is lost; the sequence carries on.
    loaded_ = true;
    return true;
  }

  if (state == HeaderState::Invalid || state == HeaderState::OtherVersion) {
    // Unreadable header: scans from another layout (e.g. after an OTA rollback), damage, out-of-range
    // fields, or an empty header that could not be replaced. Leave the bytes for an operator repair.
    // Migration never runs here, so an older blob cannot overwrite a live queue.
    clear();
    unreadable_ = true;
    incompatible_ = true;
    return false;
  }

  // No current header: migrate an older layout. An unrecognised blob is kept and reported,
  // as it may hold the only copy of the queue.
  if (prefs_.isKey(kLegacyStateKey)) {
    const auto migration = migrateLegacyState();
    if (migration == MigrationResult::Migrated) {
      loaded_ = true;
      return true;
    }
    clear();
    // WriteFailed: the old queue is still intact, so it is retried on the next boot.
    unreadable_ = migration == MigrationResult::NotRecognized;
    incompatible_ = unreadable_;
    return false;
  }

  // Nothing stored: a new or erased outbox. The sequence starts at a random point so ids do not repeat.
  clear();
  nextSequence_ = freshSequenceStart_;
  if (!writeHeader(makeOutboxQueueState(head_, count_, tail_, nextSequence_), generation_)) return false;
  loaded_ = true;
  return true;
}

OutboxStore::EnqueueResult OutboxStore::enqueue(DeviceEventKind kind, const String& code, int value,
                                                DeviceEventRecord* created, const String& manufacturer,
                                                const String& manufacturerPartNumber,
                                                const String& encodedPartName) {
  const auto trimmed = trimCopy(code);
  if (trimmed.length() == 0 || trimmed.length() >= kCodeLen || value == 0) return EnqueueResult::Rejected;
  if (!ensureStorage()) return EnqueueResult::StorageFailed;
  if (!loaded_ && !load()) return EnqueueResult::StorageFailed;
  if (full()) return EnqueueResult::QueueFull;

  const auto current = makeOutboxQueueState(head_, count_, tail_, nextSequence_);
  OutboxQueueState next;
  if (!prepareOutboxEnqueue(current, kCapacity, next)) return EnqueueResult::StorageFailed;
  const auto slot = tail_;
  Record record{};
  record.sequence = nextSequence_;
  record.value = value;
  record.kind = static_cast<uint8_t>(kind);
  copyString(record.code, sizeof(record.code), trimmed);
  copyString(record.manufacturer, sizeof(record.manufacturer), manufacturer);
  copyString(record.manufacturerPartNumber, sizeof(record.manufacturerPartNumber), manufacturerPartNumber);
  copyString(record.encodedPartName, sizeof(record.encodedPartName), encodedPartName);
  // Store the record before the header: a power cut leaves at most an unreferenced slot.
  // If either write fails, the old header and RAM state still rule; the new slot is harmless.
  if (!writeSlot(generation_, slot, record) || !writeHeader(next, generation_)) {
    return EnqueueResult::StorageFailed;
  }

  records_[slot] = record;
  head_ = next.head;
  count_ = next.count;
  tail_ = next.tail;
  nextSequence_ = next.nextSequence;
  if (created != nullptr) *created = fromRecord(record);
  return EnqueueResult::Queued;
}

bool OutboxStore::peek(std::size_t offset, DeviceEventRecord& event) const {
  if (!loaded_ || offset >= count_) return false;
  event = fromRecord(records_[(head_ + offset) % kCapacity]);
  return true;
}

bool OutboxStore::pop(DeviceEventRecord* event) {
  if (!loaded_ || empty()) return false;
  const auto slot = head_;
  const auto current = makeOutboxQueueState(head_, count_, tail_, nextSequence_);
  OutboxQueueState next;
  if (!prepareOutboxPop(current, kCapacity, next)) return false;

  // Commit the header before changing RAM or erasing the payload; on failure the event stays queued.
  if (!writeHeader(next, generation_)) return false;

  if (event != nullptr) *event = fromRecord(records_[slot]);
  head_ = next.head;
  count_ = next.count;
  tail_ = next.tail;
  nextSequence_ = next.nextSequence;
  // A power cut after the commit can leave this payload, now unreferenced, to reclaim later.
  eraseSlot(generation_, slot);
  return true;
}

void OutboxStore::clear() {
  resetRecords();
  count_ = head_ = tail_ = 0;
  nextSequence_ = 1;
  generation_ = 0;
}

void OutboxStore::resetRecords() {
  // Assign a default Record: memset gives the same bytes, but -Wclass-memaccess rejects it for types
  // with default member initialisers.
  for (auto& record : records_) record = Record();
}

OutboxStore::RepairResult OutboxStore::discardUnreadableQueue(std::size_t& dropped) {
  dropped = 0;
  for (uint8_t generation = 0; generation < 2; ++generation) {
    for (std::size_t slot = 0; slot < kCapacity; ++slot) {
      char key[8];
      slotKey(generation, slot, key);
      if (prefs_.isKey(key)) ++dropped;
    }
  }

  // Write the empty header first: a power cut leaves the old unreadable bytes (repairable again) or
  // an empty queue. Orphaned slots and any legacy blob are erased after that commit. The sequence
  // restarts at this boot's random point, so discarded ids are not reused in practice.
  if (!writeHeader(makeOutboxQueueState(0, 0, 0, freshSequenceStart_), 0)) return RepairResult::StorageFailed;
  for (std::size_t slot = 0; slot < kCapacity; ++slot) {
    eraseSlot(0, slot);
    eraseSlot(1, slot);
  }
  if (prefs_.isKey(kLegacyStateKey)) prefs_.remove(kLegacyStateKey);

  clear();
  nextSequence_ = freshSequenceStart_;
  loaded_ = true;
  unreadable_ = false;
  incompatible_ = false;
  return RepairResult::Repaired;
}

// Reads one record for repair, retrying a transient NVS failure before the record counts as lost.
bool OutboxStore::readRecordForRepair(const char* key, Record& record) {
  constexpr uint8_t kReadAttempts = 3;
  for (uint8_t attempt = 0; attempt < kReadAttempts; ++attempt) {
    if (prefs_.getBytesLength(key) == sizeof(Record) && prefs_.getBytes(key, &record, sizeof(Record)) == sizeof(Record)) {
      return true;
    }
  }
  return false;
}

OutboxStore::RepairResult OutboxStore::repair(std::size_t& salvaged, std::size_t& dropped) {
  salvaged = 0;
  dropped = 0;
  if (!storageReady_ || !unreadable_) return RepairResult::NothingToRepair;

  Header header{};
  if (readHeader(header) != HeaderState::Valid) {
    // An unreadable header cannot locate the records, so the whole queue is discarded.
    // Runs only after the operator confirms the repair.
    return discardUnreadableQueue(dropped);
  }

  const std::size_t count = header.count;
  const std::size_t head = header.head;
  const uint8_t oldGeneration = generationOf(header.version);
  const uint8_t newGeneration = static_cast<uint8_t>(oldGeneration ^ 1U);

  // records_ doubles as scratch space while unreadable_ is set; a stack array would be about 4 KB.
  Record* const kept = records_;

  for (std::size_t offset = 0; offset < count; ++offset) {
    char key[8];
    slotKey(oldGeneration, (head + offset) % kCapacity, key);
    if (readRecordForRepair(key, kept[salvaged])) {
      salvaged++;
      continue;
    }
    dropped++;
  }

  if (dropped == 0) {
    load();
    return RepairResult::NothingToRepair;
  }

  // Salvaged records go into the other slot set, which the current header does not reference. The
  // header write is the single commit; a power cut before it leaves the old queue repairable again.
  for (std::size_t i = 0; i < salvaged; ++i) {
    if (!writeSlot(newGeneration, i, kept[i])) return RepairResult::StorageFailed;
  }
  if (!writeHeader(makeOutboxQueueState(0, salvaged, salvaged % kCapacity, header.nextSequence), newGeneration)) {
    return RepairResult::StorageFailed;
  }
  for (std::size_t i = 0; i < kCapacity; ++i) {
    eraseSlot(oldGeneration, i);
  }

  // records_[i] is now slot i of the new set, so the in-memory queue matches NVS as written.
  generation_ = newGeneration;
  head_ = 0;
  count_ = salvaged;
  tail_ = salvaged % kCapacity;
  nextSequence_ = header.nextSequence;
  loaded_ = true;
  unreadable_ = false;
  incompatible_ = false;
  return RepairResult::Repaired;
}

}  // namespace inventatory_scan
