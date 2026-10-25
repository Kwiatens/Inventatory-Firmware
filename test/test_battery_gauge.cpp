// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

// Battery gauge: the percent follows the load, corrected by the load-corrected voltage, and never by the charger's
// raised voltage. The time left is smooth. Expected values assume the 1300 mAh reference cell unless a test sets the
// capacity. The simulated cell below runs hours of use against the gauge.

#include <unity.h>

#include <cstdint>

#include "InventatoryScanBattery.h"

using namespace inventatory_scan;

namespace {

// Resting voltages the curve reads as these percents.
constexpr uint32_t kMvAt40 = 3690;
constexpr uint32_t kMvAt50 = 3740;
constexpr uint32_t kMvAt60 = 3800;
constexpr uint32_t kMvAt95 = 4100;
constexpr uint32_t kShortOffMs = 60000UL;
// 65 mA sags the reading by about 10 mV (kBatteryInternalMilliohms).
constexpr uint32_t kSag65 = 10;
constexpr uint32_t kLongOffMs = 8UL * 3600000UL;

BatteryGaugeState storedAt(uint32_t percent) {
  BatteryGaugeState state;
  state.chargePpm = percent * 10000U;
  return state;
}

// A cell that follows the gauge's curve, with a resistance, reading noise and radio bursts. Its load is the model's
// load times loadScale, so a scale other than 1 is a wrong load guess.
struct SimulatedCell {
  double share = 0.5;
  double capacityMah = 3400.0;
  double loadScale = 1.0;
  double offsetMv = 0.0;
  uint32_t seed = 12345U;
  uint32_t second = 0;

  double trueMa(uint32_t modelMa) const { return modelMa * loadScale; }

  // Takes a second of load out, then reads the cell: noise of +-8 mV and a 40 mV Wi-Fi burst every 30 s, from the
  // 15th second (Wi-Fi is off at boot).
  uint32_t step(uint32_t modelMa) {
    share -= trueMa(modelMa) / 3600.0 / capacityMah;
    if (share < 0.0) share = 0.0;
    ++second;
    seed = seed * 1103515245U + 12345U;
    const double noise = static_cast<double>((seed >> 16) % 17U) - 8.0;
    const double burst = second % 30U == 15U || second % 30U == 16U ? 40.0 : 0.0;
    const double sag = trueMa(modelMa) * kBatteryInternalMilliohms / 1000.0;
    return static_cast<uint32_t>(batteryMillivoltsFromShare(share) + offsetMv - sag - burst + noise);
  }

  int percent() const { return static_cast<int>(share * 100.0 + 0.5); }
};

int distance(int a, int b) { return a > b ? a - b : b - a; }

}  // namespace

void test_gauge_time_left_is_steady_after_boot() {
  // The bug this gauge replaces: booted awake from a state saved asleep, the time left fell by tens of minutes a
  // second. 60% of 3400 mAh at 80 mA is about 1530 minutes. It must show close to that at once and stay there.
  SimulatedCell cell;
  cell.share = 0.6;
  BatteryTracker tracker;
  tracker.setCapacity(3400000UL);
  tracker.restore(storedAt(60), kLongOffMs);
  uint32_t nowMs = 0;
  tracker.update(cell.step(80), false, nowMs, 80);
  const int32_t first = tracker.minutesRemaining();
  TEST_ASSERT_TRUE(first >= 1400 && first <= 1700);
  int32_t lowest = first;
  int32_t highest = first;
  for (uint32_t second = 1; second <= 1800; ++second) {
    nowMs += 1000UL;
    tracker.update(cell.step(80), false, nowMs, 80);
    lowest = std::min(lowest, tracker.minutesRemaining());
    highest = std::max(highest, tracker.minutesRemaining());
  }
  // Half an hour on, at most one 0.1-day step either way.
  TEST_ASSERT_TRUE(highest - lowest <= 144);
}

