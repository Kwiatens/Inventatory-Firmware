// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

// Durability tests for the offline outbox (AGENTS.md section 7). They run the
// real OutboxStore against the NVS stand-in and inject write failures and power
// loss at each write the store makes, then "reboot" by building a new store
// over the same storage. A scan must never be reported as queued unless it
// survives that reboot, and a queued event must never disappear or be
// delivered twice because of a failed write.
#include <unity.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "Preferences.h"
#include "storage/outbox_store.h"

using namespace inventatory_scan;

void runHostOutboxDurabilityTests();

void test_host_outbox_fresh_device_starts_empty();
void test_host_outbox_survives_reboot_in_order();
void test_host_outbox_low_sequence_of_empty_queue_moves_past_stored_ids();
void test_host_outbox_queued_scans_keep_their_sequence_at_boot();
void test_host_outbox_persists_every_event_field();
void test_host_outbox_truncates_fields_to_their_storage_limits();
void test_host_outbox_rejects_invalid_scans_before_any_write();
void test_host_outbox_reports_full_queue_before_storage_errors();
void test_host_outbox_slot_write_failure_leaves_queue_unchanged();
void test_host_outbox_header_write_failure_keeps_new_event_out();
void test_host_outbox_power_cut_during_pop_keeps_event_queued();
void test_host_outbox_committed_pop_is_not_redelivered_after_power_cut();
void test_host_outbox_write_error_during_pop_keeps_event_queued();
void test_host_outbox_event_ids_are_not_reused_after_drain();
void test_host_outbox_wrap_around_keeps_order_across_reboot();
void test_host_outbox_lost_record_blocks_scans_until_repaired();
void test_host_outbox_repair_of_healthy_queue_reports_nothing_to_repair();
void test_host_outbox_namespace_too_long_refuses_scans();
void test_host_outbox_unrecognised_legacy_blob_is_never_destroyed();
void test_host_outbox_truncated_header_is_never_overwritten();
void test_host_outbox_power_cut_during_repair_does_not_duplicate_events();
void test_host_outbox_repair_survives_a_power_cut_at_every_write();

namespace {

constexpr char kNamespace[] = "inv_outbox";
constexpr char kDevice[] = "R1-TEST";

// Opens the outbox the way the firmware does at boot and loads the stored queue.
bool bootOutbox(OutboxStore& outbox) { return outbox.begin(kNamespace, kDevice) && outbox.load(); }

void expectEnqueue(OutboxStore::EnqueueResult expected, OutboxStore::EnqueueResult actual) {
  TEST_ASSERT_EQUAL_INT(static_cast<int>(expected), static_cast<int>(actual));
}

OutboxStore::EnqueueResult adjust(OutboxStore& outbox, const char* code, int value,
                                  DeviceEventRecord* created = nullptr) {
  return outbox.enqueue(DeviceEventKind::InventoryAdjust, String(code), value, created);
}

String codeAt(const OutboxStore& outbox, std::size_t offset) {
  DeviceEventRecord event;
  TEST_ASSERT_TRUE(outbox.peek(offset, event));
  return event.code;
}

void expectQueueCodes(const OutboxStore& outbox, const char* const* codes, std::size_t count) {
  TEST_ASSERT_EQUAL_UINT32(count, outbox.size());
  for (std::size_t index = 0; index < count; ++index) {
    TEST_ASSERT_EQUAL_STRING(codes[index], codeAt(outbox, index).c_str());
  }
}

bool storedKey(const char* key) {
  Preferences prefs;
  TEST_ASSERT_TRUE(prefs.begin(kNamespace, true));
  return prefs.isKey(key);
}

}  // namespace

void test_host_outbox_fresh_device_starts_empty() {
  OutboxStore outbox;
  TEST_ASSERT_TRUE(bootOutbox(outbox));
  TEST_ASSERT_EQUAL_UINT32(0, outbox.size());
  TEST_ASSERT_EQUAL_UINT32(1, outbox.nextSequence());
  TEST_ASSERT_TRUE(storedKey("hdr"));
}

