// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

// Battery gauge: the percent follows the load, not the charger's raised voltage.
// Expected values assume the 1300 mAh cell in kBatteryNominalCapacityUah.

#include <unity.h>

#include <cstdint>

#include "InventatoryScanBattery.h"

using namespace inventatory_scan;

namespace {

// Open-circuit voltages the curve reads as these percents.
constexpr uint32_t kMvAt40 = 3770;
constexpr uint32_t kMvAt50 = 3830;
constexpr uint32_t kMvAt95 = 4150;

uint32_t chargeAt(uint32_t percent) { return kBatteryNominalCapacityUah * percent / 100U; }

}  // namespace

void test_gauge_charger_voltage_does_not_move_the_percent() {
  // A 150 mV charger lift must not jump the percent: the gauge only counts charge in.
  BatteryTracker tracker;
  tracker.update(3800, false, 0);
  TEST_ASSERT_EQUAL_INT(45, tracker.percent());
  int previous = tracker.percent();
  uint32_t nowMs = 1000;
  for (uint32_t second = 0; second < 1800; ++second, nowMs += 1000UL) {
    tracker.update(3950, true, nowMs);
    TEST_ASSERT_TRUE(tracker.percent() >= previous);
    TEST_ASSERT_TRUE(tracker.percent() <= kBatteryChargeCapPercent);
    previous = tracker.percent();
  }
  // Thirty minutes at the prior 93.6 s per point is about 19 points.
  TEST_ASSERT_TRUE(tracker.percent() >= 63 && tracker.percent() <= 65);
  TEST_ASSERT_TRUE(tracker.charging());

  // The LED goes dark with the cell at 4.15 V: charged, and the percent reads 100.
  tracker.update(kMvAt95, false, nowMs, 0, true);
  tracker.update(kMvAt95, false, nowMs + 1000UL, 0, true);
  tracker.update(kMvAt95, false, nowMs + 2000UL, 0, true);
  TEST_ASSERT_TRUE(tracker.chargeDone());
  TEST_ASSERT_EQUAL_INT(100, tracker.percent());
}

void test_gauge_load_takes_the_charge_out_at_its_rate() {
  // 80 mAh is about 6 points of the 1300 mAh cell: 70 to 64.
  BatteryGaugeState stored;
  stored.remainingUah = chargeAt(70);
  BatteryTracker tracker;
  tracker.restore(stored);
  tracker.update(3950, false, 0, 80);
  for (uint32_t second = 1; second <= 3600; ++second) {
    tracker.update(3950, false, second * 1000UL, 80);
  }
  TEST_ASSERT_EQUAL_INT(64, tracker.estimatePercent());
  TEST_ASSERT_EQUAL_INT(64, tracker.percent());
}

void test_gauge_voltage_sag_under_load_does_not_move_the_percent() {
  // A 200 mV sag for 3 s in every 10 s (radio burst) must not move the percent.
  BatteryTracker steady;
  BatteryTracker sagging;
  steady.update(kMvAt50, false, 0, 80);
  sagging.update(kMvAt50, false, 0, 80);
  for (uint32_t second = 1; second <= 600; ++second) {
    const uint32_t nowMs = second * 1000UL;
    const bool burst = second % 10 < 3;
    steady.update(kMvAt50, false, nowMs, 80);
    sagging.update(burst ? kMvAt50 - 200U : kMvAt50, false, nowMs, 80);
    TEST_ASSERT_EQUAL_INT(steady.percent(), sagging.percent());
    TEST_ASSERT_TRUE(sagging.level() == BatteryLevel::Ok);
  }
}