void test_gauge_time_left_moves_slowly_when_the_load_changes() {
  // Wi-Fi off (80 to 65 mA) raises the true time left by about a quarter. The shown value moves at most 3% a minute.
  SimulatedCell cell;
  cell.share = 0.6;
  BatteryTracker tracker;
  tracker.setCapacity(3400000UL);
  uint32_t nowMs = 0;
  tracker.update(cell.step(80), false, nowMs, 80);
  for (uint32_t second = 1; second <= 600; ++second) tracker.update(cell.step(80), false, nowMs += 1000UL, 80);
  int32_t previous = tracker.minutesRemaining();
  for (uint32_t minute = 0; minute < 60; ++minute) {
    for (uint32_t second = 0; second < 60; ++second) tracker.update(cell.step(65), false, nowMs += 1000UL, 65);
    const int32_t shown = tracker.minutesRemaining();
    // One rounding step (144 minutes from a day) plus the slew.
    TEST_ASSERT_TRUE(distance(shown, previous) <= 144 + previous * 4 / 100);
    previous = shown;
  }
  TEST_ASSERT_TRUE(previous > 1700);
}

void test_gauge_percent_follows_a_cell_whose_load_is_underestimated() {
  // The cell draws 30% more than the load guess. Counting alone would be 9 points high after 10 hours; the voltage
  // keeps the gauge within a few points.
  SimulatedCell cell;
  cell.share = 0.9;
  cell.loadScale = 1.3;
  BatteryTracker tracker;
  tracker.setCapacity(3400000UL);
  uint32_t nowMs = 0;
  tracker.update(cell.step(80), false, nowMs, 80);
  int previous = tracker.percent();
  for (uint32_t second = 1; second <= 36000; ++second) {
    tracker.update(cell.step(80), false, nowMs += 1000UL, 80);
    TEST_ASSERT_TRUE(tracker.percent() <= previous);
    previous = tracker.percent();
    if (second > 3600 && second % 60U == 0) TEST_ASSERT_TRUE(distance(tracker.percent(), cell.percent()) <= 5);
  }
}

void test_gauge_learns_the_load_scale_over_a_discharge() {
  // Over a long discharge the gauge finds that the cell draws more than the guess, and scales its load up.
  SimulatedCell cell;
  cell.share = 0.9;
  cell.loadScale = 1.3;
  BatteryTracker tracker;
  tracker.setCapacity(3400000UL);
  uint32_t nowMs = 0;
  tracker.update(cell.step(80), false, nowMs, 80);
  while (cell.share > 0.15) tracker.update(cell.step(80), false, nowMs += 1000UL, 80);
  TEST_ASSERT_TRUE(tracker.gauge().loadScale() > 1.08);
  TEST_ASSERT_TRUE(tracker.gauge().loadScale() < 1.5);
  TEST_ASSERT_TRUE(tracker.state().loadScalePpm != 0U);
}

// Discharges in one-hour sessions, each too short for a learning span, with offMs between them. Returns the
// learned load scale in ppm, or 0 if none was learned.
uint32_t loadScaleOverSessions(uint32_t offMs) {
  SimulatedCell cell;
  cell.share = 0.9;
  cell.loadScale = 1.3;
  BatteryGaugeState stored;
  bool haveStored = false;
  while (cell.share > 0.15) {
    BatteryTracker tracker;
    tracker.setCapacity(3400000UL);
    if (haveStored) tracker.restore(stored, offMs);
    uint32_t nowMs = 0;
    tracker.update(cell.step(80), false, nowMs, 80);
    for (uint32_t second = 1; second <= 3600U && cell.share > 0.15; ++second) {
      tracker.update(cell.step(80), false, nowMs += 1000UL, 80);
    }
    stored = tracker.state();
    haveStored = true;
  }
  return stored.loadScalePpm;
}

void test_gauge_learning_span_carries_over_a_short_off() {
  // The span is stored, so short sessions with the device off between them still add up to a learned scale.
  const uint32_t scalePpm = loadScaleOverSessions(kShortOffMs);
  TEST_ASSERT_TRUE(scalePpm > 1080000U);
  TEST_ASSERT_TRUE(scalePpm < 1500000U);
}

void test_gauge_learning_span_is_dropped_after_an_unknown_off() {
  // After an unknown time off the stored span is not trusted, and no session alone is long enough for one.
  TEST_ASSERT_EQUAL_UINT32(0U, loadScaleOverSessions(kBatteryOffUnknownMs));
}