void test_host_outbox_survives_reboot_in_order() {
  OutboxStore before;
  TEST_ASSERT_TRUE(bootOutbox(before));
  expectEnqueue(OutboxStore::EnqueueResult::Queued, adjust(before, "A1", 2));
  expectEnqueue(OutboxStore::EnqueueResult::Queued, adjust(before, "B2", -1));
  expectEnqueue(OutboxStore::EnqueueResult::Queued, adjust(before, "C3", 5));

  host_nvs::reboot();
  OutboxStore after;
  TEST_ASSERT_TRUE(bootOutbox(after));
  const char* const codes[] = {"A1", "B2", "C3"};
  expectQueueCodes(after, codes, 3);

  DeviceEventRecord event;
  TEST_ASSERT_TRUE(after.peek(1, event));
  TEST_ASSERT_EQUAL_INT(-1, event.value);
  TEST_ASSERT_EQUAL_UINT32(2, event.sequence);
  TEST_ASSERT_EQUAL_UINT32(4, after.nextSequence());
}

void test_host_outbox_low_sequence_of_empty_queue_moves_past_stored_ids() {
  // Firmware before the random start numbered events from 1, and the desktop keeps the event ids it has stored. An
  // empty queue that carries such a low number moves to the fresh start, so its next event is not a stored id.
  OutboxStore before;
  TEST_ASSERT_TRUE(bootOutbox(before));
  expectEnqueue(OutboxStore::EnqueueResult::Queued, adjust(before, "A1", 2));
  expectEnqueue(OutboxStore::EnqueueResult::Queued, adjust(before, "B2", -1));
  TEST_ASSERT_TRUE(before.pop());
  TEST_ASSERT_TRUE(before.pop());
  TEST_ASSERT_EQUAL_UINT32(3, before.nextSequence());

  host_nvs::reboot();
  OutboxStore after;
  TEST_ASSERT_TRUE(after.begin(kNamespace, kDevice, 900000123U) && after.load());
  TEST_ASSERT_EQUAL_UINT32(0, after.size());
  TEST_ASSERT_EQUAL_UINT32(900000123U, after.nextSequence());

  // The raised number is stored, so a later boot keeps it and the next event takes it.
  host_nvs::reboot();
  OutboxStore later;
  TEST_ASSERT_TRUE(later.begin(kNamespace, kDevice, 5U) && later.load());
  TEST_ASSERT_EQUAL_UINT32(900000123U, later.nextSequence());
  DeviceEventRecord created;
  expectEnqueue(OutboxStore::EnqueueResult::Queued, adjust(later, "C3", 1, &created));
  TEST_ASSERT_EQUAL_UINT32(900000123U, created.sequence);
}

void test_host_outbox_queued_scans_keep_their_sequence_at_boot() {
  // The number is raised only when the queue is empty, so the ids of queued scans never change.
  OutboxStore before;
  TEST_ASSERT_TRUE(bootOutbox(before));
  expectEnqueue(OutboxStore::EnqueueResult::Queued, adjust(before, "A1", 2));

  host_nvs::reboot();
  OutboxStore after;
  TEST_ASSERT_TRUE(after.begin(kNamespace, kDevice, 900000123U) && after.load());
  TEST_ASSERT_EQUAL_UINT32(1, after.size());
  TEST_ASSERT_EQUAL_UINT32(2, after.nextSequence());
}

void test_host_outbox_persists_every_event_field() {
  OutboxStore before;
  TEST_ASSERT_TRUE(bootOutbox(before));
  DeviceEventRecord created;
  expectEnqueue(OutboxStore::EnqueueResult::Queued,
                before.enqueue(DeviceEventKind::InventoryReceive, String("  TPS7A2033PDBVR  "), -4, &created,
                               String("Texas Instruments"), String("MPN123"), String("Name 1")));
  TEST_ASSERT_EQUAL_STRING("TPS7A2033PDBVR", created.code.c_str());

  host_nvs::reboot();
  OutboxStore after;
  TEST_ASSERT_TRUE(bootOutbox(after));
  DeviceEventRecord event;
  TEST_ASSERT_TRUE(after.peek(0, event));
  TEST_ASSERT_TRUE(event.kind == DeviceEventKind::InventoryReceive);
  TEST_ASSERT_EQUAL_INT(-4, event.value);
  TEST_ASSERT_EQUAL_STRING("TPS7A2033PDBVR", event.code.c_str());
  TEST_ASSERT_EQUAL_STRING("Texas Instruments", event.manufacturer.c_str());
  TEST_ASSERT_EQUAL_STRING("MPN123", event.manufacturerPartNumber.c_str());
  TEST_ASSERT_EQUAL_STRING("Name 1", event.encodedPartName.c_str());
  // The event id is what the desktop uses to deduplicate, so it must not change across a reboot.
  TEST_ASSERT_EQUAL_STRING(created.eventId.c_str(), event.eventId.c_str());
}

