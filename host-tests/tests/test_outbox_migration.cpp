// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

// Migration tests for the offline outbox. A device that was flashed with an older
// queue layout must keep its queued scans when it boots this firmware. The
// fixtures below are fixed byte layouts, mirrored from the blobs older firmware
// wrote, so the tests check the real upgrade path rather than what the current
// code happens to produce. A power cut is injected at every write the migration
// makes, and each cut must leave exactly the original queue after the next boot.
#include <unity.h>

#include <cstdint>
#include <cstring>

#include "Preferences.h"
#include "storage/outbox_store.h"

using namespace inventatory_scan;

void runHostOutboxMigrationTests();

void test_host_outbox_version4_queue_migrates_in_order_across_the_wrap();
void test_host_outbox_version2_queue_migrates_with_its_values();
void test_host_outbox_failed_migration_write_keeps_the_legacy_blob();
void test_host_outbox_power_cut_at_every_migration_write_keeps_the_queue();
void test_host_outbox_version4_blob_with_bad_head_is_refused_and_kept();
void test_host_outbox_version4_blob_with_wrong_magic_is_refused_and_kept();
void test_host_outbox_version4_sized_blob_of_another_version_is_refused_and_kept();

namespace {

constexpr char kNamespace[] = "inv_outbox";
constexpr char kDevice[] = "R1-TEST";
constexpr char kLegacyKey[] = "state";
constexpr uint32_t kMagic = 0x48534f51UL;
constexpr std::size_t kCapacity = 16;

// Layout of the version 4 blob: the whole queue in one 4112-byte value.
struct V4Record {
  uint32_t sequence;
  int32_t value;
  uint8_t kind;
  uint8_t reserved[3];
  char code[65];
  char manufacturer[48];
  char manufacturerPartNumber[65];
  char encodedPartName[64];
};
struct V4State {
  uint32_t magic;
  uint16_t version;
  uint16_t count;
  uint16_t head;
  uint16_t reserved;
  uint32_t nextSequence;
  V4Record records[kCapacity];
};
static_assert(sizeof(V4State) == 4112, "version 4 blob must be 4112 bytes");

// Layout of the version 2 blob: a 1680-byte value with one delta per record.
struct V2Record {
  uint32_t sequence;
  char code[96];
  int32_t delta;
};
struct V2State {
  uint32_t magic;
  uint16_t version;
  uint16_t count;
  uint16_t head;
  uint16_t reserved;
  uint32_t nextSequence;
  V2Record records[kCapacity];
};
static_assert(sizeof(V2State) == 1680, "version 2 blob must be 1680 bytes");

// Stores three version 4 records that wrap the ring: slots 14, 15 and 0.
V4State makeV4Wrapped(uint32_t nextSequence) {
  V4State blob{};
  blob.magic = kMagic;
  blob.version = 4;
  blob.count = 3;
  blob.head = 14;
  blob.nextSequence = nextSequence;
  const char* const codes[] = {"A1", "B2", "C3"};
  const int32_t values[] = {2, -1, 5};
  for (std::size_t index = 0; index < 3; ++index) {
    V4Record& record = blob.records[(14 + index) % kCapacity];
    record.sequence = static_cast<uint32_t>(37 + index);
    record.value = values[index];
    record.kind = static_cast<uint8_t>(index == 1 ? 2 : 1);  // B2 is a receive
    std::strcpy(record.code, codes[index]);
  }
  std::strcpy(blob.records[15].manufacturer, "Acme");
  std::strcpy(blob.records[15].manufacturerPartNumber, "MPN-B2");
  std::strcpy(blob.records[15].encodedPartName, "Bracket");
  return blob;
}

void storeLegacy(const void* data, std::size_t length) {
  Preferences prefs;
  TEST_ASSERT_TRUE(prefs.begin(kNamespace, false));
  TEST_ASSERT_EQUAL_UINT32(length, prefs.putBytes(kLegacyKey, data, length));
}

bool storedKey(const char* key) {
  Preferences prefs;
  TEST_ASSERT_TRUE(prefs.begin(kNamespace, true));
  return prefs.isKey(key);
}

bool bootOutbox(OutboxStore& outbox) { return outbox.begin(kNamespace, kDevice) && outbox.load(); }

void expectCodes(const OutboxStore& outbox, const char* const* codes, std::size_t count) {
  TEST_ASSERT_EQUAL_UINT32(count, outbox.size());
  for (std::size_t index = 0; index < count; ++index) {
    DeviceEventRecord event;
    TEST_ASSERT_TRUE(outbox.peek(index, event));
    TEST_ASSERT_EQUAL_STRING(codes[index], event.code.c_str());
  }
}

}  // namespace

