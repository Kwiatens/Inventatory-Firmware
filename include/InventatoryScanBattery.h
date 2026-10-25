// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// Single Li-ion cell battery state: voltage-to-percent curve, voltage guards and charge gauge.
// Charge is counted from load and corrected by the load-corrected voltage, never by the charger-raised voltage.
// No Arduino calls, so host tests run the same code.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace inventatory_scan {

// Resting cell millivolts to percent of the usable charge, for an 18650 NCA cell (NCR18650B type, 3400 mAh).
// 0% is the device's floor, kBatteryFloorMv: the cell still holds a few percent below it. Approximate: calibrate
// with a discharge test.
struct BatteryCurvePoint {
  uint16_t millivolts;
  uint8_t percent;
};

constexpr BatteryCurvePoint kBatteryCurve[] = {
    {4170, 100}, {4100, 95}, {4040, 90}, {3990, 85}, {3950, 80}, {3910, 75}, {3870, 70},
    {3835, 65},  {3800, 60}, {3770, 55}, {3740, 50}, {3715, 45}, {3690, 40}, {3665, 35},
    {3640, 30},  {3610, 25}, {3580, 20}, {3545, 15}, {3500, 10}, {3430, 5},  {3300, 0},
};

// Recursive single-return form: the ESP32 toolchain uses C++11 constexpr rules.
constexpr std::size_t kBatteryCurveCount = sizeof(kBatteryCurve) / sizeof(kBatteryCurve[0]);
constexpr bool batteryCurveStepFalls(std::size_t index) {
  return index + 1U >= kBatteryCurveCount
             ? true
             : (kBatteryCurve[index].millivolts > kBatteryCurve[index + 1U].millivolts &&
                kBatteryCurve[index].percent > kBatteryCurve[index + 1U].percent) &&
                   batteryCurveStepFalls(index + 1U);
}
static_assert(batteryCurveStepFalls(0), "battery curve must fall in both voltage and percent");

// Outside this range is a bad divider reading, not a cell: it never warns or powers off,
// so a board without the divider still boots. Cells top out at 4.2 V (4.35 V high-voltage).
constexpr uint32_t kBatteryMinimumValidMv = 2500;
constexpr uint32_t kBatteryMaximumValidMv = 4500;
constexpr uint8_t kBatteryLowPercent = 10;
constexpr uint8_t kBatteryCriticalPercent = 3;
// Undocked, at or below this voltage the cell is empty whatever the gauge says (the 0% point).
constexpr uint32_t kBatteryFloorMv = 3300;
static_assert(kBatteryCurve[kBatteryCurveCount - 1U].millivolts == kBatteryFloorMv, "0% must be the floor");
// Consecutive one-second samples below a threshold before it counts: radio bursts sag the cell.
constexpr uint8_t kBatteryTripSamples = 4;
// Samples a new charger state must hold before it counts: a bouncing dock must not restart charge.
constexpr uint8_t kBatteryChargerDebounceSamples = 2;
// Charged needs the charger plugged in, LED dark and the cell at or above kBatteryChargedMv.
// Leaving needs the lower release value, so a reading at the edge does not flicker.
constexpr uint32_t kBatteryChargedMv = 4000;
constexpr uint32_t kBatteryChargedReleaseMv = 3900;
// The longest time left shown: 99 days.
constexpr int32_t kBatteryEstimateMaxMinutes = 99 * 1440;

// Gauge calibration: placeholders until bench measurements replace them.
// The cell capacity the estimate counts in. It is typed in Device settings / Energy / Battery, in mAh, and starts at
// kBatteryDefaultCapacityMah. The charge rate is kept on the reference cell's scale (kBatteryReferenceCapacityUah),
// which stays fixed: changing it would misread the stored rate.
constexpr uint16_t kBatteryDefaultCapacityMah = 3400;
constexpr uint32_t kBatteryReferenceCapacityUah = 1300000UL;
// The capacities that can be typed, in mAh.
constexpr uint16_t kBatteryCapacityMinMah = 100;
constexpr uint16_t kBatteryCapacityMaxMah = 9999;
static_assert(kBatteryDefaultCapacityMah >= kBatteryCapacityMinMah &&
                  kBatteryDefaultCapacityMah <= kBatteryCapacityMaxMah,
              "default capacity out of range");
// Average current in milliamps for each state. Together they are about 80 mA with everything on.
// The gauge learns one scale for all of them (kBatteryLoadScale*), so an even error corrects itself.
constexpr uint32_t kBatteryLoadStandbyMa = 3;
constexpr uint32_t kBatteryLoadBaseMa = 45;
constexpr uint32_t kBatteryLoadBacklightMa = 15;
constexpr uint32_t kBatteryLoadWifiMa = 15;
constexpr uint32_t kBatteryLoadScannerMa = 5;
// At or below this load the device is asleep: the voltage is near rest, and the time left is not counted down.
constexpr uint32_t kBatteryLightLoadMa = 10;
// The longest step the gauge takes at once. A longer gap is a stall or a sleep, not load.
constexpr uint32_t kBatteryMaxStepMs = 5000UL;
// Cell, protection and UPS module resistance: the load sags the reading by load x this.
constexpr uint32_t kBatteryInternalMilliohms = 150;