void test_host_outbox_truncates_fields_to_their_storage_limits() {
  OutboxStore outbox;
  TEST_ASSERT_TRUE(bootOutbox(outbox));
  String longManufacturer;
  for (int index = 0; index < 60; ++index) longManufacturer += 'M';
  expectEnqueue(OutboxStore::EnqueueResult::Queued,
                outbox.enqueue(DeviceEventKind::InventoryAdjust, String("X1"), 1, nullptr, longManufacturer));

  DeviceEventRecord event;
  TEST_ASSERT_TRUE(outbox.peek(0, event));
  // The manufacturer slot holds 48 bytes including the terminator: 47 characters survive.
  TEST_ASSERT_EQUAL_UINT32(47, event.manufacturer.length());
}

void test_host_outbox_rejects_invalid_scans_before_any_write() {
  OutboxStore outbox;
  TEST_ASSERT_TRUE(bootOutbox(outbox));
  // Every write fails, so a result other than Rejected would mean the store touched NVS first.
  host_nvs::faults().failWrites = true;
  expectEnqueue(OutboxStore::EnqueueResult::Rejected, adjust(outbox, "", 1));
  expectEnqueue(OutboxStore::EnqueueResult::Rejected, adjust(outbox, "   ", 1));
  expectEnqueue(OutboxStore::EnqueueResult::Rejected, adjust(outbox, "X1", 0));
  String tooLong;
  for (int index = 0; index < 65; ++index) tooLong += 'A';
  expectEnqueue(OutboxStore::EnqueueResult::Rejected, adjust(outbox, tooLong.c_str(), 1));
  host_nvs::faults() = host_nvs::Faults{};

  TEST_ASSERT_EQUAL_UINT32(0, outbox.size());
  String longest;
  for (int index = 0; index < 64; ++index) longest += 'A';
  expectEnqueue(OutboxStore::EnqueueResult::Queued, adjust(outbox, longest.c_str(), 1));
}

void test_host_outbox_reports_full_queue_before_storage_errors() {
  OutboxStore outbox;
  TEST_ASSERT_TRUE(bootOutbox(outbox));
  for (int index = 0; index < 16; ++index) {
    char code[16];
    snprintf(code, sizeof(code), "Q%02u", static_cast<unsigned>(index));
    expectEnqueue(OutboxStore::EnqueueResult::Queued, adjust(outbox, code, 1));
  }
  TEST_ASSERT_TRUE(outbox.full());

  host_nvs::faults().failWrites = true;
  expectEnqueue(OutboxStore::EnqueueResult::QueueFull, adjust(outbox, "Q16", 1));
  host_nvs::faults() = host_nvs::Faults{};
  TEST_ASSERT_EQUAL_UINT32(16, outbox.size());
}

void test_host_outbox_slot_write_failure_leaves_queue_unchanged() {
  OutboxStore outbox;
  TEST_ASSERT_TRUE(bootOutbox(outbox));
  expectEnqueue(OutboxStore::EnqueueResult::Queued, adjust(outbox, "A", 1));

  host_nvs::faults().writesBeforePowerCut = 0;
  expectEnqueue(OutboxStore::EnqueueResult::StorageFailed, adjust(outbox, "B", 1));
  // Nothing was committed, so the sequence number was not used up.
  TEST_ASSERT_EQUAL_UINT32(1, outbox.size());
  TEST_ASSERT_EQUAL_UINT32(2, outbox.nextSequence());

  host_nvs::reboot();
  OutboxStore after;
  TEST_ASSERT_TRUE(bootOutbox(after));
  const char* const codes[] = {"A"};
  expectQueueCodes(after, codes, 1);
  DeviceEventRecord created;
  expectEnqueue(OutboxStore::EnqueueResult::Queued, adjust(after, "B", 1, &created));
  TEST_ASSERT_EQUAL_UINT32(2, created.sequence);
}

