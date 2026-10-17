// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

// Host tests for the battery gauge's NVS state: it survives a reboot, a value out of range is
// rejected, and an unchanged state writes nothing.

#include <unity.h>

#include <cstdint>

#include "Preferences.h"
#include "storage/battery_state_store.h"

using namespace inventatory_scan;

void runHostBatteryStateTests();

void test_host_battery_state_survives_a_reboot() {
  BatteryGaugeState saved;
  saved.remainingUah = 432100U;
  saved.chargeMsPerPercent = 61000U;
  saved.averageLoadCentiMa = 8150U;

  BatteryStateStore before;
  TEST_ASSERT_TRUE(before.begin());
  TEST_ASSERT_TRUE(before.save(saved));

  host_nvs::reboot();
  BatteryStateStore after;
  TEST_ASSERT_TRUE(after.begin());
  BatteryGaugeState loaded;
  TEST_ASSERT_TRUE(after.load(loaded));
  TEST_ASSERT_EQUAL_UINT32(saved.remainingUah, loaded.remainingUah);
  TEST_ASSERT_EQUAL_UINT32(saved.chargeMsPerPercent, loaded.chargeMsPerPercent);
  TEST_ASSERT_EQUAL_UINT32(saved.averageLoadCentiMa, loaded.averageLoadCentiMa);
}

void test_host_battery_state_is_absent_before_the_first_save() {
  BatteryStateStore store;
  TEST_ASSERT_TRUE(store.begin());
  BatteryGaugeState loaded;
  TEST_ASSERT_FALSE(store.load(loaded));
}

void test_host_battery_state_rejects_out_of_range_values() {
  // Written directly, the way a foreign or damaged value would be.
  Preferences raw;
  TEST_ASSERT_TRUE(raw.begin("inv_battery", false));
  raw.putUInt("rem_uah", 1400000U);  // more charge than the 1300 mAh cell holds
  raw.putUInt("chg_ms_pct", 0U);
  raw.putUInt("avg_cma", 0U);
  raw.end();

  BatteryStateStore store;
  TEST_ASSERT_TRUE(store.begin());
  BatteryGaugeState loaded;
  TEST_ASSERT_FALSE(store.load(loaded));

  // A learned rate below its lower bound is refused as well.
  TEST_ASSERT_TRUE(raw.begin("inv_battery", false));
  raw.putUInt("rem_uah", 500000U);
  raw.putUInt("chg_ms_pct", 1000U);
  raw.end();
  BatteryStateStore rate;
  TEST_ASSERT_TRUE(rate.begin());
  TEST_ASSERT_FALSE(rate.load(loaded));
}

void test_host_battery_state_writes_only_changed_values() {
  BatteryStateStore store;
  TEST_ASSERT_TRUE(store.begin());
  BatteryGaugeState state;
  state.remainingUah = 500000U;
  state.averageLoadCentiMa = 8000U;
  TEST_ASSERT_TRUE(store.save(state));

  // With the next write refused, an unchanged state still succeeds: nothing needs writing.
  host_nvs::faults().writesBeforePowerCut = 0;
  TEST_ASSERT_TRUE(store.save(state));

  // A changed value needs a write, which the cut refuses.
  state.remainingUah = 499000U;
  TEST_ASSERT_FALSE(store.save(state));
  host_nvs::reboot();
}

void runHostBatteryStateTests() {
  RUN_TEST(test_host_battery_state_survives_a_reboot);
  RUN_TEST(test_host_battery_state_is_absent_before_the_first_save);
  RUN_TEST(test_host_battery_state_rejects_out_of_range_values);
  RUN_TEST(test_host_battery_state_writes_only_changed_values);
}
