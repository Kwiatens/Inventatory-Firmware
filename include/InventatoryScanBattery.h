// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// Battery state for the single LiPo cell: the voltage to percent curve, the voltage guards and a
// gauge that estimates the charge left. The gauge takes the charge out by the load the firmware
// draws, and corrects the estimate from the cell voltage only at rest, where that voltage follows
// the open-circuit voltage. A charge is timed rather than read from the voltage, so the charger's
// raised voltage never moves the percent. No Arduino calls, so the host tests run the same code
// the device runs.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace inventatory_scan {

// Cell voltage (millivolts) to charge (percent) for one LiPo cell under light
// load. The curve is approximate and should be calibrated on the device with a
// full discharge test.
struct BatteryCurvePoint {
  uint16_t millivolts;
  uint8_t percent;
};

constexpr BatteryCurvePoint kBatteryCurve[] = {
    {4200, 100}, {4100, 90}, {4000, 78}, {3900, 62}, {3800, 45},
    {3700, 28},  {3600, 14}, {3500, 6},  {3400, 2},  {3300, 0},
};

// Single-return, recursive form: the ESP32 toolchain compiles constexpr under C++11 rules.
constexpr std::size_t kBatteryCurveCount = sizeof(kBatteryCurve) / sizeof(kBatteryCurve[0]);
constexpr bool batteryCurveStepFalls(std::size_t index) {
  return index + 1U >= kBatteryCurveCount
             ? true
             : (kBatteryCurve[index].millivolts > kBatteryCurve[index + 1U].millivolts &&
                kBatteryCurve[index].percent > kBatteryCurve[index + 1U].percent) &&
                   batteryCurveStepFalls(index + 1U);
}
static_assert(batteryCurveStepFalls(0), "battery curve must fall in both voltage and percent");

// A reading outside this range is an unconnected or unreadable divider, or a wrong
// reading, not a cell. Such readings never warn or power off, so a board without the
// divider still boots. A LiPo cell tops out at 4.2 V (4.35 V for high-voltage cells).
constexpr uint32_t kBatteryMinimumValidMv = 2500;
constexpr uint32_t kBatteryMaximumValidMv = 4500;
constexpr uint8_t kBatteryLowPercent = 10;
constexpr uint8_t kBatteryCriticalPercent = 3;
// Undocked, a cell at or below this voltage is empty, whatever the gauge says. It is the 0%
// point of kBatteryCurve.
constexpr uint32_t kBatteryFloorMv = 3300;
// Consecutive one-second samples below a threshold before it counts. Radio
// bursts sag the cell for a moment; one low reading must not switch the device off.
constexpr uint8_t kBatteryTripSamples = 4;
// The charger line must read the new state this many samples in a row before the state
// changes. A dock contact that bounces must not restart the charge or re-show the card.
constexpr uint8_t kBatteryChargerDebounceSamples = 2;
// Observed time on battery needed before the time left is shown without a "~".
constexpr uint32_t kBatteryEstimateMinSpanMs = 300000UL;
constexpr int32_t kBatteryEstimateMaxMinutes = 9999;