// Charge estimate, a one-state Kalman filter. Counting the load predicts the charge; the voltage, corrected for
// the load's sag, measures it. Shares are 0 to 1 of the usable charge; noise is one standard deviation.
// The load guess is this far off, as a share of the load, and stays off this long (a bias, not noise).
constexpr double kBatteryLoadErrorRatio = 0.3;
constexpr double kBatteryLoadErrorHoldS = 1800.0;
// Charge that leaves unseen (self-discharge, leakage), as variance per second: about 1% a day.
constexpr double kBatteryDriftVariancePerS = 0.01 * 0.01 / 86400.0;
// Voltage smoothing before a measurement, and the time between measurements.
constexpr uint32_t kBatteryVoltageSmoothMs = 20000UL;
constexpr uint32_t kBatteryMeasureIntervalMs = 10000UL;
// Measurement noise in millivolts: under load, asleep, and after a long power off (the cell has rested).
// They include the curve and ADC error, not only the reading's noise, so the voltage corrects slowly in use.
constexpr double kBatteryNoiseLoadedMv = 40.0;
constexpr double kBatteryNoiseLightMv = 20.0;
constexpr double kBatteryNoiseRestedMv = 15.0;
// A running measurement moves the charge by at most this many standard deviations: one burst is not a trend.
constexpr double kBatteryInnovationLimit = 3.0;
// Starting uncertainty: no stored charge, a stored charge, and a stored charge after an unknown time off.
constexpr double kBatteryFreshSigma = 0.15;
constexpr double kBatteryStoredSigma = 0.02;
constexpr double kBatteryUnknownOffSigma = 0.10;
// Off for at least this long, the boot reading is a resting voltage. Charge can leave while off (a drain that is
// still to be found), up to about this share in a day off.
constexpr uint32_t kBatteryRestedOffMs = 1800000UL;
constexpr double kBatteryOffDriftPerDay = 0.15;
// kBatteryOffUnknownMs: the time off is not known (a power-on reset, a USB upload).
constexpr uint32_t kBatteryOffUnknownMs = 0xFFFFFFFFUL;
// Above this uncertainty the time left is marked "~".
constexpr double kBatteryApproximateSigma = 0.05;
// The load scale is learned only between these shares, where the curve is trusted, and after this settle time
// since the start or the charger, so a boot or charge correction is not taken for load. The settle pauses a span;
// leaving the shares, charging or a capacity change drops it.
constexpr double kBatteryLearnMinShare = 0.15;
constexpr double kBatteryLearnMaxShare = 0.85;
constexpr uint32_t kBatteryLearnSettleMs = 600000UL;
// A learning span covers this share of counted use. Each span moves the scale a quarter of the way to its finding.
// A span is stored with the charge, so it carries over a short time off; a longer or unknown one drops it.
constexpr double kBatteryLearnSpan = 0.10;
constexpr double kBatteryLearnGain = 0.25;
constexpr uint32_t kBatteryLoadScaleMinPpm = 600000UL;
constexpr uint32_t kBatteryLoadScaleMaxPpm = 1800000UL;

// While charging, the charger lifts the voltage. A stored charge this many points above the voltage's is replaced
// at boot; the voltage can only lower it then.
constexpr uint8_t kBatteryRestoreTolerancePercent = 20;
// Prior charge rate on the reference cell, a guess until measured: 1300 mAh at 500 mA is 2.6 h, 93.6 s per point.
constexpr uint32_t kBatteryPriorChargeMsPerPercent = 93600UL;
// A learned rate outside these bounds is not trusted.
constexpr uint32_t kBatteryChargeMsPerPercentMin = 20000UL;
constexpr uint32_t kBatteryChargeMsPerPercentMax = 600000UL;
// A charge teaches its rate only when it covers at least this many points.
constexpr uint8_t kBatteryChargeLearnMinPoints = 20;
// While charging the estimate stops here until the charger reports that charging is complete.
constexpr uint8_t kBatteryChargeCapPercent = 99;
// The shown percent falls by at most one point per this interval. On battery it rises only after the estimate is
// kBatteryShownRiseMargin points above it, and then one point per the longer interval.
constexpr uint32_t kBatteryDisplayStepMs = 30000UL;
constexpr uint32_t kBatteryDisplayRiseStepMs = 120000UL;
constexpr uint8_t kBatteryShownRiseMargin = 3;
// The shown time left counts down with the clock. Beyond that it moves toward the estimate by at most this share
// of itself, or this many minutes, per minute.
constexpr double kBatteryTimeSlewRatio = 0.03;
constexpr double kBatteryTimeSlewMinMinutes = 1.0;
// Time left averages the load while awake, over this time constant. Asleep time is left out: the time left is
// read on an awake screen, so it means time left awake.
constexpr uint32_t kBatteryAwakeAverageMs = 1200000UL;