void test_gauge_rest_corrects_a_drifted_estimate_slowly() {
  // Open circuit reads 50% against a 60% estimate: no change for an hour, then a point a minute.
  BatteryGaugeState stored;
  stored.remainingUah = chargeAt(60);
  BatteryTracker tracker;
  tracker.restore(stored);
  tracker.update(kMvAt50, false, 0, 0);
  int previous = tracker.estimatePercent();
  for (uint32_t second = 1; second <= 4500; ++second) {
    tracker.update(kMvAt50, false, second * 1000UL, 0);
    if (second < 3600) TEST_ASSERT_EQUAL_INT(60, tracker.estimatePercent());
    if (second % 60 == 0) {
      const int change = previous - tracker.estimatePercent();
      TEST_ASSERT_TRUE(change >= 0 && change <= 1);
      previous = tracker.estimatePercent();
    }
  }
  TEST_ASSERT_TRUE(tracker.estimatePercent() >= 49 && tracker.estimatePercent() <= 52);
}

void test_gauge_load_prevents_the_rest_correction() {
  // Under load, the open-circuit reading (50%) must not correct the 60% estimate.
  BatteryGaugeState stored;
  stored.remainingUah = chargeAt(60);
  BatteryTracker tracker;
  tracker.restore(stored);
  tracker.update(kMvAt50, false, 0, 80);
  for (uint32_t second = 1; second <= 7200; ++second) {
    tracker.update(kMvAt50, false, second * 1000UL, 80);
  }
  TEST_ASSERT_EQUAL_INT(48, tracker.estimatePercent());
}

void test_gauge_rest_at_full_voltage_reads_charged() {
  // At rest, LED dark at 4.15 V reads 100 at once, whatever was stored.
  BatteryGaugeState stored;
  stored.remainingUah = chargeAt(60);
  BatteryTracker tracker;
  tracker.restore(stored);
  for (uint32_t second = 0; second <= 4000; ++second) {
    tracker.update(kMvAt95, false, second * 1000UL, 0, true);
  }
  TEST_ASSERT_TRUE(tracker.chargeDone());
  TEST_ASSERT_EQUAL_INT(100, tracker.estimatePercent());
}

void test_gauge_charged_hold_reseeds_from_the_voltage_when_it_ends() {
  // Unplugged from full, the estimate holds at 100, then reseeds from the voltage when it falls.
  BatteryTracker tracker;
  tracker.update(kMvAt95, false, 0, 0, true);
  for (uint32_t second = 1; second <= 600; ++second) {
    tracker.update(4050, false, second * 1000UL, 80, true);
  }
  TEST_ASSERT_EQUAL_INT(100, tracker.estimatePercent());
  tracker.update(3950, false, 601000UL, 80);
  tracker.update(3950, false, 602000UL, 80);
  TEST_ASSERT_FALSE(tracker.chargeDone());
  // 3950 mV is 70% on the curve.
  TEST_ASSERT_TRUE(tracker.estimatePercent() >= 69 && tracker.estimatePercent() <= 71);
}

void test_gauge_empty_at_rest_reads_zero() {
  // An undocked cell at or below the floor for a full rest dwell is empty, whatever the estimate.
  BatteryTracker tracker;
  tracker.update(kMvAt50, false, 0);
  for (uint32_t second = 1; second <= 3600; ++second) {
    tracker.update(3290, false, second * 1000UL);
  }
  TEST_ASSERT_EQUAL_INT(0, tracker.estimatePercent());
  TEST_ASSERT_TRUE(tracker.level() == BatteryLevel::Critical);
}

void test_gauge_full_charge_teaches_the_rate() {
  // From 40% to full in about an hour: 60 points in 3600 s is about 60 s per point.
  BatteryTracker tracker;
  tracker.update(kMvAt40, false, 0);
  uint32_t nowMs = 0;
  for (uint32_t second = 1; second <= 3600; ++second) {
    nowMs = second * 1000UL;
    tracker.update(kMvAt40, true, nowMs);
  }
  // The charge ends when the LED goes dark, and the rate is learned.
  tracker.update(kMvAt95, false, nowMs + 1000UL, 0, true);
  tracker.update(kMvAt95, false, nowMs + 2000UL, 0, true);
  tracker.update(kMvAt95, false, nowMs + 3000UL, 0, true);
  const uint32_t learned = tracker.state().chargeMsPerPercent;
  TEST_ASSERT_TRUE(learned >= 58000U && learned <= 62000U);
  TEST_ASSERT_EQUAL_INT(100, tracker.percent());
}