// Gauge calibration. These are placeholders until the bench measurements of the battery plan are
// in: the capacity of the cell, the current the firmware draws in each state, the rest time and
// the charge rate. Replace them on the device.
// The cell is a 3.7 V nominal LiPo of 1300 mAh. Its usable capacity still needs a discharge test.
constexpr uint32_t kBatteryNominalCapacityUah = 1300000UL;
// Average current in milliamps for each state. Together they are about 80 mA with everything on.
constexpr uint32_t kBatteryLoadStandbyMa = 3;
constexpr uint32_t kBatteryLoadBaseMa = 45;
constexpr uint32_t kBatteryLoadBacklightMa = 15;
constexpr uint32_t kBatteryLoadWifiMa = 15;
constexpr uint32_t kBatteryLoadScannerMa = 5;
// Time constant of the average load behind the time left.
constexpr uint32_t kBatteryLoadAverageMs = 600000UL;
// The longest step the gauge takes at once. A longer gap is a stall or a sleep, not load.
constexpr uint32_t kBatteryMaxStepMs = 5000UL;
// At or below this load the cell counts as resting.
constexpr uint32_t kBatteryRestLoadMa = 5;
// Continuous rest before the voltage is read as open-circuit.
constexpr uint32_t kBatteryRestDwellMs = 3600000UL;
// The open-circuit reading corrects the gauge only between these percents. The ends are anchored
// by the empty and full events instead.
constexpr uint8_t kBatteryRestTrustMinPercent = 10;
constexpr uint8_t kBatteryRestTrustMaxPercent = 90;
// A correction starts when the reading differs by this much, and moves the gauge one point per
// kBatteryRestStepMs.
constexpr uint8_t kBatteryRestTolerancePercent = 2;
constexpr uint32_t kBatteryRestStepMs = 60000UL;
// Charging is timed until a full charge has been measured on this cell. This is a guess to replace.
// The prior assumes the 1300 mAh cell charged at 500 mA: 2.6 h, or 93.6 s per point.
constexpr uint32_t kBatteryPriorChargeMsPerPercent = 93600UL;
// A learned rate outside these bounds is not trusted.
constexpr uint32_t kBatteryChargeMsPerPercentMin = 20000UL;
constexpr uint32_t kBatteryChargeMsPerPercentMax = 600000UL;
// A charge teaches its rate only when it covers at least this many points.
constexpr uint8_t kBatteryChargeLearnMinPoints = 20;
// While charging the estimate stops here until the charger reports that charging is complete.
constexpr uint8_t kBatteryChargeCapPercent = 99;
// The shown percent moves by at most one point per this interval.
constexpr uint32_t kBatteryDisplayStepMs = 30000UL;

inline uint8_t batteryPercentFromMillivolts(uint32_t millivolts) {
  constexpr std::size_t kPoints = kBatteryCurveCount;
  if (millivolts >= kBatteryCurve[0].millivolts) return kBatteryCurve[0].percent;
  for (std::size_t index = 0; index + 1 < kPoints; ++index) {
    const BatteryCurvePoint& upper = kBatteryCurve[index];
    const BatteryCurvePoint& lower = kBatteryCurve[index + 1];
    if (millivolts >= lower.millivolts) {
      const uint32_t span = upper.millivolts - lower.millivolts;
      const uint32_t above = millivolts - lower.millivolts;
      const uint32_t percentSpan = upper.percent - lower.percent;
      return static_cast<uint8_t>(lower.percent + (above * percentSpan + span / 2U) / span);
    }
  }
  return kBatteryCurve[kPoints - 1].percent;
}

enum class BatteryLevel : uint8_t {
  Ok,
  Low,
  Critical,
};

// The charger's two status LEDs. Red lit: charging. Green lit: charging complete. Neither
// lit: on battery, or the charger has not started. Red wins if both read lit.
enum class ChargeState : uint8_t {
  Unplugged,
  Charging,
  Done,
};

inline ChargeState chargeStateFromLeds(bool redLit, bool greenLit) {
  if (redLit) return ChargeState::Charging;
  if (greenLit) return ChargeState::Done;
  return ChargeState::Unplugged;
}

// What the gauge keeps across a power-off. A charge rate of zero means no full charge has been
// timed yet, so the prior rate applies.
struct BatteryGaugeState {
  uint32_t remainingUah = 0;
  uint32_t chargeMsPerPercent = 0;
  uint32_t averageLoadCentiMa = 0;
};

// Estimates the charge left in the cell. Every second it takes out the charge the load used, or
// puts in the charge of a timed charge. At rest the open-circuit voltage corrects the estimate
// slowly. The estimate drives the warnings and the time left. The shown percent follows the
// estimate at most one point per kBatteryDisplayStepMs, and while charging it never falls.
class BatteryGauge {
 public:
  // Resumes from a stored state. The first update then keeps the stored charge instead of
  // reading it from the voltage.
  void restore(const BatteryGaugeState& state) {
    restored_ = true;
    remainingUah_ = std::min<uint64_t>(state.remainingUah, kBatteryNominalCapacityUah);
    chargeLearned_ = state.chargeMsPerPercent != 0;
    if (chargeLearned_) chargeMsPerPercent_ = state.chargeMsPerPercent;
    averageCentiMa_ = state.averageLoadCentiMa;
    // A stored average is an observed one, so the time left shows without "~" from the start.
    if (averageCentiMa_ != 0) observedMs_ = kBatteryEstimateMinSpanMs;
  }