// A typed capacity the gauge accepts.
inline bool batteryCapacityAccepted(uint16_t mah) {
  return mah >= kBatteryCapacityMinMah && mah <= kBatteryCapacityMaxMah;
}

// A stored capacity outside the accepted range falls back to the default.
inline uint16_t batteryCapacityFromStored(uint16_t mah) {
  return batteryCapacityAccepted(mah) ? mah : kBatteryDefaultCapacityMah;
}

inline uint32_t batteryCapacityUah(uint16_t mah) {
  return static_cast<uint32_t>(batteryCapacityFromStored(mah)) * 1000UL;
}

// Converts a value counted on one capacity to another, rounded to the nearest unit. A charge rate scales with the
// capacity.
inline uint64_t scaleToCapacity(uint64_t value, uint32_t fromUah, uint32_t toUah) {
  return (value * toUah + fromUah / 2U) / fromUah;
}

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

// The curve segment that holds a share: index of its upper point.
inline std::size_t batteryCurveSegment(double share) {
  std::size_t index = 0;
  while (index + 2U < kBatteryCurveCount && share * 100.0 < kBatteryCurve[index + 1U].percent) ++index;
  return index;
}

// The share (0 to 1) a resting voltage stands for, without rounding.
inline double batteryShareFromMillivolts(double millivolts) {
  if (millivolts >= kBatteryCurve[0].millivolts) return 1.0;
  if (millivolts <= kBatteryCurve[kBatteryCurveCount - 1U].millivolts) return 0.0;
  std::size_t index = 0;
  while (millivolts < kBatteryCurve[index + 1U].millivolts) ++index;
  const BatteryCurvePoint& upper = kBatteryCurve[index];
  const BatteryCurvePoint& lower = kBatteryCurve[index + 1U];
  const double fraction = (millivolts - lower.millivolts) / (upper.millivolts - lower.millivolts);
  return (lower.percent + fraction * (upper.percent - lower.percent)) / 100.0;
}

// The resting voltage of a share.
inline double batteryMillivoltsFromShare(double share) {
  share = std::min(1.0, std::max(0.0, share));
  const std::size_t index = batteryCurveSegment(share);
  const BatteryCurvePoint& upper = kBatteryCurve[index];
  const BatteryCurvePoint& lower = kBatteryCurve[index + 1U];
  const double fraction = (share * 100.0 - lower.percent) / (upper.percent - lower.percent);
  return lower.millivolts + fraction * (upper.millivolts - lower.millivolts);
}

// The curve's slope at a share, in millivolts per whole charge.
inline double batteryCurveSlope(double share) {
  const std::size_t index = batteryCurveSegment(share);
  const BatteryCurvePoint& upper = kBatteryCurve[index];
  const BatteryCurvePoint& lower = kBatteryCurve[index + 1U];
  return (upper.millivolts - lower.millivolts) * 100.0 / (upper.percent - lower.percent);
}

// Rounding step of a shown time left: 1 minute under 10, 5 under an hour, 15 under 3 hours, whole hours under a
// day, a tenth of a day under 10 days, then whole days.
inline uint32_t batteryMinutesStep(double minutes) {
  if (minutes < 10.0) return 1U;
  if (minutes < 60.0) return 5U;
  if (minutes < 180.0) return 15U;
  if (minutes < 1440.0) return 60U;
  if (minutes < 14400.0) return 144U;
  return 1440U;
}

// The shown time left, rounded to its step. The shown value holds until the time is more than 0.6 of a step away,
// so a value at a step's edge does not flicker. -1 for none.
inline int32_t batteryRoundedMinutes(double minutes, int32_t previous) {
  if (minutes < 0.0) return -1;
  if (previous >= 0) {
    const double step = batteryMinutesStep(std::min<double>(minutes, previous));
    const double distance = minutes > previous ? minutes - previous : previous - minutes;
    if (distance < step * 0.6) return previous;
  }
  const uint32_t step = batteryMinutesStep(minutes);
  const double rounded = static_cast<double>(static_cast<uint32_t>(minutes / step + 0.5) * step);
  return rounded > kBatteryEstimateMaxMinutes ? kBatteryEstimateMaxMinutes : static_cast<int32_t>(rounded);
}

enum class BatteryLevel : uint8_t {
  Ok,
  Low,
  Critical,
};

// Charger signals read through 1 MOhm / 1 MOhm midpoints: about 2200 mV high, 0 mV low.
// Between the thresholds the last state holds, so an edge reading does not flicker.
constexpr uint32_t kSignalHighAboveMv = 1600;
constexpr uint32_t kSignalLowBelowMv = 800;

inline bool signalHighNext(bool high, uint32_t midpointMv) {
  return high ? midpointMv >= kSignalLowBelowMv : midpointMv > kSignalHighAboveMv;
}

