// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

// The battery gauge: the percent follows the load, a charger's raised voltage does not move it, the
// rest correction is slow, and the shown percent moves at most once per display interval.
// Expectations are for the 1300 mAh cell in kBatteryNominalCapacityUah.

#include <unity.h>

#include <cstdint>

#include "InventatoryScanBattery.h"

using namespace inventatory_scan;

namespace {

// Open-circuit voltages the curve reads as these percents.
constexpr uint32_t kMvAt40 = 3770;
constexpr uint32_t kMvAt50 = 3830;
constexpr uint32_t kMvAt95 = 4150;

// The stored charge for a percent of the nominal capacity.
uint32_t chargeAt(uint32_t percent) { return kBatteryNominalCapacityUah * percent / 100U; }

}  // namespace

void test_gauge_charger_voltage_does_not_move_the_percent() {
  // The charger lifts the voltage 150 mV above the open-circuit value. The old voltage reading
  // turned that into a jump of about 20 points; the gauge only counts the charge in.
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
  // Thirty minutes at the prior 93.6 s per point is about 19 points: 45 to about 64.
  TEST_ASSERT_TRUE(tracker.percent() >= 63 && tracker.percent() <= 65);
  TEST_ASSERT_TRUE(tracker.charging());

  tracker.update(kMvAt95, true, nowMs, true);
  tracker.update(kMvAt95, true, nowMs + 1000UL, true);
  TEST_ASSERT_TRUE(tracker.chargeDone());
  TEST_ASSERT_EQUAL_INT(100, tracker.percent());
}

void test_gauge_load_takes_the_charge_out_at_its_rate() {
  // 80 mA for an hour is 80 mAh: about 6 points of a 1300 mAh cell, from full to 94.
  BatteryTracker tracker;
  tracker.update(4200, false, 0, false, 80);
  for (uint32_t second = 1; second <= 3600; ++second) {
    tracker.update(4200, false, second * 1000UL, false, 80);
  }
  TEST_ASSERT_EQUAL_INT(94, tracker.estimatePercent());
  TEST_ASSERT_EQUAL_INT(94, tracker.percent());
}

void test_gauge_voltage_sag_under_load_does_not_move_the_percent() {
  // Two cells on the same load. One sags 200 mV for 3 s in every 10 s, as a radio burst does.
  // The percent must match the other cell at every second.
  BatteryTracker steady;
  BatteryTracker sagging;
  steady.update(kMvAt50, false, 0, false, 80);
  sagging.update(kMvAt50, false, 0, false, 80);
  for (uint32_t second = 1; second <= 600; ++second) {
    const uint32_t nowMs = second * 1000UL;
    const bool burst = second % 10 < 3;
    steady.update(kMvAt50, false, nowMs, false, 80);
    sagging.update(burst ? kMvAt50 - 200U : kMvAt50, false, nowMs, false, 80);
    TEST_ASSERT_EQUAL_INT(steady.percent(), sagging.percent());
    TEST_ASSERT_TRUE(sagging.level() == BatteryLevel::Ok);
  }
}