void test_host_outbox_header_write_failure_keeps_new_event_out() {
  OutboxStore outbox;
  TEST_ASSERT_TRUE(bootOutbox(outbox));
  expectEnqueue(OutboxStore::EnqueueResult::Queued, adjust(outbox, "A", 1));

  // The slot write for "B" succeeds and the header write after it does not: the
  // power is cut between the two. The header is the commit point, so "B" must
  // not appear in the queue after the reboot.
  host_nvs::faults().writesBeforePowerCut = 1;
  expectEnqueue(OutboxStore::EnqueueResult::StorageFailed, adjust(outbox, "B", 1));
  TEST_ASSERT_EQUAL_UINT32(1, outbox.size());

  host_nvs::reboot();
  OutboxStore after;
  TEST_ASSERT_TRUE(bootOutbox(after));
  const char* const codes[] = {"A"};
  expectQueueCodes(after, codes, 1);
  TEST_ASSERT_EQUAL_UINT32(2, after.nextSequence());

  // The orphaned slot is reclaimed by the next scan, which then sits where "B" was.
  DeviceEventRecord created;
  expectEnqueue(OutboxStore::EnqueueResult::Queued, adjust(after, "C", 1, &created));
  TEST_ASSERT_EQUAL_UINT32(2, created.sequence);
  const char* const next[] = {"A", "C"};
  expectQueueCodes(after, next, 2);
}

void test_host_outbox_power_cut_during_pop_keeps_event_queued() {
  OutboxStore outbox;
  TEST_ASSERT_TRUE(bootOutbox(outbox));
  expectEnqueue(OutboxStore::EnqueueResult::Queued, adjust(outbox, "A", 1));
  expectEnqueue(OutboxStore::EnqueueResult::Queued, adjust(outbox, "B", 1));

  host_nvs::faults().writesBeforePowerCut = 0;
  TEST_ASSERT_FALSE(outbox.pop());
  TEST_ASSERT_EQUAL_UINT32(2, outbox.size());

  host_nvs::reboot();
  OutboxStore after;
  TEST_ASSERT_TRUE(bootOutbox(after));
  const char* const codes[] = {"A", "B"};
  expectQueueCodes(after, codes, 2);
}

void test_host_outbox_committed_pop_is_not_redelivered_after_power_cut() {
  OutboxStore outbox;
  TEST_ASSERT_TRUE(bootOutbox(outbox));
  expectEnqueue(OutboxStore::EnqueueResult::Queued, adjust(outbox, "A", 1));
  expectEnqueue(OutboxStore::EnqueueResult::Queued, adjust(outbox, "B", 1));

  // The header write commits the pop; the slot erase after it is cut off.
  host_nvs::faults().writesBeforePowerCut = 1;
  DeviceEventRecord popped;
  TEST_ASSERT_TRUE(outbox.pop(&popped));
  TEST_ASSERT_EQUAL_STRING("A", popped.code.c_str());

  host_nvs::reboot();
  OutboxStore after;
  TEST_ASSERT_TRUE(bootOutbox(after));
  const char* const codes[] = {"B"};
  expectQueueCodes(after, codes, 1);
  // The payload of "A" is left behind, unreferenced; that is safe by design.
  TEST_ASSERT_TRUE(storedKey("e0"));
}

void test_host_outbox_write_error_during_pop_keeps_event_queued() {
  OutboxStore outbox;
  TEST_ASSERT_TRUE(bootOutbox(outbox));
  expectEnqueue(OutboxStore::EnqueueResult::Queued, adjust(outbox, "A", 1));

  host_nvs::faults().failWrites = true;
  TEST_ASSERT_FALSE(outbox.pop());
  host_nvs::faults() = host_nvs::Faults{};
  TEST_ASSERT_EQUAL_UINT32(1, outbox.size());
  TEST_ASSERT_EQUAL_STRING("A", codeAt(outbox, 0).c_str());
}