// Kept across power-off. The charge is a share of the cell, so it does not depend on the capacity setting; the
// charge rate is on the reference cell's scale. A zero rate or scale means none learned yet. The learn fields are
// the load-scale span in progress, in ppm of the cell: the use counted and the voltage's net correction.
constexpr uint32_t kBatteryChargePpmFull = 1000000UL;
// A stored span past this much use is not one the gauge wrote: a span ends at kBatteryLearnSpan.
constexpr uint32_t kBatteryLearnUsedMaxPpm = 2U * static_cast<uint32_t>(kBatteryLearnSpan * kBatteryChargePpmFull);
struct BatteryGaugeState {
  uint32_t chargePpm = 0;
  uint32_t chargeMsPerPercent = 0;
  uint32_t loadScalePpm = 0;
  uint32_t learnUsedPpm = 0;
  int32_t learnCorrectionPpm = 0;
};

// Charge estimate. On battery each second takes out the load's charge, and every kBatteryMeasureIntervalMs the
// load-corrected voltage corrects it, weighted by how far each can be trusted. On the charger it adds a timed
// charge and the raised voltage is not read. The shown percent and time left are smoothed on top.
class BatteryGauge {
 public:
  // Resumes from a stored state; the first update checks it against the voltage. offMs is the time the device
  // was off, or kBatteryOffUnknownMs.
  void restore(const BatteryGaugeState& state, uint32_t offMs = kBatteryOffUnknownMs) {
    restored_ = true;
    share_ = static_cast<double>(std::min(state.chargePpm, kBatteryChargePpmFull)) / kBatteryChargePpmFull;
    chargeLearned_ = state.chargeMsPerPercent != 0;
    if (chargeLearned_) chargeMsPerPercent_ = state.chargeMsPerPercent;
    loadScaleLearned_ = state.loadScalePpm != 0;
    if (loadScaleLearned_) {
      loadScale_ = static_cast<double>(std::min(std::max(state.loadScalePpm, kBatteryLoadScaleMinPpm),
                                                kBatteryLoadScaleMaxPpm)) / 1e6;
    }
    offMs_ = offMs;
    // The boot reading after a short time off is limited and not counted, so the span goes on where it stopped.
    resetLearning();
    if (offMs < kBatteryRestedOffMs && state.learnUsedPpm <= kBatteryLearnUsedMaxPpm) {
      learnUsed_ = static_cast<double>(state.learnUsedPpm) / kBatteryChargePpmFull;
      learnCorrection_ = static_cast<double>(state.learnCorrectionPpm) / kBatteryChargePpmFull;
    }
  }

  // Changes the cell the estimate counts in. The charge is a share, so the percent is kept. A charge being timed
  // is dropped, so no rate is learned across the change.
  void setCapacity(uint32_t capacityUah) {
    if (capacityUah == capacityUah_) return;
    capacityUah_ = capacityUah;
    chargeTiming_ = false;
    resetLearning();
  }

  // One valid reading per second. charging and full are debounced; loadMilliamps 0 means unknown.
  void update(uint32_t millivolts, bool charging, bool full, uint32_t loadMilliamps, uint32_t nowMs) {
    const uint32_t stepMs = started_ ? std::min(nowMs - lastMs_, kBatteryMaxStepMs) : 0U;
    lastMs_ = nowMs;
    if (started_) {
      smoothedMv_ += (static_cast<double>(millivolts) - smoothedMv_) * stepMs / kBatteryVoltageSmoothMs;
    } else {
      start(millivolts, loadMilliamps, charging);
    }
    if (charging && !wasCharging_) beginCharge();
    // Charged only while the charger is not charging the cell.
    const bool done = full && !charging;
    const Mode mode = charging ? Mode::Charging : done ? Mode::Charged : Mode::Discharging;
    // The time left changes meaning with the mode, so the shown value starts again.
    if (mode != mode_) shownMinutes_ = -1.0;
    mode_ = mode;
    wasCharging_ = charging;

    if (charging) {
      chargeElapsedMs_ += stepMs;
      addCharge(stepMs);
      settleMs_ = 0;
      learning_ = false;
      resetLearning();
    } else if (done) {
      // Charged: hold at 100. Once the charger is off, the load counts down from here.
      share_ = 1.0;
      variance_ = std::min(variance_, kBatteryStoredSigma * kBatteryStoredSigma);
      if (chargeTiming_) learnCharge();
      settleMs_ = 0;
      learning_ = false;
      resetLearning();
    } else {
      if (settleMs_ < kBatteryLearnSettleMs) settleMs_ += stepMs;
      const bool inShares = share_ >= kBatteryLearnMinShare && share_ <= kBatteryLearnMaxShare;
      if (!inShares) resetLearning();
      learning_ = inShares && settleMs_ >= kBatteryLearnSettleMs;
      predict(stepMs, loadMilliamps);
      trackAwake(stepMs, loadMilliamps);
      measurementWaitMs_ += stepMs;
      if (measurementWaitMs_ >= kBatteryMeasureIntervalMs) {
        measurementWaitMs_ = 0;
        const double noise = loadMilliamps <= kBatteryLightLoadMa ? kBatteryNoiseLightMv : kBatteryNoiseLoadedMv;
        measure(compensatedMv(smoothedMv_, loadMilliamps), noise, true);
      }
      if (learning_) learnLoadScale();
    }
    updateShownPercent(stepMs);
    updateShownMinutes(stepMs, loadMilliamps);
  }