void test_gauge_rest_corrects_a_drifted_estimate_slowly() {
  // The estimate says 60%, but the cell reads 50% at open circuit. Nothing changes for the first
  // hour of rest; then the estimate moves one point a minute until it is within the tolerance.
  BatteryGaugeState stored;
  stored.remainingUah = chargeAt(60);
  BatteryTracker tracker;
  tracker.restore(stored);
  tracker.update(kMvAt50, false, 0, false, 0);
  int previous = tracker.estimatePercent();
  for (uint32_t second = 1; second <= 4500; ++second) {
    tracker.update(kMvAt50, false, second * 1000UL, false, 0);
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
  // 80 mA for two hours is 160 mAh, about 12 points of a 1300 mAh cell, off 60%. The open-circuit
  // reading (50%) must not correct that, because the cell is not at rest.
  BatteryGaugeState stored;
  stored.remainingUah = chargeAt(60);
  BatteryTracker tracker;
  tracker.restore(stored);
  tracker.update(kMvAt50, false, 0, false, 80);
  for (uint32_t second = 1; second <= 7200; ++second) {
    tracker.update(kMvAt50, false, second * 1000UL, false, 80);
  }
  TEST_ASSERT_EQUAL_INT(48, tracker.estimatePercent());
}

void test_gauge_rest_does_not_correct_above_the_trusted_band() {
  // Above 90% the open-circuit reading is not trusted. The full event anchors that end instead.
  BatteryGaugeState stored;
  stored.remainingUah = chargeAt(60);
  BatteryTracker tracker;
  tracker.restore(stored);
  for (uint32_t second = 0; second <= 4000; ++second) {
    tracker.update(kMvAt95, false, second * 1000UL);
  }
  TEST_ASSERT_EQUAL_INT(60, tracker.estimatePercent());
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
  tracker.update(kMvAt95, true, nowMs + 1000UL, true);
  tracker.update(kMvAt95, true, nowMs + 2000UL, true);
  const uint32_t learned = tracker.state().chargeMsPerPercent;
  TEST_ASSERT_TRUE(learned >= 58000U && learned <= 62000U);
  TEST_ASSERT_EQUAL_INT(100, tracker.percent());
}

void test_gauge_done_at_boot_teaches_nothing() {
  // A boot that finds the charger already done has not timed a charge, so it learns no rate.
  BatteryTracker tracker;
  tracker.update(kMvAt95, true, 0, true);
  tracker.update(kMvAt95, true, 1000, true);
  tracker.update(kMvAt95, true, 2000, true);
  TEST_ASSERT_EQUAL_UINT32(0U, tracker.state().chargeMsPerPercent);
  TEST_ASSERT_EQUAL_INT(100, tracker.percent());
}

void test_gauge_charge_stops_at_99_until_the_charger_is_done() {
  // From 95% the prior rate would pass 100% in about 6 minutes. The estimate stops at 99 until the
  // charger reports done, and then it reads 100.
  BatteryTracker tracker;
  tracker.update(kMvAt95, false, 0);
  for (uint32_t second = 1; second <= 7200; ++second) {
    tracker.update(kMvAt95, true, second * 1000UL);
  }
  TEST_ASSERT_EQUAL_INT(99, tracker.estimatePercent());
  TEST_ASSERT_EQUAL_INT(99, tracker.percent());
  tracker.update(kMvAt95, true, 7201000UL, true);
  tracker.update(kMvAt95, true, 7202000UL, true);
  TEST_ASSERT_EQUAL_INT(100, tracker.percent());
}

void test_gauge_shown_percent_moves_at_most_once_per_interval() {
  // A charge that adds a point every 20 s is faster than the display allows: the shown percent may
  // move only once per 30 s, so it trails the estimate.
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
  // A stall of 10 s must not count as 10 s of load. The gauge takes at most kBatteryMaxStepMs.
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
  tracker.update(kMvAt50, false, 0, false, 100);
  for (uint32_t second = 1; second <= 360; ++second) {
    tracker.update(kMvAt50, false, second * 1000UL, false, 100);
    if (second == 200) TEST_ASSERT_TRUE(tracker.estimateApproximate());
  }
  TEST_ASSERT_FALSE(tracker.estimateApproximate());
  TEST_ASSERT_TRUE(tracker.minutesRemaining() >= 382 && tracker.minutesRemaining() <= 386);
}

void test_gauge_time_left_while_charging_uses_the_timed_rate() {
  // 60 points to go at the prior 93.6 s per point is about 94 minutes, approximate until a charge
  // is timed.
  BatteryTracker tracker;
  tracker.update(kMvAt40, false, 0);
  tracker.update(kMvAt40, true, 1000);
  tracker.update(kMvAt40, true, 2000);
  TEST_ASSERT_TRUE(tracker.charging());
  TEST_ASSERT_TRUE(tracker.estimateApproximate());
  TEST_ASSERT_TRUE(tracker.minutesRemaining() >= 91 && tracker.minutesRemaining() <= 95);
}

void test_tracker_voltage_floor_is_critical_whatever_the_estimate() {
  // The estimate says 40%, but the cell reads 3.30 V undocked. Four samples, as for the other limits.
  BatteryGaugeState stored;
  stored.remainingUah = chargeAt(40);
  BatteryTracker tracker;
  tracker.restore(stored);
  for (uint32_t second = 0; second < 3; ++second) tracker.update(kBatteryFloorMv, false, second * 1000UL);
  TEST_ASSERT_TRUE(tracker.level() == BatteryLevel::Ok);
  tracker.update(kBatteryFloorMv, false, 3000);
  TEST_ASSERT_TRUE(tracker.level() == BatteryLevel::Critical);
  TEST_ASSERT_EQUAL_INT(40, tracker.estimatePercent());
}

void test_tracker_refuses_a_low_cell_by_the_lower_reading() {
  // A drifted estimate must not let a flat cell start: the lower of the two readings decides.
  BatteryGaugeState stored;
  stored.remainingUah = chargeAt(60);
  BatteryTracker drifted;
  drifted.restore(stored);
  drifted.update(3500, false, 0);  // 6% by the voltage, 60% by the estimate
  TEST_ASSERT_EQUAL_INT(6, drifted.refusePercent());

  stored.remainingUah = chargeAt(20);
  BatteryTracker flat;
  flat.restore(stored);
  flat.update(3900, false, 0);  // 62% by the voltage, 20% by the estimate
  TEST_ASSERT_EQUAL_INT(20, flat.refusePercent());
}

void test_tracker_state_resumes_the_same_estimate() {
  BatteryTracker first;
  first.update(kMvAt50, false, 0, false, 100);
  for (uint32_t second = 1; second <= 120; ++second) {
    first.update(kMvAt50, false, second * 1000UL, false, 100);
  }
  BatteryTracker resumed;
  resumed.restore(first.state());
  resumed.update(kMvAt50, false, 0, false, 100);
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
  RUN_TEST(test_gauge_rest_does_not_correct_above_the_trusted_band);
  RUN_TEST(test_gauge_empty_at_rest_reads_zero);
  RUN_TEST(test_gauge_full_charge_teaches_the_rate);
  RUN_TEST(test_gauge_done_at_boot_teaches_nothing);
  RUN_TEST(test_gauge_charge_stops_at_99_until_the_charger_is_done);
  RUN_TEST(test_gauge_shown_percent_moves_at_most_once_per_interval);
  RUN_TEST(test_gauge_gap_longer_than_a_step_is_not_load);
  RUN_TEST(test_gauge_time_left_on_battery_follows_the_load);
  RUN_TEST(test_gauge_time_left_while_charging_uses_the_timed_rate);
  RUN_TEST(test_tracker_voltage_floor_is_critical_whatever_the_estimate);
  RUN_TEST(test_tracker_refuses_a_low_cell_by_the_lower_reading);
  RUN_TEST(test_tracker_state_resumes_the_same_estimate);
}