  // One valid reading, once a second. charging and chargeDone are already debounced. loadMilliamps
  // is the current the firmware draws now, 0 when unknown.
  void update(uint32_t millivolts, bool charging, bool chargeDone, uint32_t loadMilliamps, uint32_t nowMs) {
    const uint32_t stepMs = started_ ? std::min(nowMs - lastMs_, kBatteryMaxStepMs) : 0U;
    lastMs_ = nowMs;
    if (!started_) start(millivolts, loadMilliamps);
    if (charging && !wasCharging_) beginCharge();
    wasCharging_ = charging;

    if (charging) {
      if (chargeDone) {
        remainingUah_ = kBatteryNominalCapacityUah;
        if (chargeTiming_) learnCharge();
      } else {
        chargeElapsedMs_ += stepMs;
        addCharge(stepMs);
      }
      restMs_ = 0;
      restStepMs_ = 0;
    } else {
      removeCharge(stepMs, loadMilliamps);
      trackAverage(stepMs, loadMilliamps);
      if (observedMs_ < kBatteryEstimateMinSpanMs) observedMs_ += stepMs;
      trackRest(stepMs, loadMilliamps);
      correctAtRest(millivolts, stepMs);
    }
    updateShown(stepMs, charging && chargeDone);
  }

  // The estimate, rounded, 0 to 100.
  int estimatePercent() const {
    const uint64_t percent = (remainingUah_ * 100U + kBatteryNominalCapacityUah / 2U) / kBatteryNominalCapacityUah;
    return static_cast<int>(std::min<uint64_t>(percent, 100U));
  }
  int shownPercent() const { return shownPercent_; }
  bool started() const { return started_; }
  // True while the minutes rest on the prior charge rate, or on less than kBatteryEstimateMinSpanMs
  // of observed load.
  bool estimateApproximate() const {
    return wasCharging_ ? !chargeLearned_ : observedMs_ < kBatteryEstimateMinSpanMs;
  }
  // The minutes to empty on battery, or to full while charging. -1 while there is nothing to base
  // them on.
  int32_t minutesRemaining() const {
    if (!started_) return -1;
    uint64_t minutes = 0;
    if (wasCharging_) {
      minutes = static_cast<uint64_t>(100 - estimatePercent()) * chargeMsPerPercent_ / 60000U;
    } else {
      if (averageCentiMa_ == 0) return -1;
      // uAh / (hundredths of a milliamp) x 6 gives minutes.
      minutes = remainingUah_ * 6U / averageCentiMa_;
    }
    return minutes > static_cast<uint64_t>(kBatteryEstimateMaxMinutes) ? kBatteryEstimateMaxMinutes
                                                                       : static_cast<int32_t>(minutes);
  }
  BatteryGaugeState state() const {
    BatteryGaugeState result;
    result.remainingUah = static_cast<uint32_t>(remainingUah_);
    result.chargeMsPerPercent = chargeLearned_ ? chargeMsPerPercent_ : 0U;
    result.averageLoadCentiMa = averageCentiMa_;
    return result;
  }

 private:
  // The first reading takes the cell voltage as the charge, unless a stored state was restored.
  void start(uint32_t millivolts, uint32_t loadMilliamps) {
    started_ = true;
    if (!restored_) {
      const uint64_t percent = batteryPercentFromMillivolts(millivolts);
      remainingUah_ = static_cast<uint64_t>(kBatteryNominalCapacityUah) * percent / 100U;
    }
    if (averageCentiMa_ == 0) averageCentiMa_ = loadMilliamps * 100U;
    shownPercent_ = estimatePercent();
  }

  void beginCharge() {
    chargeTiming_ = true;
    chargeElapsedMs_ = 0;
    chargeStartPercent_ = estimatePercent();
  }