void test_gauge_percent_never_rises_on_a_noisy_cell() {
  // Noise and radio bursts never move the shown percent up and down.
  SimulatedCell cell;
  cell.share = 0.7;
  BatteryTracker tracker;
  tracker.setCapacity(3400000UL);
  uint32_t nowMs = 0;
  tracker.update(cell.step(80), false, nowMs, 80);
  int previous = tracker.percent();
  for (uint32_t second = 1; second <= 14400; ++second) {
    tracker.update(cell.step(second % 600U < 300U ? 80 : 65), false, nowMs += 1000UL, second % 600U < 300U ? 80 : 65);
    TEST_ASSERT_TRUE(tracker.percent() <= previous);
    previous = tracker.percent();
  }
  TEST_ASSERT_TRUE(distance(tracker.percent(), cell.percent()) <= 3);
}

void test_gauge_boot_after_a_long_off_trusts_the_resting_voltage() {
  // 80% was stored, but the cell rests at 40% after eight hours off: charge drained while off is found at boot.
  BatteryTracker tracker;
  tracker.restore(storedAt(80), kLongOffMs);
  tracker.update(kMvAt40, false, 0, 3);
  TEST_ASSERT_TRUE(distance(tracker.estimatePercent(), 40) <= 2);
  TEST_ASSERT_EQUAL_INT(tracker.estimatePercent(), tracker.percent());
}

void test_gauge_boot_after_a_long_off_weighs_both_when_they_nearly_agree() {
  // 55% stored, 50% read after eight hours: the result lies between, nearer the resting voltage.
  BatteryTracker tracker;
  tracker.restore(storedAt(55), kLongOffMs);
  tracker.update(kMvAt50, false, 0, 3);
  TEST_ASSERT_TRUE(tracker.estimatePercent() >= 50 && tracker.estimatePercent() <= 53);
}

void test_gauge_boot_after_a_short_off_keeps_the_stored_charge() {
  // A minute off: the cell has not rested, so a reading a few points low barely moves the stored 55%.
  BatteryTracker tracker;
  tracker.restore(storedAt(55), kShortOffMs);
  tracker.update(kMvAt50, false, 0, 65);
  TEST_ASSERT_TRUE(tracker.estimatePercent() >= 54 && tracker.estimatePercent() <= 55);
}

void test_gauge_boot_with_an_unknown_time_off_leans_on_the_voltage() {
  // A power-on reset gives no time off. 70% stored against a 50% reading: the voltage wins.
  BatteryTracker tracker;
  tracker.restore(storedAt(70));
  tracker.update(kMvAt50, false, 0, 3);
  TEST_ASSERT_TRUE(distance(tracker.estimatePercent(), 50) <= 3);
}

void test_gauge_unplugging_a_full_cell_does_not_jump() {
  // Charged on the charger, then unplugged at 4.10 V. The percent counts down from 100 with no jump to the voltage.
  BatteryTracker tracker;
  tracker.setCapacity(3400000UL);
  uint32_t nowMs = 0;
  for (uint32_t second = 0; second < 5; ++second) tracker.update(4150, false, nowMs += 1000UL, 80, true);
  TEST_ASSERT_TRUE(tracker.chargeDone());
  TEST_ASSERT_EQUAL_INT(100, tracker.percent());
  SimulatedCell cell;
  cell.share = 0.99;
  int previous = tracker.percent();
  for (uint32_t second = 0; second < 1800; ++second) {
    tracker.update(cell.step(80), false, nowMs += 1000UL, 80);
    TEST_ASSERT_TRUE(previous - tracker.percent() <= 1);
    previous = tracker.percent();
  }
  TEST_ASSERT_FALSE(tracker.chargeDone());
  TEST_ASSERT_TRUE(tracker.percent() >= 97);
}

void test_gauge_charger_voltage_does_not_move_the_percent() {
  // A 150 mV charger lift must not jump the percent: the gauge only counts charge in.
  BatteryTracker tracker;
  tracker.update(kMvAt60, false, 0);
  TEST_ASSERT_EQUAL_INT(60, tracker.percent());
  int previous = tracker.percent();
  uint32_t nowMs = 1000;
  for (uint32_t second = 0; second < 1800; ++second, nowMs += 1000UL) {
    tracker.update(3950, true, nowMs);
    TEST_ASSERT_TRUE(tracker.percent() >= previous);
    TEST_ASSERT_TRUE(tracker.percent() <= kBatteryChargeCapPercent);
    previous = tracker.percent();
  }
  // Thirty minutes at the prior 93.6 s per point is about 19 points.
  TEST_ASSERT_TRUE(tracker.percent() >= 78 && tracker.percent() <= 80);
  TEST_ASSERT_TRUE(tracker.charging());

  // The LED goes dark with the cell at 4.10 V: charged, and the percent reads 100.
  tracker.update(kMvAt95, false, nowMs, 0, true);
  tracker.update(kMvAt95, false, nowMs + 1000UL, 0, true);
  tracker.update(kMvAt95, false, nowMs + 2000UL, 0, true);
  TEST_ASSERT_TRUE(tracker.chargeDone());
  TEST_ASSERT_EQUAL_INT(100, tracker.percent());
}

