// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

// Host tests for the provisioning record and the transport replay counter
// (AGENTS.md invariant 3, transport v1 section 8). The counter is what lets the
// desktop refuse a replayed request, so these tests check the properties that
// matter across reboots and power loss: counters only go up, a reset may skip
// values but never reuse one, and a missing or half-written record never looks
// complete.
#include <unity.h>

#include <cstdint>

#include "Preferences.h"
#include "setup/setup_state_store.h"

using namespace inventatory_scan;

void runHostSetupStateTests();

void test_host_setup_counter_is_strictly_increasing_across_blocks();
void test_host_setup_counter_skips_but_never_reuses_after_reboot();
void test_host_setup_counter_power_cut_hands_out_nothing();
void test_host_setup_counter_missing_key_fails_closed();
void test_host_setup_save_then_load_round_trips();
void test_host_setup_incomplete_record_is_not_loaded();
void test_host_setup_rejects_bad_token_and_oversized_fields();
void test_host_setup_power_cut_during_save_never_looks_complete();
void test_host_setup_clear_removes_credentials_and_counter();

namespace {

constexpr char kToken[] = "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";

ProvisionedSettings sampleSettings() {
  ProvisionedSettings settings;
  settings.wifiSsid = "Shop-Net";
  settings.wifiPassword = "correct horse";
  settings.deviceToken = kToken;
  return settings;
}

// Reserves one counter and fails the test if the store refuses.
uint64_t reserveOne(SetupStateStore& store) {
  uint64_t counter = 0;
  TEST_ASSERT_TRUE(store.reserveTransportCounter(counter));
  return counter;
}

String repeated(char value, std::size_t count) {
  String out;
  for (std::size_t index = 0; index < count; ++index) out += value;
  return out;
}

void storeProvisioned() {
  SetupStateStore store;
  TEST_ASSERT_TRUE(store.begin());
  TEST_ASSERT_TRUE(store.save(sampleSettings()));
}

}  // namespace

void test_host_setup_counter_is_strictly_increasing_across_blocks() {
  storeProvisioned();
  SetupStateStore store;
  TEST_ASSERT_TRUE(store.begin());

  // A hundred reservations cross several 32-counter blocks.
  uint64_t previous = 0;
  for (int index = 0; index < 100; ++index) {
    const uint64_t counter = reserveOne(store);
    TEST_ASSERT_TRUE(counter > previous);
    previous = counter;
  }
  TEST_ASSERT_EQUAL_UINT64(100, previous);
}

void test_host_setup_counter_skips_but_never_reuses_after_reboot() {
  storeProvisioned();
  SetupStateStore before;
  TEST_ASSERT_TRUE(before.begin());
  uint64_t last = 0;
  for (int index = 0; index < 100; ++index) last = reserveOne(before);

  // After a reboot, the next block starts from the persisted ceiling (128), so the
  // counters 101 to 128 are skipped. They were never sent, so skipping is safe.
  host_nvs::reboot();
  SetupStateStore after;
  TEST_ASSERT_TRUE(after.begin());
  const uint64_t resumed = reserveOne(after);
  TEST_ASSERT_TRUE(resumed > last);
  TEST_ASSERT_EQUAL_UINT64(129, resumed);
}

void test_host_setup_counter_power_cut_hands_out_nothing() {
  storeProvisioned();
  SetupStateStore store;
  TEST_ASSERT_TRUE(store.begin());

  // The first reservation writes a block ceiling. A power cut on that write must not
  // hand out a counter, because the desktop could then see a value that was never recorded.
  host_nvs::faults().writesBeforePowerCut = 0;
  uint64_t counter = 999;
  TEST_ASSERT_FALSE(store.reserveTransportCounter(counter));
  TEST_ASSERT_EQUAL_UINT64(999, counter);

  host_nvs::reboot();
  SetupStateStore rebooted;
  TEST_ASSERT_TRUE(rebooted.begin());
  TEST_ASSERT_TRUE(reserveOne(rebooted) >= 1U);
}

void test_host_setup_counter_missing_key_fails_closed() {
  storeProvisioned();
  Preferences raw;
  TEST_ASSERT_TRUE(raw.begin("inv_setup", false));
  TEST_ASSERT_TRUE(raw.remove("request_counter"));

  SetupStateStore store;
  TEST_ASSERT_TRUE(store.begin());
  // Defaulting to zero would restart the counter and reuse values the desktop has seen.
  uint64_t counter = 7;
  TEST_ASSERT_FALSE(store.reserveTransportCounter(counter));
  TEST_ASSERT_EQUAL_UINT64(7, counter);
}