  // A full scale of 100 points takes 100 x chargeMsPerPercent_ milliseconds. The remainder is kept,
  // so short steps add up.
  void addCharge(uint32_t stepMs) {
    const uint64_t cap = static_cast<uint64_t>(kBatteryNominalCapacityUah) * kBatteryChargeCapPercent / 100U;
    if (remainingUah_ >= cap) return;
    const uint64_t fullScaleMs = static_cast<uint64_t>(chargeMsPerPercent_) * 100U;
    chargeRemainder_ += static_cast<uint64_t>(kBatteryNominalCapacityUah) * stepMs;
    const uint64_t added = chargeRemainder_ / fullScaleMs;
    chargeRemainder_ %= fullScaleMs;
    remainingUah_ = std::min(cap, remainingUah_ + added);
  }

  // Called at the done event. The rate is the average time per point since the charge began.
  void learnCharge() {
    chargeTiming_ = false;
    const int points = 100 - chargeStartPercent_;
    if (points < kBatteryChargeLearnMinPoints) return;
    // A charge faster than the fastest allowed rate is not a real one, such as a boot that
    // finds the charger already done.
    if (chargeElapsedMs_ < static_cast<uint64_t>(kBatteryChargeMsPerPercentMin) * points) return;
    const uint64_t sample = chargeElapsedMs_ / static_cast<uint64_t>(points);
    const uint64_t value = chargeLearned_ ? (static_cast<uint64_t>(chargeMsPerPercent_) * 3U + sample) / 4U : sample;
    chargeMsPerPercent_ = static_cast<uint32_t>(std::min<uint64_t>(
        std::max<uint64_t>(value, kBatteryChargeMsPerPercentMin), kBatteryChargeMsPerPercentMax));
    chargeLearned_ = true;
  }

  // Takes out the charge the load used. mA x ms / 3600 is microamp-hours, and the remainder is kept.
  void removeCharge(uint32_t stepMs, uint32_t loadMilliamps) {
    dischargeRemainder_ += static_cast<uint64_t>(loadMilliamps) * stepMs;
    const uint64_t used = dischargeRemainder_ / 3600U;
    dischargeRemainder_ %= 3600U;
    remainingUah_ = used >= remainingUah_ ? 0 : remainingUah_ - used;
  }

  // The average load, as an exponential average in hundredths of a milliamp.
  void trackAverage(uint32_t stepMs, uint32_t loadMilliamps) {
    const int64_t sample = static_cast<int64_t>(loadMilliamps) * 100;
    const int64_t average = static_cast<int64_t>(averageCentiMa_);
    averageCentiMa_ = static_cast<uint32_t>(
        average + (sample - average) * static_cast<int64_t>(stepMs) / static_cast<int64_t>(kBatteryLoadAverageMs));
  }

  void trackRest(uint32_t stepMs, uint32_t loadMilliamps) {
    if (loadMilliamps > kBatteryRestLoadMa) {
      restMs_ = 0;
    } else if (restMs_ < kBatteryRestDwellMs) {
      restMs_ += stepMs;
    }
  }

  // After the rest dwell, the open-circuit voltage corrects the estimate, one point per
  // kBatteryRestStepMs, while it differs by the tolerance. At or below the floor the cell is empty.
  void correctAtRest(uint32_t millivolts, uint32_t stepMs) {
    if (restMs_ < kBatteryRestDwellMs) {
      restStepMs_ = 0;
      return;
    }
    if (millivolts <= kBatteryFloorMv) {
      remainingUah_ = 0;
      restStepMs_ = 0;
      return;
    }
    const int target = batteryPercentFromMillivolts(millivolts);
    const int estimate = estimatePercent();
    const bool trusted = target >= kBatteryRestTrustMinPercent && target <= kBatteryRestTrustMaxPercent;
    if (!trusted || std::abs(target - estimate) < kBatteryRestTolerancePercent) {
      restStepMs_ = 0;
      return;
    }
    restStepMs_ += stepMs;
    if (restStepMs_ < kBatteryRestStepMs) return;
    restStepMs_ = 0;
    const uint64_t step = kBatteryNominalCapacityUah / 100U;
    if (target > estimate) {
      remainingUah_ = std::min<uint64_t>(kBatteryNominalCapacityUah, remainingUah_ + step);
    } else {
      remainingUah_ = remainingUah_ > step ? remainingUah_ - step : 0;
    }
  }