void test_gauge_load_takes_the_charge_out_at_its_rate() {
  // 80 mAh is about 6 points of the 1300 mAh cell: 70 to 64. The voltage falls with it, from 70% (3.870 V at rest,
  // 3.858 V with 80 mA of sag) to 64% (3.816 V), so it agrees.
  BatteryTracker tracker;
  tracker.restore(storedAt(70), kShortOffMs);
  tracker.update(3858, false, 0, 80);
  for (uint32_t second = 1; second <= 3600; ++second) {
    tracker.update(3858U - second * 42U / 3600U, false, second * 1000UL, 80);
  }
  TEST_ASSERT_TRUE(tracker.estimatePercent() >= 63 && tracker.estimatePercent() <= 65);
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
  BatteryGaugeState stored = storedAt(40);
  stored.chargeMsPerPercent = 20000U;
  BatteryTracker tracker;
  tracker.restore(stored, kShortOffMs);
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

void test_gauge_shown_percent_rises_on_battery_only_past_a_margin() {
  // The estimate rises 2 points after a correction: the shown percent holds. At 4 points above it, it climbs, one
  // point per two minutes.
  BatteryTracker tracker;
  tracker.restore(storedAt(50), kShortOffMs);
  tracker.update(kMvAt50, false, 0, 3);
  TEST_ASSERT_EQUAL_INT(50, tracker.percent());
  uint32_t nowMs = 0;
  // 3755 mV rests at 52.5%: an hour asleep pulls the estimate up a little.
  for (uint32_t second = 1; second <= 3600; ++second) tracker.update(3752, false, nowMs += 1000UL, 3);
  TEST_ASSERT_TRUE(tracker.estimatePercent() >= 51 && tracker.estimatePercent() <= 52);
  TEST_ASSERT_EQUAL_INT(50, tracker.percent());
  for (uint32_t second = 1; second <= 7200; ++second) tracker.update(3800, false, nowMs += 1000UL, 3);
  TEST_ASSERT_TRUE(tracker.estimatePercent() >= 57);
  TEST_ASSERT_TRUE(tracker.percent() > 50 && tracker.percent() <= tracker.estimatePercent());
}

void test_gauge_gap_longer_than_a_step_is_not_load() {
  // A 10 s stall counts as at most kBatteryMaxStepMs of load. 3600 mA for 5 s is 5 mAh, 5000 ppm of 1000 mAh.
  BatteryGauge gauge;
  gauge.setCapacity(1000000UL);
  gauge.update(kMvAt50, false, false, 3600, 0);
  const uint32_t before = gauge.state().chargePpm;
  gauge.update(kMvAt50, false, false, 3600, 10000);
  TEST_ASSERT_UINT32_WITHIN(2U, before - 5000U, gauge.state().chargePpm);
}

void test_gauge_time_left_on_battery_follows_the_load() {
  // 50% of 1300 mAh is 650 mAh; at 100 mA that is 390 minutes, shown in whole hours from three hours.
  BatteryTracker tracker;
  tracker.restore(storedAt(50), kShortOffMs);
  tracker.update(kMvAt50, false, 0, 100);
  TEST_ASSERT_EQUAL_INT32(420, tracker.minutesRemaining());
  TEST_ASSERT_FALSE(tracker.estimateApproximate());
  // No load known: no time.
  BatteryTracker unknown;
  unknown.update(kMvAt50, false, 0);
  TEST_ASSERT_EQUAL_INT32(-1, unknown.minutesRemaining());
}

void test_gauge_time_left_counts_down_with_the_clock() {
  // Awake on a cell the voltage agrees with, the shown time falls by about the time that passed.
  SimulatedCell cell;
  cell.share = 0.08;
  cell.capacityMah = 1300.0;
  BatteryTracker tracker;
  tracker.restore(storedAt(8), kShortOffMs);
  uint32_t nowMs = 0;
  tracker.update(cell.step(80), false, nowMs, 80);
  const int32_t first = tracker.minutesRemaining();
  TEST_ASSERT_TRUE(first >= 75 && first <= 80);
  for (uint32_t second = 1; second <= 1800; ++second) tracker.update(cell.step(80), false, nowMs += 1000UL, 80);
  TEST_ASSERT_TRUE(distance(tracker.minutesRemaining(), first - 30) <= 6);
}

void test_gauge_time_left_while_charging_uses_the_timed_rate() {
  // 60 points at the prior 93.6 s each is about 94 minutes, shown as 1h 30m and marked approximate.
  BatteryTracker tracker;
  tracker.update(kMvAt40, false, 0);
  tracker.update(kMvAt40, true, 1000);
  tracker.update(kMvAt40, true, 2000);
  TEST_ASSERT_TRUE(tracker.charging());
  TEST_ASSERT_TRUE(tracker.estimateApproximate());
  TEST_ASSERT_EQUAL_INT32(90, tracker.minutesRemaining());
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

void test_gauge_stored_full_charge_is_not_held_off_the_charger() {
  // The charged hold needs the charger. Off it after a night, 4.10 V reads 95%, not the stored 100%.
  BatteryTracker tracker;
  tracker.restore(storedAt(100), kLongOffMs);
  tracker.update(kMvAt95, false, 0, 3);
  TEST_ASSERT_TRUE(distance(tracker.percent(), 95) <= 2);
  TEST_ASSERT_FALSE(tracker.chargeDone());
}

void test_gauge_stored_full_charge_is_held_on_the_charger_at_boot() {
  // Booted on the charger with the cell full and the LED dark: charged, at 100%, as after a charge.
  BatteryTracker tracker;
  tracker.restore(storedAt(100), kLongOffMs);
  tracker.update(kMvAt95, false, 0, 65, true);
  TEST_ASSERT_TRUE(tracker.chargeDone());
  TEST_ASSERT_EQUAL_INT(100, tracker.percent());
}

void test_gauge_stored_charge_is_checked_while_charging() {
  // The charger only raises the voltage: a reading of about 22% still replaces the stored 80%.
  BatteryTracker tracker;
  tracker.restore(storedAt(80), kLongOffMs);
  tracker.update(3590, true, 0);
  TEST_ASSERT_TRUE(tracker.charging());
  TEST_ASSERT_TRUE(distance(tracker.estimatePercent(), 22) <= 1);
  // A higher reading does not raise a stored charge: it is the charger's lift.
  BatteryTracker high;
  high.restore(storedAt(40), kLongOffMs);
  high.update(3950, true, 0);
  TEST_ASSERT_EQUAL_INT(40, high.estimatePercent());
}

void test_tracker_boot_refusal_follows_the_gauge_not_a_sagged_voltage() {
  // A cell the gauge holds at 25% reads 12% under load a minute after a power off. The sag does not refuse it.
  BatteryTracker sagged;
  sagged.restore(storedAt(25), kShortOffMs);
  sagged.update(3510, false, 0);
  TEST_ASSERT_TRUE(sagged.estimatePercent() >= 23 && sagged.estimatePercent() <= 25);
  TEST_ASSERT_TRUE(sagged.refusePercent() > kBatteryLowPercent);
}

void test_tracker_boot_refusal_at_the_voltage_floor() {
  // At the floor the cell is empty, whatever the estimate says.
  BatteryTracker atFloor;
  atFloor.restore(storedAt(15), kShortOffMs);
  atFloor.update(kBatteryFloorMv, false, 0);
  TEST_ASSERT_EQUAL_INT(0, atFloor.refusePercent());
}

void test_tracker_boot_refusal_of_a_fresh_low_cell() {
  // A first boot takes the voltage as the charge, so a cell at 3.45 V is low and is refused.
  BatteryTracker fresh;
  fresh.update(3450, false, 0);
  TEST_ASSERT_TRUE(fresh.refusePercent() <= kBatteryLowPercent);
}

void test_tracker_state_resumes_the_same_estimate() {
  BatteryTracker first;
  first.update(kMvAt50, false, 0, 100);
  for (uint32_t second = 1; second <= 120; ++second) {
    first.update(kMvAt50, false, second * 1000UL, 100);
  }
  BatteryTracker resumed;
  resumed.restore(first.state(), kShortOffMs);
  resumed.update(kMvAt50, false, 0, 100);
  TEST_ASSERT_EQUAL_INT(first.estimatePercent(), resumed.estimatePercent());
  TEST_ASSERT_EQUAL_INT(first.minutesRemaining(), resumed.minutesRemaining());
}

void test_gauge_capacity_change_keeps_the_percent() {
  // The charge is a share, so the same percent is shown on a bigger cell.
  BatteryTracker tracker;
  tracker.update(kMvAt50 - kSag65, false, 0, 65);
  TEST_ASSERT_EQUAL_INT(50, tracker.percent());
  tracker.setCapacity(2000000UL);
  TEST_ASSERT_EQUAL_INT(50, tracker.estimatePercent());
  TEST_ASSERT_EQUAL_INT(50, tracker.percent());
}

void test_gauge_stored_charge_is_a_share_of_the_capacity() {
  // A stored 50% is 50% of a 2000 mAh cell, which lasts about 923 minutes at 65 mA: shown as 15 hours.
  BatteryTracker tracker;
  tracker.setCapacity(2000000UL);
  tracker.restore(storedAt(50), kShortOffMs);
  tracker.update(kMvAt50 - kSag65, false, 0, 65);
  TEST_ASSERT_EQUAL_INT(50, tracker.percent());
  TEST_ASSERT_EQUAL_INT32(900, tracker.minutesRemaining());
}

void test_gauge_stored_rate_is_kept_on_the_default_scale() {
  // A rate learned on the reference cell is stored unchanged, whatever capacity it is read with.
  BatteryGaugeState stored = storedAt(50);
  stored.chargeMsPerPercent = kBatteryPriorChargeMsPerPercent;
  stored.loadScalePpm = 1200000U;
  BatteryTracker tracker;
  tracker.setCapacity(2000000UL);
  tracker.restore(stored, kShortOffMs);
  tracker.update(kMvAt50 - kSag65, false, 0, 65);
  const BatteryGaugeState saved = tracker.state();
  TEST_ASSERT_UINT32_WITHIN(1000U, stored.chargePpm, saved.chargePpm);
  TEST_ASSERT_EQUAL_UINT32(stored.chargeMsPerPercent, saved.chargeMsPerPercent);
  TEST_ASSERT_EQUAL_UINT32(stored.loadScalePpm, saved.loadScalePpm);
}

void test_gauge_rate_learned_on_a_bigger_cell_is_kept_on_the_default_scale() {
  // A 2000 mAh cell takes about 1.54 times as long per point at the same charger current: 40% to full is about
  // 5538 s. Stored on the reference cell's scale, the rate reads as the same 60 s per point.
  BatteryTracker tracker;
  tracker.setCapacity(2000000UL);
  tracker.update(kMvAt40, false, 0);
  uint32_t nowMs = 0;
  for (uint32_t second = 1; second <= 5538; ++second) {
    nowMs = second * 1000UL;
    tracker.update(kMvAt40, true, nowMs);
  }
  tracker.update(kMvAt95, false, nowMs + 1000UL, 0, true);
  tracker.update(kMvAt95, false, nowMs + 2000UL, 0, true);
  tracker.update(kMvAt95, false, nowMs + 3000UL, 0, true);
  const uint32_t learned = tracker.state().chargeMsPerPercent;
  TEST_ASSERT_TRUE(learned >= 58000U && learned <= 62000U);
  TEST_ASSERT_EQUAL_INT(100, tracker.percent());
}

void test_battery_capacity_is_typed_within_100_to_9999_mah() {
  TEST_ASSERT_FALSE(batteryCapacityAccepted(99));
  TEST_ASSERT_TRUE(batteryCapacityAccepted(100));
  TEST_ASSERT_TRUE(batteryCapacityAccepted(9999));
  TEST_ASSERT_EQUAL_UINT16(kBatteryDefaultCapacityMah, batteryCapacityFromStored(0));
  TEST_ASSERT_EQUAL_UINT16(2500, batteryCapacityFromStored(2500));
  TEST_ASSERT_EQUAL_UINT32(2500000UL, batteryCapacityUah(2500));
}

void test_battery_time_left_rounds_with_hysteresis() {
  TEST_ASSERT_EQUAL_INT32(-1, batteryRoundedMinutes(-1.0, 40));
  TEST_ASSERT_EQUAL_INT32(7, batteryRoundedMinutes(7.4, -1));
  TEST_ASSERT_EQUAL_INT32(40, batteryRoundedMinutes(41.0, -1));
  TEST_ASSERT_EQUAL_INT32(135, batteryRoundedMinutes(131.0, -1));
  TEST_ASSERT_EQUAL_INT32(840, batteryRoundedMinutes(845.0, -1));
  TEST_ASSERT_EQUAL_INT32(2592, batteryRoundedMinutes(2600.0, -1));
  TEST_ASSERT_EQUAL_INT32(kBatteryEstimateMaxMinutes, batteryRoundedMinutes(500000.0, -1));
  // A value near a step's edge keeps the shown step until it is clearly past it.
  TEST_ASSERT_EQUAL_INT32(840, batteryRoundedMinutes(805.0, 840));
  TEST_ASSERT_EQUAL_INT32(780, batteryRoundedMinutes(800.0, 840));
  // Falling below an hour uses the finer step at once.
  TEST_ASSERT_EQUAL_INT32(55, batteryRoundedMinutes(54.0, 60));
}

void registerBatteryGaugeTests() {
  RUN_TEST(test_gauge_time_left_is_steady_after_boot);
  RUN_TEST(test_gauge_time_left_moves_slowly_when_the_load_changes);
  RUN_TEST(test_gauge_percent_follows_a_cell_whose_load_is_underestimated);
  RUN_TEST(test_gauge_learns_the_load_scale_over_a_discharge);
  RUN_TEST(test_gauge_learning_span_carries_over_a_short_off);
  RUN_TEST(test_gauge_learning_span_is_dropped_after_an_unknown_off);
  RUN_TEST(test_gauge_percent_never_rises_on_a_noisy_cell);
  RUN_TEST(test_gauge_boot_after_a_long_off_trusts_the_resting_voltage);
  RUN_TEST(test_gauge_boot_after_a_long_off_weighs_both_when_they_nearly_agree);
  RUN_TEST(test_gauge_boot_after_a_short_off_keeps_the_stored_charge);
  RUN_TEST(test_gauge_boot_with_an_unknown_time_off_leans_on_the_voltage);
  RUN_TEST(test_gauge_unplugging_a_full_cell_does_not_jump);
  RUN_TEST(test_battery_capacity_is_typed_within_100_to_9999_mah);
  RUN_TEST(test_battery_time_left_rounds_with_hysteresis);
  RUN_TEST(test_gauge_capacity_change_keeps_the_percent);
  RUN_TEST(test_gauge_stored_charge_is_a_share_of_the_capacity);
  RUN_TEST(test_gauge_stored_rate_is_kept_on_the_default_scale);
  RUN_TEST(test_gauge_rate_learned_on_a_bigger_cell_is_kept_on_the_default_scale);
  RUN_TEST(test_gauge_charger_voltage_does_not_move_the_percent);
  RUN_TEST(test_gauge_load_takes_the_charge_out_at_its_rate);
  RUN_TEST(test_gauge_full_charge_teaches_the_rate);
  RUN_TEST(test_gauge_done_at_boot_teaches_nothing);
  RUN_TEST(test_gauge_charge_stops_at_99_until_the_charger_is_done);
  RUN_TEST(test_gauge_shown_percent_moves_at_most_once_per_interval);
  RUN_TEST(test_gauge_shown_percent_rises_on_battery_only_past_a_margin);
  RUN_TEST(test_gauge_gap_longer_than_a_step_is_not_load);
  RUN_TEST(test_gauge_time_left_on_battery_follows_the_load);
  RUN_TEST(test_gauge_time_left_counts_down_with_the_clock);
  RUN_TEST(test_gauge_time_left_while_charging_uses_the_timed_rate);
  RUN_TEST(test_tracker_voltage_floor_is_critical_whatever_the_estimate);
  RUN_TEST(test_gauge_stored_full_charge_is_not_held_off_the_charger);
  RUN_TEST(test_gauge_stored_full_charge_is_held_on_the_charger_at_boot);
  RUN_TEST(test_gauge_stored_charge_is_checked_while_charging);
  RUN_TEST(test_tracker_boot_refusal_follows_the_gauge_not_a_sagged_voltage);
  RUN_TEST(test_tracker_boot_refusal_at_the_voltage_floor);
  RUN_TEST(test_tracker_boot_refusal_of_a_fresh_low_cell);
  RUN_TEST(test_tracker_state_resumes_the_same_estimate);
}