void test_host_setup_save_then_load_round_trips() {
  storeProvisioned();
  SetupStateStore store;
  TEST_ASSERT_TRUE(store.begin());
  TEST_ASSERT_TRUE(store.complete());

  ProvisionedSettings loaded;
  TEST_ASSERT_TRUE(store.load(loaded));
  TEST_ASSERT_EQUAL_STRING("Shop-Net", loaded.wifiSsid.c_str());
  TEST_ASSERT_EQUAL_STRING("correct horse", loaded.wifiPassword.c_str());
  TEST_ASSERT_EQUAL_STRING(kToken, loaded.deviceToken.c_str());
}

void test_host_setup_incomplete_record_is_not_loaded() {
  // Staged but not committed, as after Wi-Fi joins and before the runtime starts.
  SetupStateStore store;
  TEST_ASSERT_TRUE(store.begin());
  TEST_ASSERT_TRUE(store.savePending(sampleSettings()));

  SetupStateStore after;
  TEST_ASSERT_TRUE(after.begin());
  TEST_ASSERT_FALSE(after.complete());
  ProvisionedSettings loaded;
  TEST_ASSERT_FALSE(after.load(loaded));
}

void test_host_setup_rejects_bad_token_and_oversized_fields() {
  SetupStateStore store;
  TEST_ASSERT_TRUE(store.begin());

  ProvisionedSettings badToken = sampleSettings();
  badToken.deviceToken = "00";
  TEST_ASSERT_FALSE(store.savePending(badToken));

  ProvisionedSettings longSsid = sampleSettings();
  longSsid.wifiSsid = repeated('a', 33);
  TEST_ASSERT_FALSE(store.savePending(longSsid));

  ProvisionedSettings longPassword = sampleSettings();
  longPassword.wifiPassword = repeated('p', 64);
  TEST_ASSERT_FALSE(store.savePending(longPassword));

  // The limits themselves are accepted, and an empty password is an open network.
  ProvisionedSettings limits = sampleSettings();
  limits.wifiSsid = repeated('s', 32);
  limits.wifiPassword = repeated('p', 63);
  TEST_ASSERT_TRUE(store.savePending(limits));
  ProvisionedSettings open = sampleSettings();
  open.wifiPassword = "";
  TEST_ASSERT_TRUE(store.savePending(open));
}

void test_host_setup_power_cut_during_save_never_looks_complete() {
  // A save makes nine writes (eight staged, then the commit). A cut before each of
  // them, and after the last, must leave a device that either loads the full record
  // or stays in setup.
  for (long long cutAfter = 0; cutAfter <= 10; ++cutAfter) {
    host_nvs::reset();
    host_nvs::faults().writesBeforePowerCut = cutAfter;
    SetupStateStore interrupted;
    TEST_ASSERT_TRUE(interrupted.begin());
    (void)interrupted.save(sampleSettings());

    host_nvs::reboot();
    SetupStateStore rebooted;
    TEST_ASSERT_TRUE(rebooted.begin());
    ProvisionedSettings loaded;
    if (rebooted.load(loaded)) {
      TEST_ASSERT_EQUAL_STRING("Shop-Net", loaded.wifiSsid.c_str());
      TEST_ASSERT_EQUAL_STRING("correct horse", loaded.wifiPassword.c_str());
      TEST_ASSERT_EQUAL_STRING(kToken, loaded.deviceToken.c_str());
    } else {
      TEST_ASSERT_FALSE(rebooted.complete());
    }
  }
}

void test_host_setup_clear_removes_credentials_and_counter() {
  storeProvisioned();
  SetupStateStore store;
  TEST_ASSERT_TRUE(store.begin());
  (void)reserveOne(store);
  TEST_ASSERT_TRUE(store.clear());

  SetupStateStore after;
  TEST_ASSERT_TRUE(after.begin());
  TEST_ASSERT_FALSE(after.complete());
  uint64_t counter = 0;
  TEST_ASSERT_FALSE(after.reserveTransportCounter(counter));
}

void runHostSetupStateTests() {
  RUN_TEST(test_host_setup_counter_is_strictly_increasing_across_blocks);
  RUN_TEST(test_host_setup_counter_skips_but_never_reuses_after_reboot);
  RUN_TEST(test_host_setup_counter_power_cut_hands_out_nothing);
  RUN_TEST(test_host_setup_counter_missing_key_fails_closed);
  RUN_TEST(test_host_setup_save_then_load_round_trips);
  RUN_TEST(test_host_setup_incomplete_record_is_not_loaded);
  RUN_TEST(test_host_setup_rejects_bad_token_and_oversized_fields);
  RUN_TEST(test_host_setup_power_cut_during_save_never_looks_complete);
  RUN_TEST(test_host_setup_clear_removes_credentials_and_counter);
}