  // The estimate, rounded, 0 to 100.
  int estimatePercent() const {
    const double percent = share_ * 100.0 + 0.5;
    return percent >= 100.0 ? 100 : static_cast<int>(percent);
  }
  int shownPercent() const { return shownPercent_; }
  bool started() const { return started_; }
  // One standard deviation of the charge estimate, as a share.
  double uncertainty() const { return std::sqrt(variance_); }
  double loadScale() const { return loadScale_; }
  // The load-corrected smoothed voltage the gauge last saw.
  uint32_t compensatedMillivolts() const { return static_cast<uint32_t>(lastCompensatedMv_ + 0.5); }
  // True while the time to full uses the prior rate, or the charge is too uncertain for a firm time left.
  bool estimateApproximate() const {
    return mode_ == Mode::Charging ? !chargeLearned_ : uncertainty() > kBatteryApproximateSigma;
  }
  // The shown minutes to empty (or to full while charging), rounded and smoothed; -1 with nothing to base them on.
  int32_t minutesRemaining() const { return started_ ? displayedMinutes_ : -1; }
  // The same time, unsmoothed, for the log.
  int32_t rawMinutesRemaining() const {
    const double minutes = rawMinutes();
    if (minutes < 0.0) return -1;
    return minutes > kBatteryEstimateMaxMinutes ? kBatteryEstimateMaxMinutes : static_cast<int32_t>(minutes + 0.5);
  }
  BatteryGaugeState state() const {
    BatteryGaugeState result;
    result.chargePpm = static_cast<uint32_t>(share_ * kBatteryChargePpmFull + 0.5);
    result.chargeMsPerPercent = chargeLearned_ ? chargeMsPerPercent_ : 0U;
    result.loadScalePpm = loadScaleLearned_ ? static_cast<uint32_t>(loadScale_ * 1e6 + 0.5) : 0U;
    result.learnUsedPpm = static_cast<uint32_t>(
        std::min(learnUsed_ * kBatteryChargePpmFull + 0.5, static_cast<double>(kBatteryLearnUsedMaxPpm)));
    result.learnCorrectionPpm = static_cast<int32_t>(std::lround(
        std::min(1.0, std::max(-1.0, learnCorrection_)) * kBatteryChargePpmFull));
    return result;
  }

 private:
  enum class Mode : uint8_t { Discharging, Charging, Charged };

  double capacityMah() const { return capacityUah_ / 1000.0; }

  // The charge rate for this cell: the reference cell's rate scaled to the capacity, since a bigger cell takes longer.
  uint32_t actualMsPerPercent() const {
    return static_cast<uint32_t>(scaleToCapacity(chargeMsPerPercent_, kBatteryReferenceCapacityUah, capacityUah_));
  }

  // The resting voltage behind a reading: the load sags it by load x resistance.
  double compensatedMv(double millivolts, uint32_t loadMilliamps) const {
    return millivolts + loadScale_ * loadMilliamps * kBatteryInternalMilliohms / 1000.0;
  }

  // The first reading sets the charge, or checks a stored one. Off for long, the boot reading is a resting voltage
  // and weighs more; a stored charge that disagrees beyond both uncertainties is replaced by the voltage's.
  void start(uint32_t millivolts, uint32_t loadMilliamps, bool charging) {
    started_ = true;
    smoothedMv_ = millivolts;
    const double compensated = compensatedMv(millivolts, loadMilliamps);
    lastCompensatedMv_ = compensated;
    if (!restored_) {
      share_ = batteryShareFromMillivolts(compensated);
      variance_ = kBatteryFreshSigma * kBatteryFreshSigma;
    } else if (charging) {
      // The charger only raises the voltage, so a low voltage counts while charging too.
      const double fromVoltage = batteryShareFromMillivolts(compensated);
      if (share_ - fromVoltage > kBatteryRestoreTolerancePercent / 100.0) share_ = fromVoltage;
      variance_ = kBatteryStoredSigma * kBatteryStoredSigma;
    } else {
      // After a short time off the stored charge is the better guess, and the cell may not have recovered from
      // its load yet, so a disagreeing reading is limited like a running one.
      double sigma = kBatteryStoredSigma;
      double noise = kBatteryNoiseLoadedMv;
      bool shortOff = true;
      if (offMs_ == kBatteryOffUnknownMs) {
        sigma = kBatteryUnknownOffSigma;
        noise = kBatteryNoiseLightMv;
        shortOff = false;
      } else if (offMs_ >= kBatteryRestedOffMs) {
        const double drift = kBatteryOffDriftPerDay * std::min(1.0, offMs_ / 86400000.0);
        sigma = std::sqrt(sigma * sigma + drift * drift);
        noise = kBatteryNoiseRestedMv;
        shortOff = false;
      }
      variance_ = sigma * sigma;
      if (!measure(compensated, noise, shortOff)) {
        share_ = batteryShareFromMillivolts(compensated);
        const double shareNoise = noise / batteryCurveSlope(share_);
        variance_ = shareNoise * shareNoise;
      }
    }
    shownPercent_ = estimatePercent();
  }

