// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

// Recovery tests for the offline outbox. They cover a queue header this firmware
// cannot read (for example after an OTA rollback to an older image), the
// operator-confirmed discard that clears it, the random start of a new event
// sequence, and the rule that every stored header field is range-checked rather
// than clamped. Each test starts from empty NVS (see main.cpp).
#include <unity.h>

#include <cstdint>
#include <cstring>

#include "InventatoryScanOutbox.h"
#include "Preferences.h"
#include "storage/outbox_store.h"

using namespace inventatory_scan;

void runHostOutboxRecoveryTests();

void test_host_outbox_fresh_sequence_start_is_odd_nonzero_and_bounded();
void test_host_outbox_header_field_rule_rejects_out_of_range_values();
void test_host_outbox_new_outbox_starts_at_the_given_sequence();
void test_host_outbox_erased_outbox_does_not_reuse_event_ids();
void test_host_outbox_out_of_range_head_is_refused_not_clamped();
void test_host_outbox_foreign_header_with_scans_is_refused_and_kept_intact();
void test_host_outbox_empty_foreign_header_is_adopted_and_keeps_its_sequence();
void test_host_outbox_empty_foreign_header_without_a_sequence_is_refused_and_kept();
void test_host_outbox_failed_adoption_keeps_the_header_and_retries_at_next_boot();
void test_host_outbox_operator_discard_recovers_a_foreign_header();
void test_host_outbox_operator_discard_removes_an_unrecognised_legacy_blob();
void test_host_outbox_copy_never_splits_a_utf8_sequence();

namespace {

constexpr char kNamespace[] = "inv_outbox";
constexpr char kDevice[] = "R1-TEST";
constexpr uint32_t kMagic = 0x48534f51UL;

// The header as the firmware writes it, mirrored here so the tests can damage
// individual fields and check the bytes that are left behind.
struct RawHeader {
  uint32_t magic;
  uint16_t version;
  uint16_t count;
  uint16_t head;
  uint16_t reserved;
  uint32_t nextSequence;
};
static_assert(sizeof(RawHeader) == 16, "RawHeader must match the stored header");

void storeRawHeader(const RawHeader& header) {
  Preferences prefs;
  TEST_ASSERT_TRUE(prefs.begin(kNamespace, false));
  TEST_ASSERT_EQUAL_UINT32(sizeof(header), prefs.putBytes("hdr", &header, sizeof(header)));
}

RawHeader readRawHeader() {
  RawHeader header{};
  Preferences prefs;
  TEST_ASSERT_TRUE(prefs.begin(kNamespace, true));
  TEST_ASSERT_EQUAL_UINT32(sizeof(header), prefs.getBytes("hdr", &header, sizeof(header)));
  return header;
}

// Returns the number of bytes stored under "hdr", copied into the buffer.
std::size_t storedHeaderBytes(uint8_t (&out)[sizeof(RawHeader)]) {
  Preferences prefs;
  TEST_ASSERT_TRUE(prefs.begin(kNamespace, true));
  return prefs.getBytes("hdr", out, sizeof(out));
}

bool storedKey(const char* key) {
  Preferences prefs;
  TEST_ASSERT_TRUE(prefs.begin(kNamespace, true));
  return prefs.isKey(key);
}

bool bootWithSeed(OutboxStore& outbox, uint32_t freshSequenceStart) {
  return outbox.begin(kNamespace, kDevice, freshSequenceStart) && outbox.load();
}

void expectQueued(OutboxStore& outbox, const char* code) {
  TEST_ASSERT_EQUAL_INT(static_cast<int>(OutboxStore::EnqueueResult::Queued),
                        static_cast<int>(outbox.enqueue(DeviceEventKind::InventoryAdjust, String(code), 1)));
}

}  // namespace

void test_host_outbox_fresh_sequence_start_is_odd_nonzero_and_bounded() {
  const uint32_t samples[] = {0U, 1U, 2U, 0x7FFFFFFFU, 0x80000000U, 0xFFFFFFFFU, 0x12345678U};
  for (const uint32_t entropy : samples) {
    const uint32_t start = freshOutboxSequenceStart(entropy);
    TEST_ASSERT_EQUAL_UINT32(1U, start & 1U);
    TEST_ASSERT_TRUE(start >= 1U);
    TEST_ASSERT_TRUE(start < 0x80000000U);
  }
  // The start must follow the entropy, not a constant: otherwise every device would
  // begin at the same id, which is the collision the random start exists to prevent.
  TEST_ASSERT_EQUAL_UINT32(0x12345679U, freshOutboxSequenceStart(0x12345678U));
  TEST_ASSERT_EQUAL_UINT32(0x12345679U, freshOutboxSequenceStart(0x92345678U));
  TEST_ASSERT_EQUAL_UINT32(1U, freshOutboxSequenceStart(0x80000000U));
  TEST_ASSERT_TRUE(freshOutboxSequenceStart(0x11111111U) != freshOutboxSequenceStart(0x22222222U));
}