void test_gauge_done_at_boot_teaches_nothing() {
  // Booting already charged times no charge, so no rate is learned.
  BatteryTracker tracker;
  tracker.update(kMvAt95, false, 0, 0, true);
  tracker.update(kMvAt95, false, 1000, 0, true);
  tracker.update(kMvAt95, false, 2000, 0, true);
  TEST_ASSERT_EQUAL_UINT32(0U, tracker.state().chargeMsPerPercent);
  TEST_ASSERT_EQUAL_INT(100, tracker.percent());
}

void test_gauge_charge_stops_at_99_until_the_charger_is_done() {
  // The estimate caps at 99 while the LED is lit; it reads 100 once the LED goes dark.
  BatteryTracker tracker;
  tracker.update(kMvAt95, true, 0);
  for (uint32_t second = 1; second <= 7200; ++second) {
    tracker.update(kMvAt95, true, second * 1000UL);
  }
  TEST_ASSERT_EQUAL_INT(99, tracker.estimatePercent());
  TEST_ASSERT_EQUAL_INT(99, tracker.percent());
  tracker.update(kMvAt95, false, 7201000UL, 0, true);
  tracker.update(kMvAt95, false, 7202000UL, 0, true);
  tracker.update(kMvAt95, false, 7203000UL, 0, true);
  TEST_ASSERT_EQUAL_INT(100, tracker.percent());
}

void test_gauge_shown_percent_moves_at_most_once_per_interval() {
  // The shown percent moves at most once per 30 s, so it trails a faster estimate.
  BatteryGaugeState stored;
  stored.remainingUah = chargeAt(40);
  stored.chargeMsPerPercent = 20000U;
  BatteryTracker tracker;
  tracker.restore(stored);
  tracker.update(kMvAt40, false, 0);
  int previous = tracker.percent();
  uint32_t lastChangeSecond = 0;
  for (uint32_t second = 1; second <= 600; ++second) {
    tracker.update(kMvAt40, true, second * 1000UL);
    if (tracker.percent() != previous) {
      if (lastChangeSecond != 0) TEST_ASSERT_TRUE(second - lastChangeSecond >= 30U);
      lastChangeSecond = second;
      previous = tracker.percent();
    }
  }
  TEST_ASSERT_TRUE(tracker.estimatePercent() >= 69);
  TEST_ASSERT_TRUE(tracker.percent() <= 61);
}

void test_gauge_gap_longer_than_a_step_is_not_load() {
  // A 10 s stall counts as at most kBatteryMaxStepMs of load.
  BatteryGauge gauge;
  gauge.update(kMvAt50, false, false, 3600, 0);
  const uint32_t before = static_cast<uint32_t>(gauge.state().remainingUah);
  gauge.update(kMvAt50, false, false, 3600, 10000);
  // 3600 mA for 5 s is 5000 uAh.
  TEST_ASSERT_EQUAL_UINT32(before - 5000U, gauge.state().remainingUah);
}

void test_gauge_time_left_on_battery_follows_the_load() {
  // 50% of 1300 mAh is 650 mAh. Six minutes at 100 mA take 10 mAh, leaving 640 mAh: 384 minutes.
  BatteryTracker tracker;
  tracker.update(kMvAt50, false, 0, 100);
  for (uint32_t second = 1; second <= 360; ++second) {
    tracker.update(kMvAt50, false, second * 1000UL, 100);
    if (second == 200) TEST_ASSERT_TRUE(tracker.estimateApproximate());
  }
  TEST_ASSERT_FALSE(tracker.estimateApproximate());
  TEST_ASSERT_TRUE(tracker.minutesRemaining() >= 382 && tracker.minutesRemaining() <= 386);
}

void test_gauge_time_left_while_charging_uses_the_timed_rate() {
  // 60 points at the prior 93.6 s each is about 94 minutes, marked approximate.
  BatteryTracker tracker;
  tracker.update(kMvAt40, false, 0);
  tracker.update(kMvAt40, true, 1000);
  tracker.update(kMvAt40, true, 2000);
  TEST_ASSERT_TRUE(tracker.charging());
  TEST_ASSERT_TRUE(tracker.estimateApproximate());
  TEST_ASSERT_TRUE(tracker.minutesRemaining() >= 91 && tracker.minutesRemaining() <= 95);
}