  // Kalman update from a resting voltage, read off the curve as a share. The voltage noise becomes share noise
  // through the slope at that voltage, so a flat part of the curve counts for little. A running measurement is
  // limited to kBatteryInnovationLimit deviations; a boot measurement beyond that returns false, since the stored
  // charge and the voltage disagree.
  bool measure(double millivolts, double noiseMv, bool limit) {
    lastCompensatedMv_ = millivolts;
    const double measured = batteryShareFromMillivolts(millivolts);
    const double noise = noiseMv / batteryCurveSlope(measured);
    const double spread = variance_ + noise * noise;
    double innovation = measured - share_;
    const double bound = kBatteryInnovationLimit * std::sqrt(spread);
    if (innovation > bound || innovation < -bound) {
      if (!limit) return false;
      innovation = innovation > 0.0 ? bound : -bound;
    }
    const double gain = variance_ / spread;
    const double before = share_;
    share_ = std::min(1.0, std::max(0.0, share_ + gain * innovation));
    variance_ *= 1.0 - gain;
    if (learning_) learnCorrection_ += share_ - before;
    return true;
  }

  // Takes out the load's charge. The load guess is a bias, so its uncertainty grows with the time it holds.
  void predict(uint32_t stepMs, uint32_t loadMilliamps) {
    if (stepMs == 0) return;
    const double seconds = stepMs / 1000.0;
    const double used = loadScale_ * loadMilliamps * seconds / 3600.0 / capacityMah();
    share_ = std::max(0.0, share_ - used);
    if (learning_) learnUsed_ += used;
    const double rateError = kBatteryLoadErrorRatio * used / seconds;
    variance_ += rateError * rateError * kBatteryLoadErrorHoldS * seconds + kBatteryDriftVariancePerS * seconds;
    variance_ = std::min(variance_, kBatteryFreshSigma * kBatteryFreshSigma);
  }

  // Over a span of use, the voltage's corrections show how far the load guess is off: corrections up mean less
  // was used than counted. The scale moves part of the way, within its bounds.
  void learnLoadScale() {
    if (learnUsed_ < kBatteryLearnSpan) return;
    const double ratio = std::min(2.0, std::max(0.5, (learnUsed_ - learnCorrection_) / learnUsed_));
    const double scale = loadScale_ * (1.0 + kBatteryLearnGain * (ratio - 1.0));
    loadScale_ = std::min(kBatteryLoadScaleMaxPpm / 1e6, std::max(kBatteryLoadScaleMinPpm / 1e6, scale));
    loadScaleLearned_ = true;
    resetLearning();
  }

  void resetLearning() {
    learnUsed_ = 0.0;
    learnCorrection_ = 0.0;
  }

  // The awake load behind the time left. The first awake reading seeds it, so it never starts from a stale value.
  void trackAwake(uint32_t stepMs, uint32_t loadMilliamps) {
    if (loadMilliamps <= kBatteryLightLoadMa) return;
    const double sample = loadScale_ * loadMilliamps;
    if (awakeMa_ <= 0.0) {
      awakeMa_ = sample;
    } else {
      awakeMa_ += (sample - awakeMa_) * stepMs / kBatteryAwakeAverageMs;
    }
  }

  void beginCharge() {
    chargeTiming_ = true;
    chargeElapsedMs_ = 0;
    chargeStartPercent_ = estimatePercent();
  }

  // 100 points take 100 x the rate.
  void addCharge(uint32_t stepMs) {
    const double cap = kBatteryChargeCapPercent / 100.0;
    if (share_ >= cap) return;
    share_ = std::min(cap, share_ + static_cast<double>(stepMs) / (100.0 * actualMsPerPercent()));
  }

  // Called at done: the rate is the average time per point since the charge began, on the reference cell's scale.
  void learnCharge() {
    chargeTiming_ = false;
    const int points = 100 - chargeStartPercent_;
    if (points < kBatteryChargeLearnMinPoints) return;
    const uint64_t sample =
        scaleToCapacity(chargeElapsedMs_ / static_cast<uint64_t>(points), capacityUah_, kBatteryReferenceCapacityUah);
    // Faster than the fastest allowed rate is not a real charge (e.g. boot with charger done).
    if (sample < kBatteryChargeMsPerPercentMin) return;
    const uint64_t value = chargeLearned_ ? (static_cast<uint64_t>(chargeMsPerPercent_) * 3U + sample) / 4U : sample;
    chargeMsPerPercent_ = static_cast<uint32_t>(std::min<uint64_t>(
        std::max<uint64_t>(value, kBatteryChargeMsPerPercentMin), kBatteryChargeMsPerPercentMax));
    chargeLearned_ = true;
  }