void test_host_outbox_header_field_rule_rejects_out_of_range_values() {
  TEST_ASSERT_TRUE(outboxHeaderFieldsValid(0, 0, 1, 16));
  TEST_ASSERT_TRUE(outboxHeaderFieldsValid(15, 16, 1, 16));
  TEST_ASSERT_FALSE(outboxHeaderFieldsValid(16, 0, 1, 16));
  TEST_ASSERT_FALSE(outboxHeaderFieldsValid(0, 17, 1, 16));
  TEST_ASSERT_FALSE(outboxHeaderFieldsValid(0, 0, 0, 16));
  TEST_ASSERT_FALSE(outboxHeaderFieldsValid(0, 0, 1, 0));
}

void test_host_outbox_new_outbox_starts_at_the_given_sequence() {
  OutboxStore outbox;
  TEST_ASSERT_TRUE(bootWithSeed(outbox, 0x1001U));
  TEST_ASSERT_EQUAL_UINT32(0x1001U, outbox.nextSequence());

  DeviceEventRecord created;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(OutboxStore::EnqueueResult::Queued),
                        static_cast<int>(outbox.enqueue(DeviceEventKind::InventoryAdjust, String("A"), 1, &created)));
  TEST_ASSERT_EQUAL_STRING("R1-TEST-4097", created.eventId.c_str());
}

void test_host_outbox_erased_outbox_does_not_reuse_event_ids() {
  // First life of the outbox: three events, all with ids below 4100.
  OutboxStore first;
  TEST_ASSERT_TRUE(bootWithSeed(first, 0x1001U));
  expectQueued(first, "A");
  expectQueued(first, "B");
  expectQueued(first, "C");
  DeviceEventRecord issued[3];
  for (std::size_t offset = 0; offset < 3; ++offset) TEST_ASSERT_TRUE(first.peek(offset, issued[offset]));

  // A full flash erase wipes NVS. The device boots with a new random start.
  // The outbox namespace is never cleared on its own (AGENTS.md section 7).
  host_nvs::reset();
  host_nvs::reboot();

  OutboxStore second;
  TEST_ASSERT_TRUE(bootWithSeed(second, 0x5555U));
  DeviceEventRecord reissued;
  TEST_ASSERT_FALSE(second.peek(0, reissued));
  DeviceEventRecord created;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(OutboxStore::EnqueueResult::Queued),
                        static_cast<int>(second.enqueue(DeviceEventKind::InventoryAdjust, String("D"), 1, &created)));
  for (const auto& old : issued) TEST_ASSERT_TRUE(created.eventId != old.eventId);
  TEST_ASSERT_EQUAL_STRING("R1-TEST-21845", created.eventId.c_str());
}

void test_host_outbox_out_of_range_head_is_refused_not_clamped() {
  OutboxStore outbox;
  TEST_ASSERT_TRUE(bootWithSeed(outbox, 1U));
  expectQueued(outbox, "A");
  expectQueued(outbox, "B");

  RawHeader damaged = readRawHeader();
  damaged.head = 16;  // one past the last slot; the old code silently used slot 15
  storeRawHeader(damaged);
  uint8_t before[sizeof(RawHeader)] = {};
  TEST_ASSERT_EQUAL_UINT32(sizeof(before), storedHeaderBytes(before));

  host_nvs::reboot();
  OutboxStore reloaded;
  TEST_ASSERT_FALSE(bootWithSeed(reloaded, 1U));
  TEST_ASSERT_TRUE(reloaded.unreadable());
  TEST_ASSERT_EQUAL_UINT32(0, reloaded.size());

  uint8_t after[sizeof(RawHeader)] = {};
  TEST_ASSERT_EQUAL_UINT32(sizeof(after), storedHeaderBytes(after));
  TEST_ASSERT_EQUAL_MEMORY(before, after, sizeof(before));
}