  // The shown percent follows the estimate, one point per kBatteryDisplayStepMs. Done shows 100
  // at once. While charging it never falls, even if the estimate does.
  void updateShown(uint32_t stepMs, bool done) {
    if (done) {
      shownPercent_ = 100;
      displayWaitMs_ = 0;
      return;
    }
    // Time since the shown percent last moved, capped at one interval.
    displayWaitMs_ = std::min(displayWaitMs_ + stepMs, kBatteryDisplayStepMs);
    int target = estimatePercent();
    if (wasCharging_ && target < shownPercent_) target = shownPercent_;
    if (target == shownPercent_ || displayWaitMs_ < kBatteryDisplayStepMs) return;
    displayWaitMs_ = 0;
    shownPercent_ += target > shownPercent_ ? 1 : -1;
  }

  uint64_t remainingUah_ = 0;
  uint32_t chargeMsPerPercent_ = kBatteryPriorChargeMsPerPercent;
  bool chargeLearned_ = false;
  uint32_t averageCentiMa_ = 0;
  uint32_t observedMs_ = 0;
  uint32_t restMs_ = 0;
  uint32_t restStepMs_ = 0;
  uint32_t displayWaitMs_ = 0;
  uint32_t lastMs_ = 0;
  uint64_t chargeElapsedMs_ = 0;
  uint64_t chargeRemainder_ = 0;
  uint64_t dischargeRemainder_ = 0;
  int chargeStartPercent_ = 0;
  int shownPercent_ = -1;
  bool chargeTiming_ = false;
  bool wasCharging_ = false;
  bool started_ = false;
  bool restored_ = false;
};

// Tracks the battery from one reading per second. The voltage guards the readings, the charger
// state is debounced, and the percent comes from the gauge. Timestamps are millis() values;
// differences use unsigned arithmetic, so they survive the 32-bit wrap.
class BatteryTracker {
 public:
  // Resumes the gauge from a stored state. Call it before the first update.
  void restore(const BatteryGaugeState& state) { gauge_.restore(state); }

  // chargeDone: the charger reports that charging is complete. Like `charging`, it must read the new
  // state for kBatteryChargerDebounceSamples samples in a row before it changes.
  // loadMilliamps: the current the firmware draws now, 0 when unknown.
  void update(uint32_t millivolts, bool charging, uint32_t nowMs, bool chargeDone = false,
              uint32_t loadMilliamps = 0) {
    if (millivolts < kBatteryMinimumValidMv || millivolts > kBatteryMaximumValidMv) {
      valid_ = false;
      if (invalidSamples_ < 255U) ++invalidSamples_;
      lowSamples_ = 0;
      criticalSamples_ = 0;
      floorSamples_ = 0;
      return;
    }
    invalidSamples_ = 0;
    const bool firstSample = !valid_;
    valid_ = true;
    lastMillivolts_ = millivolts;
    if (firstSample) {
      charging_ = charging;
      chargeDone_ = chargeDone;
      chargerFlipSamples_ = 0;
      doneFlipSamples_ = 0;
    } else if (charging != charging_) {
      if (chargerFlipSamples_ < 255U) ++chargerFlipSamples_;
      if (chargerFlipSamples_ >= kBatteryChargerDebounceSamples) {
        charging_ = charging;
        chargerFlipSamples_ = 0;
      }
    } else {
      chargerFlipSamples_ = 0;
    }
    if (chargeDone == chargeDone_) {
      doneFlipSamples_ = 0;
    } else if (++doneFlipSamples_ >= kBatteryChargerDebounceSamples) {
      chargeDone_ = chargeDone;
      doneFlipSamples_ = 0;
    }
    gauge_.update(millivolts, charging_, chargeDone_, loadMilliamps, nowMs);
    countTrip(lowSamples_, gauge_.estimatePercent() <= kBatteryLowPercent);
    countTrip(criticalSamples_, gauge_.estimatePercent() <= kBatteryCriticalPercent);
    countTrip(floorSamples_, millivolts <= kBatteryFloorMv);
  }