  // Shown percent: charged shows 100. It falls one point per kBatteryDisplayStepMs, and never falls while
  // charging. On battery a rise waits for kBatteryShownRiseMargin points, then goes one point per
  // kBatteryDisplayRiseStepMs, so noise does not move it up and down.
  void updateShownPercent(uint32_t stepMs) {
    if (mode_ == Mode::Charged) {
      shownPercent_ = 100;
      displayWaitMs_ = 0;
      rising_ = false;
      return;
    }
    displayWaitMs_ = std::min(displayWaitMs_ + stepMs, kBatteryDisplayRiseStepMs);
    int target = estimatePercent();
    if (mode_ == Mode::Charging) {
      target = std::max(target, shownPercent_);
    } else if (target > shownPercent_) {
      if (target - shownPercent_ >= kBatteryShownRiseMargin) rising_ = true;
      if (!rising_) target = shownPercent_;
    } else {
      rising_ = false;
    }
    if (target == shownPercent_) return;
    const bool slowRise = target > shownPercent_ && mode_ == Mode::Discharging;
    if (displayWaitMs_ < (slowRise ? kBatteryDisplayRiseStepMs : kBatteryDisplayStepMs)) return;
    displayWaitMs_ = 0;
    shownPercent_ += target > shownPercent_ ? 1 : -1;
  }

  // Minutes to empty at the awake load, or to full at the charge rate; -1 for none.
  double rawMinutes() const {
    if (!started_) return -1.0;
    if (mode_ == Mode::Charging) return (1.0 - share_) * 100.0 * actualMsPerPercent() / 60000.0;
    if (mode_ == Mode::Charged || awakeMa_ <= 0.0) return -1.0;
    return share_ * capacityMah() * 60.0 / awakeMa_;
  }

  // The shown time counts down with the clock while it runs (awake, or charging), and moves toward the estimate at
  // a limited rate, so it never jumps. It starts at the estimate.
  void updateShownMinutes(uint32_t stepMs, uint32_t loadMilliamps) {
    const double raw = rawMinutes();
    if (raw < 0.0) {
      shownMinutes_ = -1.0;
      displayedMinutes_ = -1;
      return;
    }
    if (shownMinutes_ < 0.0) {
      shownMinutes_ = raw;
      displayedMinutes_ = -1;
    } else {
      const double elapsed = stepMs / 60000.0;
      const bool running = mode_ == Mode::Charging || loadMilliamps > kBatteryLightLoadMa;
      if (running) shownMinutes_ = std::max(0.0, shownMinutes_ - elapsed);
      const double limit = std::max(kBatteryTimeSlewMinMinutes, shownMinutes_ * kBatteryTimeSlewRatio) * elapsed;
      shownMinutes_ += std::min(limit, std::max(-limit, raw - shownMinutes_));
    }
    displayedMinutes_ = batteryRoundedMinutes(shownMinutes_, displayedMinutes_);
  }

  uint32_t capacityUah_ = kBatteryReferenceCapacityUah;
  double share_ = 0.0;
  double variance_ = kBatteryFreshSigma * kBatteryFreshSigma;
  double loadScale_ = 1.0;
  double smoothedMv_ = 0.0;
  double lastCompensatedMv_ = 0.0;
  double awakeMa_ = 0.0;
  double learnUsed_ = 0.0;
  double learnCorrection_ = 0.0;
  double shownMinutes_ = -1.0;
  int32_t displayedMinutes_ = -1;
  uint32_t chargeMsPerPercent_ = kBatteryPriorChargeMsPerPercent;
  uint32_t offMs_ = kBatteryOffUnknownMs;
  uint32_t measurementWaitMs_ = 0;
  uint32_t settleMs_ = 0;
  uint32_t displayWaitMs_ = 0;
  uint32_t lastMs_ = 0;
  uint64_t chargeElapsedMs_ = 0;
  int chargeStartPercent_ = 0;
  int shownPercent_ = -1;
  Mode mode_ = Mode::Discharging;
  bool chargeLearned_ = false;
  bool loadScaleLearned_ = false;
  // Counting use and corrections into the span: settled, on battery and within the learn shares.
  bool learning_ = false;
  bool chargeTiming_ = false;
  bool wasCharging_ = false;
  bool started_ = false;
  bool restored_ = false;
  bool rising_ = false;
};

// One reading per second. Timestamps are millis() values; differences use unsigned arithmetic,
// so the 32-bit wrap is safe.
class BatteryTracker {
 public:
  // Resumes the gauge from a stored state. Call it before the first update. offMs: time off, if known.
  void restore(const BatteryGaugeState& state, uint32_t offMs = kBatteryOffUnknownMs) { gauge_.restore(state, offMs); }

  // The cell the estimate counts in. The stored charge is a share, so it may come before or after restore.
  void setCapacity(uint32_t capacityUah) { gauge_.setCapacity(capacityUah); }