void test_host_outbox_version4_queue_migrates_in_order_across_the_wrap() {
  const V4State blob = makeV4Wrapped(40);
  storeLegacy(&blob, sizeof(blob));

  OutboxStore outbox;
  TEST_ASSERT_TRUE(bootOutbox(outbox));
  const char* const codes[] = {"A1", "B2", "C3"};
  expectCodes(outbox, codes, 3);

  DeviceEventRecord second;
  TEST_ASSERT_TRUE(outbox.peek(1, second));
  TEST_ASSERT_EQUAL_INT(-1, second.value);
  TEST_ASSERT_TRUE(second.kind == DeviceEventKind::InventoryReceive);
  TEST_ASSERT_EQUAL_STRING("Acme", second.manufacturer.c_str());
  TEST_ASSERT_EQUAL_STRING("MPN-B2", second.manufacturerPartNumber.c_str());
  TEST_ASSERT_EQUAL_STRING("Bracket", second.encodedPartName.c_str());
  // Event ids are what the desktop deduplicates on, so the migrated ids must match the originals.
  TEST_ASSERT_EQUAL_STRING("R1-TEST-38", second.eventId.c_str());
  TEST_ASSERT_EQUAL_UINT32(40, outbox.nextSequence());

  // Only a committed migration removes the legacy blob.
  TEST_ASSERT_FALSE(storedKey(kLegacyKey));
  TEST_ASSERT_TRUE(storedKey("hdr"));

  host_nvs::reboot();
  OutboxStore rebooted;
  TEST_ASSERT_TRUE(bootOutbox(rebooted));
  expectCodes(rebooted, codes, 3);
}

void test_host_outbox_version2_queue_migrates_with_its_values() {
  V2State blob{};
  blob.magic = kMagic;
  blob.version = 2;
  blob.count = 2;
  blob.head = 0;
  blob.nextSequence = 7;
  blob.records[0].sequence = 5;
  blob.records[0].delta = -3;
  std::strcpy(blob.records[0].code, "P1");
  blob.records[1].sequence = 6;
  blob.records[1].delta = 4;
  std::strcpy(blob.records[1].code, "Q2");
  storeLegacy(&blob, sizeof(blob));

  OutboxStore outbox;
  TEST_ASSERT_TRUE(bootOutbox(outbox));
  const char* const codes[] = {"P1", "Q2"};
  expectCodes(outbox, codes, 2);
  DeviceEventRecord first;
  TEST_ASSERT_TRUE(outbox.peek(0, first));
  TEST_ASSERT_EQUAL_INT(-3, first.value);
  TEST_ASSERT_TRUE(first.kind == DeviceEventKind::InventoryAdjust);
  TEST_ASSERT_EQUAL_STRING("R1-TEST-5", first.eventId.c_str());
  TEST_ASSERT_EQUAL_UINT32(7, outbox.nextSequence());
}