void test_tracker_voltage_floor_is_critical_whatever_the_estimate() {
  // Undocked at the floor is critical whatever the estimate says, after four samples.
  BatteryTracker tracker;
  tracker.update(kMvAt40, false, 0);
  for (uint32_t second = 1; second <= 3; ++second) tracker.update(kBatteryFloorMv, false, second * 1000UL);
  TEST_ASSERT_TRUE(tracker.level() == BatteryLevel::Ok);
  tracker.update(kBatteryFloorMv, false, 4000);
  TEST_ASSERT_TRUE(tracker.level() == BatteryLevel::Critical);
  TEST_ASSERT_EQUAL_INT(40, tracker.estimatePercent());
}

void test_gauge_stored_full_charge_is_checked_at_boot() {
  // The stored 100% is replaced by the boot voltage (21%): charge lost while off is not counted.
  BatteryGaugeState stored;
  stored.remainingUah = kBatteryNominalCapacityUah;
  BatteryTracker tracker;
  tracker.restore(stored);
  tracker.update(3650, false, 0, 65);
  TEST_ASSERT_EQUAL_INT(21, tracker.estimatePercent());
  TEST_ASSERT_EQUAL_INT(21, tracker.percent());
  // Twenty minutes at 65 mA take about 1.7 points.
  for (uint32_t second = 1; second <= 1200; ++second) {
    tracker.update(3650, false, second * 1000UL, 65);
  }
  TEST_ASSERT_TRUE(tracker.percent() >= 18 && tracker.percent() <= 21);
}

void test_gauge_stored_full_charge_is_not_held_off_the_charger() {
  // The charged hold needs the charger: off it, 4.15 V reads 95%, not the stored 100%.
  BatteryGaugeState stored;
  stored.remainingUah = kBatteryNominalCapacityUah;
  BatteryTracker tracker;
  tracker.restore(stored);
  tracker.update(kMvAt95, false, 0, 65);
  TEST_ASSERT_EQUAL_INT(95, tracker.percent());
  TEST_ASSERT_FALSE(tracker.chargeDone());
}

void test_gauge_stored_full_charge_is_held_on_the_charger_at_boot() {
  // Booted on the charger with the cell full and the LED dark: charged, at 100%, as after a charge.
  BatteryGaugeState stored;
  stored.remainingUah = kBatteryNominalCapacityUah;
  BatteryTracker tracker;
  tracker.restore(stored);
  tracker.update(kMvAt95, false, 0, 65, true);
  TEST_ASSERT_TRUE(tracker.chargeDone());
  TEST_ASSERT_EQUAL_INT(100, tracker.percent());
}

void test_gauge_stored_charge_within_tolerance_is_kept_at_boot() {
  // 60% stored against 50% is within tolerance; the rest correction handles it.
  BatteryGaugeState stored;
  stored.remainingUah = chargeAt(60);
  BatteryTracker tracker;
  tracker.restore(stored);
  tracker.update(kMvAt50, false, 0, 65);
  TEST_ASSERT_EQUAL_INT(60, tracker.estimatePercent());
}

void test_gauge_stored_charge_is_checked_while_charging() {
  // The charger only raises the voltage: a 21% reading still replaces the stored 80%.
  BatteryGaugeState stored;
  stored.remainingUah = chargeAt(80);
  BatteryTracker tracker;
  tracker.restore(stored);
  tracker.update(3650, true, 0, 65);
  TEST_ASSERT_TRUE(tracker.charging());
  TEST_ASSERT_EQUAL_INT(21, tracker.estimatePercent());
}

void test_tracker_boot_refusal_follows_the_gauge_not_a_sagged_voltage() {
  // A cell the gauge holds at 25% reads 10% under load at boot. The gap is within the boot tolerance, so the
  // stored charge stays, and the sag does not refuse the boot.
  BatteryGaugeState stored;
  stored.remainingUah = chargeAt(25);
  BatteryTracker sagged;
  sagged.restore(stored);
  sagged.update(3550, false, 0);
  TEST_ASSERT_EQUAL_INT(25, sagged.estimatePercent());
  TEST_ASSERT_EQUAL_INT(25, sagged.refusePercent());
}

