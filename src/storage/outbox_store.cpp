#include "storage/outbox_store.h"

#include "InventatoryScanCore.h"

#include <algorithm>
#include <cstring>

namespace inventatory_scan {

namespace {

constexpr std::size_t kLegacyCodeLen = 96;

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

}  // namespace

bool OutboxStore::begin(const char* namespaceName, const char* deviceId) {
  deviceId_ = deviceId == nullptr ? String() : String(deviceId);
  return prefs_.begin(namespaceName, false);
}

void OutboxStore::end() { prefs_.end(); }

void OutboxStore::copyString(char* dest, std::size_t capacity, const String& value) {
  if (capacity == 0) return;
  const auto trimmed = trimCopy(value);
  const auto limit = std::min(capacity - 1U, static_cast<std::size_t>(trimmed.length()));
  memcpy(dest, trimmed.c_str(), limit);
  dest[limit] = '\0';
}

String OutboxStore::readString(const char* value) { return value == nullptr ? String() : String(value); }

DeviceEventRecord OutboxStore::fromRecord(const Record& record) const {
  DeviceEventRecord event;
  event.sequence = record.sequence;
  event.eventId = makeRequestId(deviceId_, record.sequence);
  event.kind = static_cast<DeviceEventKind>(record.kind);
  event.code = readString(record.code);
  event.value = record.value;
  return event;
}

void OutboxStore::syncFromState() {
  count_ = std::min<std::size_t>(state_.count, kCapacity);
  head_ = std::min<std::size_t>(state_.head, kCapacity - 1U);
  tail_ = (head_ + count_) % kCapacity;
  nextSequence_ = state_.nextSequence == 0 ? 1U : state_.nextSequence;
}

void OutboxStore::syncToState() {
  state_.magic = kMagic;
  state_.version = kVersion;
  state_.count = static_cast<uint16_t>(count_);
  state_.head = static_cast<uint16_t>(head_);
  state_.nextSequence = nextSequence_;
}

bool OutboxStore::migrateVersion2() {
  if (prefs_.getBytesLength("state") != sizeof(LegacyState)) return false;
  LegacyState legacy{};
  if (prefs_.getBytes("state", &legacy, sizeof(legacy)) != sizeof(legacy) ||
      legacy.magic != kMagic || legacy.version != 2) return false;

  memset(&state_, 0, sizeof(state_));
  state_.magic = kMagic;
  state_.version = kVersion;
  count_ = 0;
  head_ = 0;
  tail_ = 0;
  const auto legacyCount = std::min<std::size_t>(legacy.count, kCapacity);
  for (std::size_t offset = 0; offset < legacyCount; ++offset) {
    const auto index = (legacy.head + offset) % kCapacity;
    auto& record = state_.records[tail_];
    record.sequence = legacy.records[index].sequence;
    record.value = legacy.records[index].delta;
    record.kind = static_cast<uint8_t>(DeviceEventKind::InventoryAdjust);
    copyString(record.code, sizeof(record.code), String(legacy.records[index].code));
    tail_ = (tail_ + 1U) % kCapacity;
    ++count_;
  }
  nextSequence_ = legacy.nextSequence == 0 ? 1U : legacy.nextSequence;
  return save();
}

bool OutboxStore::load() {
  if (!prefs_.isKey("state")) {
    clear();
    return true;
  }
  if (prefs_.getBytesLength("state") == sizeof(state_) &&
      prefs_.getBytes("state", &state_, sizeof(state_)) == sizeof(state_) &&
      state_.magic == kMagic && state_.version == kVersion) {
    syncFromState();
    return true;
  }
  if (migrateVersion2()) return true;
  clear();
  return save();
}

bool OutboxStore::save() {
  syncToState();
  return prefs_.putBytes("state", &state_, sizeof(state_)) == sizeof(state_);
}

bool OutboxStore::enqueue(DeviceEventKind kind, const String& code, int value, DeviceEventRecord* created) {
  const auto trimmed = trimCopy(code);
  if (full() || trimmed.length() == 0 || trimmed.length() >= kCodeLen || value == 0) return false;
  Record record{};
  record.sequence = nextSequence_++;
  record.value = value;
  record.kind = static_cast<uint8_t>(kind);
  copyString(record.code, sizeof(record.code), trimmed);
  state_.records[tail_] = record;
  tail_ = (tail_ + 1U) % kCapacity;
  ++count_;
  if (!save()) {
    tail_ = (tail_ + kCapacity - 1U) % kCapacity;
    --count_;
    --nextSequence_;
    return false;
  }
  if (created != nullptr) *created = fromRecord(record);
  return true;
}

bool OutboxStore::peek(std::size_t offset, DeviceEventRecord& event) const {
  if (offset >= count_) return false;
  event = fromRecord(state_.records[(head_ + offset) % kCapacity]);
  return true;
}

bool OutboxStore::pop(DeviceEventRecord* event) {
  if (empty()) return false;
  if (event != nullptr) *event = fromRecord(state_.records[head_]);
  head_ = (head_ + 1U) % kCapacity;
  --count_;
  if (count_ == 0) head_ = tail_ = 0;
  return save();
}

void OutboxStore::clear() {
  memset(&state_, 0, sizeof(state_));
  state_.magic = kMagic;
  state_.version = kVersion;
  state_.nextSequence = 1;
  count_ = head_ = tail_ = 0;
  nextSequence_ = 1;
}

}  // namespace inventatory_scan