void test_host_outbox_event_ids_are_not_reused_after_drain() {
  OutboxStore before;
  TEST_ASSERT_TRUE(bootOutbox(before));
  expectEnqueue(OutboxStore::EnqueueResult::Queued, adjust(before, "A", 1));
  expectEnqueue(OutboxStore::EnqueueResult::Queued, adjust(before, "B", 1));
  DeviceEventRecord first;
  DeviceEventRecord second;
  TEST_ASSERT_TRUE(before.pop(&first));
  TEST_ASSERT_TRUE(before.pop(&second));
  TEST_ASSERT_TRUE(before.empty());

  // After the queue drains and the device reboots, new scans must get fresh ids.
  host_nvs::reboot();
  OutboxStore after;
  TEST_ASSERT_TRUE(bootOutbox(after));
  DeviceEventRecord created;
  expectEnqueue(OutboxStore::EnqueueResult::Queued, adjust(after, "C", 1, &created));
  TEST_ASSERT_EQUAL_UINT32(3, created.sequence);
  TEST_ASSERT_TRUE(strcmp(created.eventId.c_str(), first.eventId.c_str()) != 0);
  TEST_ASSERT_TRUE(strcmp(created.eventId.c_str(), second.eventId.c_str()) != 0);
}

void test_host_outbox_wrap_around_keeps_order_across_reboot() {
  OutboxStore outbox;
  TEST_ASSERT_TRUE(bootOutbox(outbox));
  const char* const firstBatch[] = {"E01", "E02", "E03", "E04", "E05", "E06", "E07", "E08",
                                    "E09", "E10", "E11", "E12", "E13", "E14", "E15", "E16"};
  for (const char* code : firstBatch) expectEnqueue(OutboxStore::EnqueueResult::Queued, adjust(outbox, code, 1));
  for (int index = 0; index < 10; ++index) TEST_ASSERT_TRUE(outbox.pop());

  // Six more scans reuse the slots at the start of the ring.
  const char* const secondBatch[] = {"F01", "F02", "F03", "F04", "F05", "F06"};
  for (const char* code : secondBatch) expectEnqueue(OutboxStore::EnqueueResult::Queued, adjust(outbox, code, 1));

  host_nvs::reboot();
  OutboxStore after;
  TEST_ASSERT_TRUE(bootOutbox(after));
  const char* const expected[] = {"E11", "E12", "E13", "E14", "E15", "E16", "F01", "F02", "F03", "F04", "F05", "F06"};
  expectQueueCodes(after, expected, 12);
  for (std::size_t index = 0; index < 12; ++index) {
    DeviceEventRecord event;
    TEST_ASSERT_TRUE(after.peek(index, event));
    TEST_ASSERT_EQUAL_UINT32(11 + index, event.sequence);
  }
}

void test_host_outbox_lost_record_blocks_scans_until_repaired() {
  OutboxStore outbox;
  TEST_ASSERT_TRUE(bootOutbox(outbox));
  expectEnqueue(OutboxStore::EnqueueResult::Queued, adjust(outbox, "A", 1));
  expectEnqueue(OutboxStore::EnqueueResult::Queued, adjust(outbox, "B", 1));
  expectEnqueue(OutboxStore::EnqueueResult::Queued, adjust(outbox, "C", 1));

  // Simulate a record that disappeared from NVS while the header still counts it.
  Preferences prefs;
  TEST_ASSERT_TRUE(prefs.begin(kNamespace));
  TEST_ASSERT_TRUE(prefs.remove("e1"));

  host_nvs::reboot();
  OutboxStore after;
  TEST_ASSERT_FALSE(bootOutbox(after));
  TEST_ASSERT_TRUE(after.unreadable());
  // A scan must never be reported as queued while its queue cannot be read.
  expectEnqueue(OutboxStore::EnqueueResult::StorageFailed, adjust(after, "D", 1));

  std::size_t salvaged = 0;
  std::size_t dropped = 0;
  TEST_ASSERT_TRUE(after.repair(salvaged, dropped) == OutboxStore::RepairResult::Repaired);
  TEST_ASSERT_EQUAL_UINT32(2, salvaged);
  TEST_ASSERT_EQUAL_UINT32(1, dropped);
  TEST_ASSERT_FALSE(after.unreadable());
  const char* const repaired[] = {"A", "C"};
  expectQueueCodes(after, repaired, 2);
  expectEnqueue(OutboxStore::EnqueueResult::Queued, adjust(after, "D", 1));

  host_nvs::reboot();
  OutboxStore rebooted;
  TEST_ASSERT_TRUE(bootOutbox(rebooted));
  const char* const persisted[] = {"A", "C", "D"};
  expectQueueCodes(rebooted, persisted, 3);
}