  // charging: LED lit, debounced here. loadMilliamps: 0 when unknown.
  // plugged: charger input, debounced by the caller.
  void update(uint32_t millivolts, bool charging, uint32_t nowMs, uint32_t loadMilliamps = 0,
              bool plugged = false) {
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
      chargerFlipSamples_ = 0;
    } else if (charging != charging_) {
      if (chargerFlipSamples_ < 255U) ++chargerFlipSamples_;
      if (chargerFlipSamples_ >= kBatteryChargerDebounceSamples) {
        charging_ = charging;
        chargerFlipSamples_ = 0;
      }
    } else {
      chargerFlipSamples_ = 0;
    }
    const bool full =
        plugged && !charging_ && millivolts >= (chargeDone_ ? kBatteryChargedReleaseMv : kBatteryChargedMv);
    if (firstSample) {
      chargeDone_ = full;
      doneFlipSamples_ = 0;
    } else if (full == chargeDone_) {
      doneFlipSamples_ = 0;
    } else if (++doneFlipSamples_ >= kBatteryChargerDebounceSamples) {
      chargeDone_ = full;
      doneFlipSamples_ = 0;
    }
    gauge_.update(millivolts, charging_, chargeDone_, loadMilliamps, nowMs);
    countTrip(lowSamples_, gauge_.estimatePercent() <= kBatteryLowPercent);
    countTrip(criticalSamples_, gauge_.estimatePercent() <= kBatteryCriticalPercent);
    countTrip(floorSamples_, millivolts <= kBatteryFloorMv);
  }

  bool valid() const { return valid_; }
  // Only after kBatteryTripSamples invalid readings in a row, never for one bad sample.
  bool unreadable() const { return invalidSamples_ >= kBatteryTripSamples; }
  // The charger's LED is lit: the cell is charging.
  bool charging() const { return valid_ && charging_; }
  // Plugged in, LED dark, cell full. The hold ends when the charger is removed.
  bool chargeDone() const { return valid_ && chargeDone_ && !charging_; }
  // On the charger: charging, or charged.
  bool docked() const { return valid_ && (charging_ || chargeDone_); }
  bool started() const { return gauge_.started(); }
  // Display percent; -1 with no reading.
  int percent() const { return valid_ ? gauge_.shownPercent() : -1; }
  // Estimate used by the warnings; -1 with no reading.
  int estimatePercent() const { return valid_ ? gauge_.estimatePercent() : -1; }
  // The percent the boot check refuses a cell at. The gauge decides, because it is the percent the device shows.
  // The voltage counts only at the floor, since a load sags it. -1 while no reading has been taken.
  int refusePercent() const {
    if (!valid_) return -1;
    if (lastMillivolts_ <= kBatteryFloorMv) return 0;
    return gauge_.estimatePercent();
  }
  // Shown minutes, rounded and smoothed; -1 while there is no reading.
  int32_t minutesRemaining() const { return valid_ ? gauge_.minutesRemaining() : -1; }
  // True while the minutes rest on the prior charge rate, or the charge is too uncertain.
  bool estimateApproximate() const { return gauge_.estimateApproximate(); }
  // For the bench log.
  const BatteryGauge& gauge() const { return gauge_; }
  BatteryGaugeState state() const { return gauge_.state(); }
  // Thresholds are debounced and skipped while the LED is lit; a plugged-in charger that is not
  // charging still gets them. The floor is critical whatever the gauge says.
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

// A time left in its shown form: "42 min", "2h 15m" from an hour, "14 h" from 3 hours, "1.8 d" from a day and
// "12 d" from 10 days. Round with batteryRoundedMinutes first. Returns false if the text does not fit.
inline bool formatBatteryDuration(char* out, std::size_t capacity, uint32_t minutes) {
  char digits[32];
  int written = 0;
  if (minutes < 60U) {
    written = snprintf(digits, sizeof(digits), "%lu min", static_cast<unsigned long>(minutes));
  } else if (minutes < 180U) {
    written = snprintf(digits, sizeof(digits), "%luh %02lum", static_cast<unsigned long>(minutes / 60U),
                       static_cast<unsigned long>(minutes % 60U));
  } else if (minutes < 1440U) {
    written = snprintf(digits, sizeof(digits), "%lu h", static_cast<unsigned long>((minutes + 30U) / 60U));
  } else if (minutes < 14400U) {
    const unsigned long tenths = (minutes * 10UL + 720UL) / 1440UL;
    written = snprintf(digits, sizeof(digits), "%lu.%lu d", tenths / 10UL, tenths % 10UL);
  } else {
    written = snprintf(digits, sizeof(digits), "%lu d", static_cast<unsigned long>((minutes + 720U) / 1440U));
  }
  if (capacity == 0 || written <= 0 || static_cast<std::size_t>(written) >= capacity) {
    if (capacity > 0) out[0] = '\0';
    return false;
  }
  std::memcpy(out, digits, static_cast<std::size_t>(written) + 1U);
  return true;
}

// Runtime line: "Full: 2h 15m" charging, "1.8 d left" on battery, "Fully charged" at 100%.
// Approximate adds "~". Returns false with no reading.
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
