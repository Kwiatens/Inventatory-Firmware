// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

// Checks the NVS stand-in itself. The durability tests trust it to behave like
// the ESP32 Preferences API, so these tests pin down the parts they depend on.
#include <unity.h>

#include <cstdint>

#include "Preferences.h"

namespace {

const std::uint8_t kPayload[4] = {0xDE, 0xAD, 0xBE, 0xEF};

}  // namespace

void test_nvs_shim_enforces_namespace_length() {
  Preferences prefs;
  TEST_ASSERT_TRUE(prefs.begin("123456789012345"));    // 15 characters: the NVS limit.
  TEST_ASSERT_FALSE(prefs.begin("1234567890123456"));  // 16 characters: KEY_TOO_LONG on the device.
  TEST_ASSERT_FALSE(prefs.begin(""));
  TEST_ASSERT_FALSE(prefs.begin(nullptr));
}

void test_nvs_shim_read_only_begin_needs_existing_namespace() {
  Preferences prefs;
  TEST_ASSERT_FALSE(prefs.begin("inv_outbox", true));
  TEST_ASSERT_TRUE(prefs.begin("inv_outbox", false));
  TEST_ASSERT_EQUAL_UINT32(sizeof(kPayload), prefs.putBytes("hdr", kPayload, sizeof(kPayload)));
  prefs.end();

  TEST_ASSERT_TRUE(prefs.begin("inv_outbox", true));
  // A read-only handle cannot write.
  TEST_ASSERT_EQUAL_UINT32(0, prefs.putBytes("hdr", kPayload, sizeof(kPayload)));
  TEST_ASSERT_FALSE(prefs.remove("hdr"));
  TEST_ASSERT_FALSE(prefs.clear());
}

void test_nvs_shim_enforces_key_length() {
  Preferences prefs;
  TEST_ASSERT_TRUE(prefs.begin("inv_outbox"));
  TEST_ASSERT_EQUAL_UINT32(0, prefs.putBytes("123456789012345X", kPayload, sizeof(kPayload)));
  TEST_ASSERT_FALSE(prefs.isKey("123456789012345X"));
  TEST_ASSERT_EQUAL_UINT32(sizeof(kPayload), prefs.putBytes("123456789012345", kPayload, sizeof(kPayload)));
  TEST_ASSERT_TRUE(prefs.isKey("123456789012345"));
}

void test_nvs_shim_length_query_is_zero_when_buffer_is_too_small() {
  Preferences prefs;
  TEST_ASSERT_TRUE(prefs.begin("inv_outbox"));
  TEST_ASSERT_EQUAL_UINT32(0, prefs.getBytesLength("missing"));
  TEST_ASSERT_EQUAL_UINT32(sizeof(kPayload), prefs.putBytes("blob", kPayload, sizeof(kPayload)));
  TEST_ASSERT_EQUAL_UINT32(sizeof(kPayload), prefs.getBytesLength("blob"));

  std::uint8_t small[3] = {};
  TEST_ASSERT_EQUAL_UINT32(0, prefs.getBytes("blob", small, sizeof(small)));

  std::uint8_t exact[4] = {};
  TEST_ASSERT_EQUAL_UINT32(sizeof(exact), prefs.getBytes("blob", exact, sizeof(exact)));
  TEST_ASSERT_EQUAL_HEX8_ARRAY(kPayload, exact, sizeof(exact));
}

void test_nvs_shim_power_cut_fails_every_later_write() {
  Preferences prefs;
  TEST_ASSERT_TRUE(prefs.begin("inv_outbox"));
  TEST_ASSERT_TRUE(prefs.putBytes("a", kPayload, sizeof(kPayload)) == sizeof(kPayload));
  host_nvs::faults().writesBeforePowerCut = 2;
  TEST_ASSERT_EQUAL_UINT32(sizeof(kPayload), prefs.putBytes("b", kPayload, sizeof(kPayload)));
  TEST_ASSERT_EQUAL_UINT32(sizeof(kPayload), prefs.putBytes("c", kPayload, sizeof(kPayload)));

  // The budget is spent: the power is cut, so nothing else reaches storage.
  TEST_ASSERT_EQUAL_UINT32(0, prefs.putBytes("d", kPayload, sizeof(kPayload)));
  TEST_ASSERT_FALSE(prefs.remove("a"));
  TEST_ASSERT_FALSE(prefs.clear());
  TEST_ASSERT_TRUE(prefs.isKey("a"));
  TEST_ASSERT_FALSE(prefs.isKey("d"));
}

void test_nvs_shim_reboot_keeps_stored_values() {
  Preferences prefs;
  TEST_ASSERT_TRUE(prefs.begin("inv_outbox"));
  TEST_ASSERT_EQUAL_UINT32(sizeof(kPayload), prefs.putBytes("keep", kPayload, sizeof(kPayload)));
  host_nvs::faults().failWrites = true;
  TEST_ASSERT_EQUAL_UINT32(0, prefs.putBytes("lost", kPayload, sizeof(kPayload)));

  host_nvs::reboot();
  TEST_ASSERT_TRUE(prefs.begin("inv_outbox"));
  std::uint8_t read[4] = {};
  TEST_ASSERT_EQUAL_UINT32(sizeof(read), prefs.getBytes("keep", read, sizeof(read)));
  TEST_ASSERT_EQUAL_HEX8_ARRAY(kPayload, read, sizeof(read));
  TEST_ASSERT_FALSE(prefs.isKey("lost"));
  TEST_ASSERT_EQUAL_UINT32(sizeof(kPayload), prefs.putBytes("after", kPayload, sizeof(kPayload)));
}

void test_nvs_shim_namespaces_are_isolated() {
  Preferences first;
  Preferences second;
  TEST_ASSERT_TRUE(first.begin("inv_outbox"));
  TEST_ASSERT_TRUE(second.begin("inv_setup"));
  TEST_ASSERT_EQUAL_UINT32(sizeof(kPayload), first.putBytes("hdr", kPayload, sizeof(kPayload)));
  TEST_ASSERT_FALSE(second.isKey("hdr"));
  TEST_ASSERT_EQUAL_UINT32(0, second.getBytesLength("hdr"));
}

void test_nvs_shim_free_entries_track_writes() {
  Preferences prefs;
  TEST_ASSERT_TRUE(prefs.begin("inv_outbox"));
  const std::size_t before = prefs.freeEntries();
  TEST_ASSERT_EQUAL_UINT32(sizeof(kPayload), prefs.putBytes("one", kPayload, sizeof(kPayload)));
  TEST_ASSERT_EQUAL_UINT32(sizeof(kPayload), prefs.putBytes("two", kPayload, sizeof(kPayload)));
  TEST_ASSERT_EQUAL_UINT32(before - 2, prefs.freeEntries());
  TEST_ASSERT_TRUE(prefs.remove("one"));
  TEST_ASSERT_EQUAL_UINT32(before - 1, prefs.freeEntries());
}