void test_host_outbox_repair_of_healthy_queue_reports_nothing_to_repair() {
  OutboxStore outbox;
  TEST_ASSERT_TRUE(bootOutbox(outbox));
  expectEnqueue(OutboxStore::EnqueueResult::Queued, adjust(outbox, "A", 1));

  host_nvs::reboot();
  OutboxStore after;
  TEST_ASSERT_TRUE(bootOutbox(after));
  std::size_t salvaged = 0;
  std::size_t dropped = 0;
  TEST_ASSERT_TRUE(after.repair(salvaged, dropped) == OutboxStore::RepairResult::NothingToRepair);
  TEST_ASSERT_EQUAL_UINT32(1, after.size());
}

void test_host_outbox_namespace_too_long_refuses_scans() {
  OutboxStore outbox;
  // Seventeen characters: NVS rejects the namespace, so nothing can be stored.
  TEST_ASSERT_FALSE(outbox.begin("inv_outbox_long_1", kDevice));
  expectEnqueue(OutboxStore::EnqueueResult::StorageFailed, adjust(outbox, "A", 1));
  TEST_ASSERT_EQUAL_UINT32(0, outbox.freeStorageEntries());
}

void test_host_outbox_unrecognised_legacy_blob_is_never_destroyed() {
  Preferences prefs;
  TEST_ASSERT_TRUE(prefs.begin(kNamespace));
  const std::uint8_t unknown[7] = {1, 2, 3, 4, 5, 6, 7};
  TEST_ASSERT_EQUAL_UINT32(sizeof(unknown), prefs.putBytes("state", unknown, sizeof(unknown)));

  OutboxStore outbox;
  TEST_ASSERT_FALSE(bootOutbox(outbox));
  expectEnqueue(OutboxStore::EnqueueResult::StorageFailed, adjust(outbox, "A", 1));
  // The blob may hold the only copy of a queue, so it is kept and no new queue is written over it.
  TEST_ASSERT_TRUE(storedKey("state"));
  TEST_ASSERT_FALSE(storedKey("hdr"));
}

void test_host_outbox_truncated_header_is_never_overwritten() {
  Preferences prefs;
  TEST_ASSERT_TRUE(prefs.begin(kNamespace));
  const std::uint8_t damaged[1] = {0x5A};
  TEST_ASSERT_EQUAL_UINT32(sizeof(damaged), prefs.putBytes("hdr", damaged, sizeof(damaged)));

  OutboxStore outbox;
  TEST_ASSERT_FALSE(bootOutbox(outbox));
  expectEnqueue(OutboxStore::EnqueueResult::StorageFailed, adjust(outbox, "A", 1));
  // Clearing or rewriting the namespace here would destroy whatever it still holds.
  TEST_ASSERT_TRUE(prefs.begin(kNamespace, true));
  TEST_ASSERT_EQUAL_UINT32(sizeof(damaged), prefs.getBytesLength("hdr"));
}

// Queues A, B and C, then removes the record of B while the header still counts it. Returns with the
// device rebooted, so the queue is unreadable and no fault is armed.
void buildDamagedQueue() {
  OutboxStore outbox;
  TEST_ASSERT_TRUE(bootOutbox(outbox));
  expectEnqueue(OutboxStore::EnqueueResult::Queued, adjust(outbox, "A", 1));
  expectEnqueue(OutboxStore::EnqueueResult::Queued, adjust(outbox, "B", 1));
  expectEnqueue(OutboxStore::EnqueueResult::Queued, adjust(outbox, "C", 1));
  Preferences prefs;
  TEST_ASSERT_TRUE(prefs.begin(kNamespace));
  TEST_ASSERT_TRUE(prefs.remove("e1"));
  host_nvs::reboot();
}

