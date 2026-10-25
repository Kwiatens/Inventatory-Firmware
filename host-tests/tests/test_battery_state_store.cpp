// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

// Battery gauge NVS state: survives reboot, rejects out-of-range values, skips unchanged writes.

#include <unity.h>

#include <cstdint>

#include "Preferences.h"
#include "storage/battery_state_store.h"

using namespace inventatory_scan;

void runHostBatteryStateTests();

void test_host_battery_state_survives_a_reboot() {
  BatteryGaugeState saved;
  saved.chargePpm = 432100U;
  saved.chargeMsPerPercent = 61000U;
  saved.loadScalePpm = 1150000U;
  saved.learnUsedPpm = 64000U;
  saved.learnCorrectionPpm = -7300;

  BatteryStateStore before;
  TEST_ASSERT_TRUE(before.begin());
  TEST_ASSERT_TRUE(before.save(saved));

  host_nvs::reboot();
  BatteryStateStore after;
  TEST_ASSERT_TRUE(after.begin());
  BatteryGaugeState loaded;
  TEST_ASSERT_TRUE(after.load(loaded));
  TEST_ASSERT_EQUAL_UINT32(saved.chargePpm, loaded.chargePpm);
  TEST_ASSERT_EQUAL_UINT32(saved.chargeMsPerPercent, loaded.chargeMsPerPercent);
  TEST_ASSERT_EQUAL_UINT32(saved.loadScalePpm, loaded.loadScalePpm);
  TEST_ASSERT_EQUAL_UINT32(saved.learnUsedPpm, loaded.learnUsedPpm);
  TEST_ASSERT_EQUAL_INT32(saved.learnCorrectionPpm, loaded.learnCorrectionPpm);
}

void test_host_battery_state_drops_an_out_of_range_span_only() {
  BatteryGaugeState saved;
  saved.chargePpm = 432100U;
  saved.learnUsedPpm = kBatteryLearnUsedMaxPpm + 1U;
  saved.learnCorrectionPpm = 5000;

  BatteryStateStore before;
  TEST_ASSERT_TRUE(before.begin());
  TEST_ASSERT_TRUE(before.save(saved));

  host_nvs::reboot();
  BatteryStateStore after;
  TEST_ASSERT_TRUE(after.begin());
  BatteryGaugeState loaded;
  TEST_ASSERT_TRUE(after.load(loaded));
  TEST_ASSERT_EQUAL_UINT32(saved.chargePpm, loaded.chargePpm);
  TEST_ASSERT_EQUAL_UINT32(0U, loaded.learnUsedPpm);
  TEST_ASSERT_EQUAL_INT32(0, loaded.learnCorrectionPpm);
}

void test_host_battery_state_is_absent_before_the_first_save() {
  BatteryStateStore store;
  TEST_ASSERT_TRUE(store.begin());
  BatteryGaugeState loaded;
  TEST_ASSERT_FALSE(store.load(loaded));
}

void test_host_battery_state_rejects_out_of_range_values() {
  // Written raw, as a foreign or damaged value would be.
  Preferences raw;
  TEST_ASSERT_TRUE(raw.begin("inv_battery", false));
  raw.putUInt("soc_ppm", 1000001U);  // more than a full cell
  raw.putUInt("chg_ms_pct", 0U);
  raw.putUInt("load_ppm", 0U);
  raw.end();

  BatteryStateStore store;
  TEST_ASSERT_TRUE(store.begin());
  BatteryGaugeState loaded;
  TEST_ASSERT_FALSE(store.load(loaded));

  // A learned rate below its lower bound is refused as well.
  TEST_ASSERT_TRUE(raw.begin("inv_battery", false));
  raw.putUInt("soc_ppm", 500000U);
  raw.putUInt("chg_ms_pct", 1000U);
  raw.end();
  BatteryStateStore rate;
  TEST_ASSERT_TRUE(rate.begin());
  TEST_ASSERT_FALSE(rate.load(loaded));

  // So is a load scale outside its bounds.
  TEST_ASSERT_TRUE(raw.begin("inv_battery", false));
  raw.putUInt("chg_ms_pct", 0U);
  raw.putUInt("load_ppm", 3000000U);
  raw.end();
  BatteryStateStore scale;
  TEST_ASSERT_TRUE(scale.begin());
  TEST_ASSERT_FALSE(scale.load(loaded));
  host_nvs::reboot();
}

void test_host_battery_state_migrates_the_first_layout() {
  // The first layout: 650 mAh of the 1300 mAh reference cell, a learned rate, and an average load.
  Preferences raw;
  TEST_ASSERT_TRUE(raw.begin("inv_battery", false));
  raw.putUInt("rem_uah", 650000U);
  raw.putUInt("chg_ms_pct", 61000U);
  raw.putUInt("avg_cma", 800U);
  raw.end();

  BatteryStateStore store;
  TEST_ASSERT_TRUE(store.begin());
  BatteryGaugeState loaded;
  TEST_ASSERT_TRUE(store.load(loaded));
  TEST_ASSERT_EQUAL_UINT32(500000U, loaded.chargePpm);
  TEST_ASSERT_EQUAL_UINT32(61000U, loaded.chargeMsPerPercent);
  TEST_ASSERT_EQUAL_UINT32(0U, loaded.loadScalePpm);

  // The first save writes the new keys and removes the old ones, even with nothing changed.
  TEST_ASSERT_TRUE(store.save(loaded));
  TEST_ASSERT_TRUE(raw.begin("inv_battery", false));
  TEST_ASSERT_FALSE(raw.isKey("rem_uah"));
  TEST_ASSERT_FALSE(raw.isKey("avg_cma"));
  TEST_ASSERT_TRUE(raw.isKey("soc_ppm"));
  raw.end();

  host_nvs::reboot();
  BatteryStateStore after;
  TEST_ASSERT_TRUE(after.begin());
  BatteryGaugeState reloaded;
  TEST_ASSERT_TRUE(after.load(reloaded));
  TEST_ASSERT_EQUAL_UINT32(500000U, reloaded.chargePpm);
  TEST_ASSERT_EQUAL_UINT32(61000U, reloaded.chargeMsPerPercent);
}

void test_host_battery_state_writes_only_changed_values() {
  BatteryStateStore store;
  TEST_ASSERT_TRUE(store.begin());
  BatteryGaugeState state;
  state.chargePpm = 500000U;
  state.loadScalePpm = 1000000U;
  TEST_ASSERT_TRUE(store.save(state));

  // An unchanged state needs no write, so it succeeds even with writes refused.
  host_nvs::faults().writesBeforePowerCut = 0;
  TEST_ASSERT_TRUE(store.save(state));

  // A changed value needs a write, which the cut refuses.
  state.chargePpm = 499000U;
  TEST_ASSERT_FALSE(store.save(state));
  host_nvs::reboot();
}

void runHostBatteryStateTests() {
  RUN_TEST(test_host_battery_state_survives_a_reboot);
  RUN_TEST(test_host_battery_state_drops_an_out_of_range_span_only);
  RUN_TEST(test_host_battery_state_is_absent_before_the_first_save);
  RUN_TEST(test_host_battery_state_rejects_out_of_range_values);
  RUN_TEST(test_host_battery_state_migrates_the_first_layout);
  RUN_TEST(test_host_battery_state_writes_only_changed_values);
}