void test_host_outbox_failed_migration_write_keeps_the_legacy_blob() {
  const V4State blob = makeV4Wrapped(40);
  storeLegacy(&blob, sizeof(blob));

  host_nvs::faults().failWrites = true;
  OutboxStore failed;
  TEST_ASSERT_FALSE(bootOutbox(failed));
  // The old queue is intact, so nothing is discarded and the scan is not reported as queued.
  TEST_ASSERT_FALSE(failed.unreadable());
  TEST_ASSERT_TRUE(storedKey(kLegacyKey));
  TEST_ASSERT_FALSE(storedKey("hdr"));

  host_nvs::reboot();
  OutboxStore retried;
  TEST_ASSERT_TRUE(bootOutbox(retried));
  const char* const codes[] = {"A1", "B2", "C3"};
  expectCodes(retried, codes, 3);
}

void test_host_outbox_power_cut_at_every_migration_write_keeps_the_queue() {
  // The migration writes three records, the header and removes the blob: five
  // writes. A cut is injected before each one, and after the last.
  const char* const codes[] = {"A1", "B2", "C3"};
  for (long long cutAfter = 0; cutAfter <= 6; ++cutAfter) {
    host_nvs::reset();
    const V4State blob = makeV4Wrapped(40);
    storeLegacy(&blob, sizeof(blob));

    host_nvs::faults().writesBeforePowerCut = cutAfter;
    OutboxStore interrupted;
    (void)bootOutbox(interrupted);

    host_nvs::reboot();
    OutboxStore recovered;
    TEST_ASSERT_TRUE_MESSAGE(bootOutbox(recovered), "queue must load after the cut");
    expectCodes(recovered, codes, 3);
    TEST_ASSERT_EQUAL_UINT32(40, recovered.nextSequence());
  }
}

void test_host_outbox_version4_blob_with_bad_head_is_refused_and_kept() {
  V4State blob = makeV4Wrapped(40);
  blob.head = 16;  // out of range for a 16-slot ring
  storeLegacy(&blob, sizeof(blob));

  OutboxStore outbox;
  TEST_ASSERT_FALSE(bootOutbox(outbox));
  TEST_ASSERT_TRUE(outbox.unreadable());
  TEST_ASSERT_TRUE(storedKey(kLegacyKey));
  TEST_ASSERT_FALSE(storedKey("hdr"));
}

void test_host_outbox_version4_blob_with_wrong_magic_is_refused_and_kept() {
  V4State blob = makeV4Wrapped(40);
  blob.magic = 0x12345678UL;
  storeLegacy(&blob, sizeof(blob));

  OutboxStore outbox;
  TEST_ASSERT_FALSE(bootOutbox(outbox));
  TEST_ASSERT_TRUE(outbox.unreadable());
  TEST_ASSERT_TRUE(storedKey(kLegacyKey));
  TEST_ASSERT_FALSE(storedKey("hdr"));
}

void test_host_outbox_version4_sized_blob_of_another_version_is_refused_and_kept() {
  // The right size and magic, but a version this firmware does not migrate. Only the
  // version 4 layout is migrated, so this must not be read as one.
  V4State blob = makeV4Wrapped(40);
  blob.version = 5;
  storeLegacy(&blob, sizeof(blob));

  OutboxStore outbox;
  TEST_ASSERT_FALSE(bootOutbox(outbox));
  TEST_ASSERT_TRUE(outbox.unreadable());
  TEST_ASSERT_TRUE(storedKey(kLegacyKey));
  TEST_ASSERT_FALSE(storedKey("hdr"));
}

void runHostOutboxMigrationTests() {
  RUN_TEST(test_host_outbox_version4_queue_migrates_in_order_across_the_wrap);
  RUN_TEST(test_host_outbox_version2_queue_migrates_with_its_values);
  RUN_TEST(test_host_outbox_failed_migration_write_keeps_the_legacy_blob);
  RUN_TEST(test_host_outbox_power_cut_at_every_migration_write_keeps_the_queue);
  RUN_TEST(test_host_outbox_version4_blob_with_bad_head_is_refused_and_kept);
  RUN_TEST(test_host_outbox_version4_blob_with_wrong_magic_is_refused_and_kept);
  RUN_TEST(test_host_outbox_version4_sized_blob_of_another_version_is_refused_and_kept);
}