  bool valid() const { return valid_; }
  // True only after kBatteryTripSamples readings in a row were outside the valid range, so a
  // single bad sample never shows a fault.
  bool unreadable() const { return invalidSamples_ >= kBatteryTripSamples; }
  bool charging() const { return valid_ && charging_; }
  // Charging, and the charger says it is complete.
  bool chargeDone() const { return charging() && chargeDone_; }
  // True once a reading has started the gauge.
  bool started() const { return gauge_.started(); }
  // The percent on the display. -1 while no reading has been taken.
  int percent() const { return valid_ ? gauge_.shownPercent() : -1; }
  // The estimate the warnings use. -1 while no reading has been taken.
  int estimatePercent() const { return valid_ ? gauge_.estimatePercent() : -1; }
  // The percent the boot check refuses a cell at: the lower of the estimate and the voltage, so
  // a drifted estimate cannot let a flat cell start. -1 while no reading has been taken.
  int refusePercent() const {
    if (!valid_) return -1;
    return std::min(gauge_.estimatePercent(), static_cast<int>(batteryPercentFromMillivolts(lastMillivolts_)));
  }
  // -1 while there is no reading.
  int32_t minutesRemaining() const { return valid_ ? gauge_.minutesRemaining() : -1; }
  // True while the minutes rest on the prior rate rather than on this cell's observed behaviour.
  bool estimateApproximate() const { return gauge_.estimateApproximate(); }
  BatteryGaugeState state() const { return gauge_.state(); }
  // Discharge thresholds are debounced and never apply while charging. The voltage floor counts
  // like the others: an undocked cell at or below kBatteryFloorMv is critical whatever the gauge says.
  BatteryLevel level() const {
    if (!valid_ || charging_) return BatteryLevel::Ok;
    if (criticalSamples_ >= kBatteryTripSamples || floorSamples_ >= kBatteryTripSamples) {
      return BatteryLevel::Critical;
    }
    if (lowSamples_ >= kBatteryTripSamples) return BatteryLevel::Low;
    return BatteryLevel::Ok;
  }

 private:
  void countTrip(uint8_t& samples, bool belowThreshold) {
    if (!belowThreshold || charging_) {
      samples = 0;
    } else if (samples < 255U) {
      ++samples;
    }
  }

  BatteryGauge gauge_;
  uint32_t lastMillivolts_ = 0;
  bool valid_ = false;
  bool charging_ = false;
  bool chargeDone_ = false;
  uint8_t lowSamples_ = 0;
  uint8_t criticalSamples_ = 0;
  uint8_t floorSamples_ = 0;
  uint8_t chargerFlipSamples_ = 0;
  uint8_t doneFlipSamples_ = 0;
  uint8_t invalidSamples_ = 0;
};

// "42 min", or "1h 05m" from 60 minutes. Returns false if the text does not fit.
inline bool formatBatteryDuration(char* out, std::size_t capacity, uint32_t minutes) {
  char digits[32];
  const int written =
      minutes < 60U ? snprintf(digits, sizeof(digits), "%lu min", static_cast<unsigned long>(minutes))
                    : snprintf(digits, sizeof(digits), "%luh %02lum", static_cast<unsigned long>(minutes / 60U),
                               static_cast<unsigned long>(minutes % 60U));
  if (capacity == 0 || written <= 0 || static_cast<std::size_t>(written) >= capacity) {
    if (capacity > 0) out[0] = '\0';
    return false;
  }
  std::memcpy(out, digits, static_cast<std::size_t>(written) + 1U);
  return true;
}

// The runtime line for the top bar and the battery cards: "Full: 1h 05m" while
// charging, "6h 20m left" on battery, "Fully charged" at 100%. An estimate that is still
// approximate carries a "~". Returns false, and writes nothing useful, while there is no reading.
inline bool formatBatteryEstimate(char* out, std::size_t capacity, int32_t minutes, bool charging,
                                  int percent, bool approximate = false) {
  if (capacity == 0) return false;
  out[0] = '\0';
  if (charging && percent >= 100) {
    snprintf(out, capacity, "Fully charged");
    return true;
  }
  if (minutes < 0) return false;
  char duration[16];
  if (!formatBatteryDuration(duration, sizeof(duration), static_cast<uint32_t>(minutes))) return false;
  const char* const mark = approximate ? "~" : "";
  const int written = snprintf(out, capacity, charging ? "Full: %s%s" : "%s%s left", mark, duration);
  return written > 0 && static_cast<std::size_t>(written) < capacity;
}

}  // namespace inventatory_scan