void test_tracker_boot_refusal_at_the_voltage_floor() {
  // At the floor the cell is empty, whatever the estimate says.
  BatteryGaugeState stored;
  stored.remainingUah = chargeAt(15);
  BatteryTracker atFloor;
  atFloor.restore(stored);
  atFloor.update(kBatteryFloorMv, false, 0);
  TEST_ASSERT_EQUAL_INT(0, atFloor.refusePercent());
}

void test_tracker_boot_refusal_of_a_fresh_low_cell() {
  // A first boot takes the voltage as the charge, so a cell at 3.42 V is low and is refused.
  BatteryTracker fresh;
  fresh.update(3420, false, 0);
  TEST_ASSERT_TRUE(fresh.refusePercent() <= kBatteryLowPercent);
}

void test_tracker_state_resumes_the_same_estimate() {
  BatteryTracker first;
  first.update(kMvAt50, false, 0, 100);
  for (uint32_t second = 1; second <= 120; ++second) {
    first.update(kMvAt50, false, second * 1000UL, 100);
  }
  BatteryTracker resumed;
  resumed.restore(first.state());
  resumed.update(kMvAt50, false, 0, 100);
  TEST_ASSERT_EQUAL_INT(first.estimatePercent(), resumed.estimatePercent());
  TEST_ASSERT_EQUAL_INT(first.minutesRemaining(), resumed.minutesRemaining());
  // The stored average is an observed one, so the time left is not marked approximate.
  TEST_ASSERT_FALSE(resumed.estimateApproximate());
}

void registerBatteryGaugeTests() {
  RUN_TEST(test_gauge_charger_voltage_does_not_move_the_percent);
  RUN_TEST(test_gauge_load_takes_the_charge_out_at_its_rate);
  RUN_TEST(test_gauge_voltage_sag_under_load_does_not_move_the_percent);
  RUN_TEST(test_gauge_rest_corrects_a_drifted_estimate_slowly);
  RUN_TEST(test_gauge_load_prevents_the_rest_correction);
  RUN_TEST(test_gauge_rest_at_full_voltage_reads_charged);
  RUN_TEST(test_gauge_charged_hold_reseeds_from_the_voltage_when_it_ends);
  RUN_TEST(test_gauge_empty_at_rest_reads_zero);
  RUN_TEST(test_gauge_full_charge_teaches_the_rate);
  RUN_TEST(test_gauge_done_at_boot_teaches_nothing);
  RUN_TEST(test_gauge_charge_stops_at_99_until_the_charger_is_done);
  RUN_TEST(test_gauge_shown_percent_moves_at_most_once_per_interval);
  RUN_TEST(test_gauge_gap_longer_than_a_step_is_not_load);
  RUN_TEST(test_gauge_time_left_on_battery_follows_the_load);
  RUN_TEST(test_gauge_time_left_while_charging_uses_the_timed_rate);
  RUN_TEST(test_tracker_voltage_floor_is_critical_whatever_the_estimate);
  RUN_TEST(test_gauge_stored_full_charge_is_checked_at_boot);
  RUN_TEST(test_gauge_stored_full_charge_is_not_held_off_the_charger);
  RUN_TEST(test_gauge_stored_full_charge_is_held_on_the_charger_at_boot);
  RUN_TEST(test_gauge_stored_charge_within_tolerance_is_kept_at_boot);
  RUN_TEST(test_gauge_stored_charge_is_checked_while_charging);
  RUN_TEST(test_tracker_boot_refusal_follows_the_gauge_not_a_sagged_voltage);
  RUN_TEST(test_tracker_boot_refusal_at_the_voltage_floor);
  RUN_TEST(test_tracker_boot_refusal_of_a_fresh_low_cell);
  RUN_TEST(test_tracker_state_resumes_the_same_estimate);
}
