// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// Battery state for the single LiPo cell: voltage to charge, a smoothed tracker
// with a learned runtime rate, and the low-battery thresholds. No Arduino calls,
// so the host tests run the same code the device runs.

#include <cstddef>
#include <cstdint>
#include <cstdio>
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

// A reading below this is an unconnected or unreadable divider, not a cell. Such
// readings never warn or power off, so a board without the divider still boots.
constexpr uint32_t kBatteryMinimumValidMv = 2500;
constexpr uint8_t kBatteryLowPercent = 10;
constexpr uint8_t kBatteryCriticalPercent = 3;
// Consecutive one-second samples below a threshold before it counts. Radio
// bursts sag the cell for a moment; one low reading must not switch the device off.
constexpr uint8_t kBatteryTripSamples = 4;
// The charger line must read the new state this many samples in a row before the state
// changes. A dock contact that bounces must not restart the rate or re-show the card.
constexpr uint8_t kBatteryChargerDebounceSamples = 2;
// Observed time needed before a runtime rate is trusted and shown.
constexpr uint32_t kBatteryEstimateMinSpanMs = 300000UL;
constexpr int32_t kBatteryEstimateMaxMinutes = 9999;

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

// Tracks the battery from one reading per second. The voltage is smoothed, the
// percent follows the smoothed voltage, and the time to empty or full comes from
// the rate seen so far: how long each whole percent took to change while
// discharging or charging. Timestamps are millis() values; differences use
// unsigned arithmetic, so they survive the 32-bit wrap.
class BatteryTracker {
 public:
  void update(uint32_t millivolts, bool charging, uint32_t nowMs) {
    if (millivolts < kBatteryMinimumValidMv) {
      valid_ = false;
      lowSamples_ = 0;
      criticalSamples_ = 0;
      return;
    }
    const bool firstSample = !valid_;
    if (firstSample) {
      smoothedMv_ = millivolts;
    } else {
      // Moves a quarter of the way to each new reading.
      const int32_t difference = static_cast<int32_t>(millivolts) - static_cast<int32_t>(smoothedMv_);
      smoothedMv_ = static_cast<uint32_t>(static_cast<int32_t>(smoothedMv_) + difference / 4);
    }
    valid_ = true;
    percent_ = batteryPercentFromMillivolts(smoothedMv_);
    if (firstSample) {
      charging_ = charging;
      chargerFlipSamples_ = 0;
      restartAnchor(nowMs);
    } else if (charging != charging_) {
      if (chargerFlipSamples_ < 255U) ++chargerFlipSamples_;
      if (chargerFlipSamples_ >= kBatteryChargerDebounceSamples) {
        charging_ = charging;
        chargerFlipSamples_ = 0;
        restartAnchor(nowMs);
      }
    } else {
      chargerFlipSamples_ = 0;
    }
    learnRate(nowMs);
    countTrip(lowSamples_, percent_ <= kBatteryLowPercent);
    countTrip(criticalSamples_, percent_ <= kBatteryCriticalPercent);
  }

  bool valid() const { return valid_; }
  bool charging() const { return valid_ && charging_; }
  // -1 while no reading has been taken.
  int percent() const { return valid_ ? static_cast<int>(percent_) : -1; }
  // -1 until a rate has been observed, so a time is never shown from guesswork.
  int32_t minutesRemaining() const {
    if (!valid_) return -1;
    const std::size_t slot = charging_ ? 1U : 0U;
    if (!rateKnown_[slot]) return -1;
    const uint32_t remainingPercent = charging_ ? 100U - percent_ : percent_;
    const uint64_t minutes = msPerPercent_[slot] * remainingPercent / 60000ULL;
    return minutes > static_cast<uint64_t>(kBatteryEstimateMaxMinutes)
               ? kBatteryEstimateMaxMinutes
               : static_cast<int32_t>(minutes);
  }
  // Discharge thresholds are debounced and never apply while charging.
  BatteryLevel level() const {
    if (!valid_ || charging_) return BatteryLevel::Ok;
    if (criticalSamples_ >= kBatteryTripSamples) return BatteryLevel::Critical;
    if (lowSamples_ >= kBatteryTripSamples) return BatteryLevel::Low;
    return BatteryLevel::Ok;
  }

 private:
  void restartAnchor(uint32_t nowMs) {
    anchorMs_ = nowMs;
    anchorPercent_ = percent_;
  }

  // Waits for at least one whole percent after the anchor point and at least the
  // minimum span, then records the average time per percent in the trend's direction.
  void learnRate(uint32_t nowMs) {
    const uint32_t elapsed = nowMs - anchorMs_;
    if (elapsed < kBatteryEstimateMinSpanMs) return;
    const int delta = charging_ ? static_cast<int>(percent_) - static_cast<int>(anchorPercent_)
                                : static_cast<int>(anchorPercent_) - static_cast<int>(percent_);
    if (delta < 0) {
      // Moved against the trend (for example, the charger was removed mid-charge): start over.
      restartAnchor(nowMs);
      return;
    }
    if (delta == 0) return;
    const uint64_t sample = elapsed / static_cast<uint32_t>(delta);
    const std::size_t slot = charging_ ? 1U : 0U;
    msPerPercent_[slot] = rateKnown_[slot] ? (msPerPercent_[slot] * 3U + sample) / 4U : sample;
    rateKnown_[slot] = true;
    restartAnchor(nowMs);
  }

  void countTrip(uint8_t& samples, bool belowThreshold) {
    if (!belowThreshold || charging_) {
      samples = 0;
    } else if (samples < 255U) {
      ++samples;
    }
  }

  uint32_t smoothedMv_ = 0;
  uint32_t anchorMs_ = 0;
  uint8_t percent_ = 0;
  uint8_t anchorPercent_ = 0;
  bool valid_ = false;
  bool charging_ = false;
  // Index 0 is discharging, 1 is charging.
  uint64_t msPerPercent_[2] = {0, 0};
  bool rateKnown_[2] = {false, false};
  uint8_t lowSamples_ = 0;
  uint8_t criticalSamples_ = 0;
  uint8_t chargerFlipSamples_ = 0;
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
// charging, "6h 20m left" on battery, "Fully charged" at 100%. Returns false, and
// writes nothing useful, while no rate is known yet.
inline bool formatBatteryEstimate(char* out, std::size_t capacity, int32_t minutes, bool charging,
                                  int percent) {
  if (capacity == 0) return false;
  out[0] = '\0';
  if (charging && percent >= 100) {
    snprintf(out, capacity, "Fully charged");
    return true;
  }
  if (minutes < 0) return false;
  char duration[16];
  if (!formatBatteryDuration(duration, sizeof(duration), static_cast<uint32_t>(minutes))) return false;
  const int written = snprintf(out, capacity, charging ? "Full: %s" : "%s left", duration);
  return written > 0 && static_cast<std::size_t>(written) < capacity;
}

}  // namespace inventatory_scan