void test_host_outbox_power_cut_during_repair_does_not_duplicate_events() {
  buildDamagedQueue();
  OutboxStore damaged;
  TEST_ASSERT_FALSE(bootOutbox(damaged));
  // Two salvage writes succeed, then the power is cut before the header commit. The old header still
  // points at the old slots, which repair() did not touch, so the queue is unreadable, not wrong.
  host_nvs::faults().writesBeforePowerCut = 2;
  std::size_t salvaged = 0;
  std::size_t dropped = 0;
  TEST_ASSERT_TRUE(damaged.repair(salvaged, dropped) == OutboxStore::RepairResult::StorageFailed);

  host_nvs::reboot();
  OutboxStore rebooted;
  TEST_ASSERT_FALSE(bootOutbox(rebooted));
  TEST_ASSERT_TRUE(rebooted.unreadable());

  // A second repair completes the job, and each salvaged event is queued exactly once.
  TEST_ASSERT_TRUE(rebooted.repair(salvaged, dropped) == OutboxStore::RepairResult::Repaired);
  const char* const once[] = {"A", "C"};
  expectQueueCodes(rebooted, once, 2);
  host_nvs::reboot();
  OutboxStore after;
  TEST_ASSERT_TRUE(bootOutbox(after));
  expectQueueCodes(after, once, 2);
}

void test_host_outbox_repair_survives_a_power_cut_at_every_write() {
  // Cuts the power after each write that repair() makes, from the first to past the last. After the
  // reboot the queue either still needs repair, or it holds A and C exactly once. Never a lost or a
  // repeated event.
  const char* const once[] = {"A", "C"};
  for (long long cutAfter = 0; cutAfter <= 40; ++cutAfter) {
    host_nvs::reset();
    buildDamagedQueue();
    OutboxStore damaged;
    TEST_ASSERT_FALSE(bootOutbox(damaged));
    host_nvs::faults().writesBeforePowerCut = cutAfter;
    std::size_t salvaged = 0;
    std::size_t dropped = 0;
    damaged.repair(salvaged, dropped);

    host_nvs::reboot();
    OutboxStore rebooted;
    if (!bootOutbox(rebooted)) {
      TEST_ASSERT_TRUE(rebooted.unreadable());
      TEST_ASSERT_TRUE(rebooted.repair(salvaged, dropped) == OutboxStore::RepairResult::Repaired);
    }
    expectQueueCodes(rebooted, once, 2);
  }
}

void runHostOutboxDurabilityTests() {
  RUN_TEST(test_host_outbox_fresh_device_starts_empty);
  RUN_TEST(test_host_outbox_survives_reboot_in_order);
  RUN_TEST(test_host_outbox_low_sequence_of_empty_queue_moves_past_stored_ids);
  RUN_TEST(test_host_outbox_queued_scans_keep_their_sequence_at_boot);
  RUN_TEST(test_host_outbox_persists_every_event_field);
  RUN_TEST(test_host_outbox_truncates_fields_to_their_storage_limits);
  RUN_TEST(test_host_outbox_rejects_invalid_scans_before_any_write);
  RUN_TEST(test_host_outbox_reports_full_queue_before_storage_errors);
  RUN_TEST(test_host_outbox_slot_write_failure_leaves_queue_unchanged);
  RUN_TEST(test_host_outbox_header_write_failure_keeps_new_event_out);
  RUN_TEST(test_host_outbox_power_cut_during_pop_keeps_event_queued);
  RUN_TEST(test_host_outbox_committed_pop_is_not_redelivered_after_power_cut);
  RUN_TEST(test_host_outbox_write_error_during_pop_keeps_event_queued);
  RUN_TEST(test_host_outbox_event_ids_are_not_reused_after_drain);
  RUN_TEST(test_host_outbox_wrap_around_keeps_order_across_reboot);
  RUN_TEST(test_host_outbox_lost_record_blocks_scans_until_repaired);
  RUN_TEST(test_host_outbox_repair_of_healthy_queue_reports_nothing_to_repair);
  RUN_TEST(test_host_outbox_namespace_too_long_refuses_scans);
  RUN_TEST(test_host_outbox_unrecognised_legacy_blob_is_never_destroyed);
  RUN_TEST(test_host_outbox_truncated_header_is_never_overwritten);
  RUN_TEST(test_host_outbox_power_cut_during_repair_does_not_duplicate_events);
  RUN_TEST(test_host_outbox_repair_survives_a_power_cut_at_every_write);
}