void test_host_outbox_foreign_header_with_scans_is_refused_and_kept_intact() {
  // A header from a newer layout that still lists a queued scan, as left behind before an OTA rollback.
  // Version 6 is a layout this firmware reads (the alternate slot set), so the foreign version is 7.
  storeRawHeader(RawHeader{kMagic, 7, 1, 0, 0, 1});

  OutboxStore outbox;
  TEST_ASSERT_FALSE(bootWithSeed(outbox, 1U));
  TEST_ASSERT_TRUE(outbox.unreadable());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(OutboxStore::EnqueueResult::StorageFailed),
                        static_cast<int>(outbox.enqueue(DeviceEventKind::InventoryAdjust, String("X"), 1)));
  // Nothing is written over the foreign header.
  TEST_ASSERT_EQUAL_UINT16(7, readRawHeader().version);
}

void test_host_outbox_empty_foreign_header_is_adopted_and_keeps_its_sequence() {
  // A header from another layout with no scans in it, as an update leaves it. Nothing can be lost, so
  // this layout's empty header replaces it: the scanner keeps working, and the event sequence carries on,
  // so no event id is issued twice.
  OutboxStore first;
  TEST_ASSERT_TRUE(bootWithSeed(first, 0x1001U));
  const uint16_t current = readRawHeader().version;
  storeRawHeader(RawHeader{kMagic, static_cast<uint16_t>(current + 2U), 0, 0, 0, 0x4001U});

  host_nvs::reboot();
  OutboxStore outbox;
  TEST_ASSERT_TRUE(bootWithSeed(outbox, 0x9001U));
  TEST_ASSERT_FALSE(outbox.unreadable());
  TEST_ASSERT_EQUAL_UINT32(0, outbox.size());
  TEST_ASSERT_EQUAL_UINT32(0x4001U, outbox.nextSequence());
  TEST_ASSERT_EQUAL_UINT16(current, readRawHeader().version);

  expectQueued(outbox, "A");
  DeviceEventRecord event;
  TEST_ASSERT_TRUE(outbox.peek(0, event));
  TEST_ASSERT_EQUAL_STRING("R1-TEST-16385", event.eventId.c_str());
}

void test_host_outbox_empty_foreign_header_without_a_sequence_is_refused_and_kept() {
  // Without an event sequence the header cannot be continued safely, so it is kept as it is.
  storeRawHeader(RawHeader{kMagic, 7, 0, 0, 0, 0});

  OutboxStore outbox;
  TEST_ASSERT_FALSE(bootWithSeed(outbox, 1U));
  TEST_ASSERT_TRUE(outbox.unreadable());
  TEST_ASSERT_EQUAL_UINT16(7, readRawHeader().version);
}

void test_host_outbox_failed_adoption_keeps_the_header_and_retries_at_next_boot() {
  // If the replacement header cannot be written, the old header stays and the next boot tries again.
  storeRawHeader(RawHeader{kMagic, 7, 0, 0, 0, 0x4001U});

  host_nvs::faults().failWrites = true;
  OutboxStore outbox;
  TEST_ASSERT_FALSE(bootWithSeed(outbox, 0x9001U));
  TEST_ASSERT_TRUE(outbox.unreadable());
  TEST_ASSERT_EQUAL_UINT16(7, readRawHeader().version);

  host_nvs::reboot();
  OutboxStore retried;
  TEST_ASSERT_TRUE(bootWithSeed(retried, 0x9001U));
  TEST_ASSERT_FALSE(retried.unreadable());
  TEST_ASSERT_EQUAL_UINT32(0x4001U, retried.nextSequence());
}

void test_host_outbox_operator_discard_recovers_a_foreign_header() {
  OutboxStore before;
  TEST_ASSERT_TRUE(bootWithSeed(before, 0x3001U));
  expectQueued(before, "A");
  expectQueued(before, "B");
  RawHeader foreign = readRawHeader();
  foreign.version = 7;
  storeRawHeader(foreign);

  host_nvs::reboot();
  OutboxStore outbox;
  TEST_ASSERT_FALSE(bootWithSeed(outbox, 0x5001U));
  TEST_ASSERT_TRUE(outbox.unreadable());

  // The operator confirms the repair. Nothing can be salvaged from a header that
  // this firmware cannot locate, so both stored records are removed.
  std::size_t salvaged = 99;
  std::size_t dropped = 0;
  TEST_ASSERT_TRUE(outbox.repair(salvaged, dropped) == OutboxStore::RepairResult::Repaired);
  TEST_ASSERT_EQUAL_UINT32(0, salvaged);
  TEST_ASSERT_EQUAL_UINT32(2, dropped);
  TEST_ASSERT_FALSE(outbox.unreadable());
  TEST_ASSERT_EQUAL_UINT32(0x5001U, outbox.nextSequence());

  // The device works again straight away, and the new queue survives a reboot.
  expectQueued(outbox, "C");
  host_nvs::reboot();
  OutboxStore rebooted;
  TEST_ASSERT_TRUE(bootWithSeed(rebooted, 0x7001U));
  TEST_ASSERT_EQUAL_UINT32(1, rebooted.size());
  DeviceEventRecord event;
  TEST_ASSERT_TRUE(rebooted.peek(0, event));
  TEST_ASSERT_EQUAL_STRING("C", event.code.c_str());
  TEST_ASSERT_EQUAL_STRING("R1-TEST-20481", event.eventId.c_str());
}

void test_host_outbox_operator_discard_removes_an_unrecognised_legacy_blob() {
  // A blob of a size no layout uses. It may be the only copy of an old queue, so
  // it is kept until the operator confirms the discard.
  Preferences prefs;
  TEST_ASSERT_TRUE(prefs.begin(kNamespace, false));
  const uint8_t unknown[7] = {1, 2, 3, 4, 5, 6, 7};
  TEST_ASSERT_EQUAL_UINT32(sizeof(unknown), prefs.putBytes("state", unknown, sizeof(unknown)));

  OutboxStore outbox;
  TEST_ASSERT_FALSE(bootWithSeed(outbox, 0x3001U));
  TEST_ASSERT_TRUE(outbox.unreadable());
  TEST_ASSERT_TRUE(storedKey("state"));
  TEST_ASSERT_FALSE(storedKey("hdr"));

  std::size_t salvaged = 0;
  std::size_t dropped = 0;
  TEST_ASSERT_TRUE(outbox.repair(salvaged, dropped) == OutboxStore::RepairResult::Repaired);
  TEST_ASSERT_FALSE(storedKey("state"));
  TEST_ASSERT_TRUE(storedKey("hdr"));
  expectQueued(outbox, "A");
}

void test_host_outbox_copy_never_splits_a_utf8_sequence() {
  OutboxStore outbox;
  TEST_ASSERT_TRUE(bootWithSeed(outbox, 1U));

  // 45 ASCII bytes and U+00E9 (two bytes) fill the 47 usable bytes exactly.
  String fits;
  for (int index = 0; index < 45; ++index) fits += 'a';
  fits += "\xC3\xA9";
  DeviceEventRecord exact;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(OutboxStore::EnqueueResult::Queued),
                        static_cast<int>(outbox.enqueue(DeviceEventKind::InventoryAdjust, String("EXACT"), 1,
                                                        &exact, fits)));
  TEST_ASSERT_EQUAL_UINT32(47, exact.manufacturer.length());
  TEST_ASSERT_EQUAL_STRING(fits.c_str(), exact.manufacturer.c_str());

  // 46 ASCII bytes then U+00E9 would need 48 bytes. The copy stops before the
  // whole character rather than keeping a lone lead byte.
  String overflows;
  for (int index = 0; index < 46; ++index) overflows += 'a';
  overflows += "\xC3\xA9";
  DeviceEventRecord cut;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(OutboxStore::EnqueueResult::Queued),
                        static_cast<int>(outbox.enqueue(DeviceEventKind::InventoryAdjust, String("CUT"), 1, &cut,
                                                        overflows)));
  TEST_ASSERT_EQUAL_UINT32(46, cut.manufacturer.length());
  TEST_ASSERT_EQUAL_UINT8('a', static_cast<uint8_t>(cut.manufacturer[45]));
}

void runHostOutboxRecoveryTests() {
  RUN_TEST(test_host_outbox_fresh_sequence_start_is_odd_nonzero_and_bounded);
  RUN_TEST(test_host_outbox_header_field_rule_rejects_out_of_range_values);
  RUN_TEST(test_host_outbox_new_outbox_starts_at_the_given_sequence);
  RUN_TEST(test_host_outbox_erased_outbox_does_not_reuse_event_ids);
  RUN_TEST(test_host_outbox_out_of_range_head_is_refused_not_clamped);
  RUN_TEST(test_host_outbox_foreign_header_with_scans_is_refused_and_kept_intact);
  RUN_TEST(test_host_outbox_empty_foreign_header_is_adopted_and_keeps_its_sequence);
  RUN_TEST(test_host_outbox_empty_foreign_header_without_a_sequence_is_refused_and_kept);
  RUN_TEST(test_host_outbox_failed_adoption_keeps_the_header_and_retries_at_next_boot);
  RUN_TEST(test_host_outbox_operator_discard_recovers_a_foreign_header);
  RUN_TEST(test_host_outbox_operator_discard_removes_an_unrecognised_legacy_blob);
  RUN_TEST(test_host_outbox_copy_never_splits_a_utf8_sequence);
}
