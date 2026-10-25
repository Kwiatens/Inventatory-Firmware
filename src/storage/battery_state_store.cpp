// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

// Inventatory Scanner R1 battery gauge state, kept across power-off.

#include "storage/battery_state_store.h"

namespace inventatory_scan {

namespace {
constexpr char kBatteryNamespace[] = "inv_battery";
constexpr char kChargeShareKey[] = "soc_ppm";
constexpr char kChargeRateKey[] = "chg_ms_pct";
constexpr char kLoadScaleKey[] = "load_ppm";
constexpr char kLearnUsedKey[] = "learn_used";
constexpr char kLearnCorrectionKey[] = "learn_corr";
// The first layout kept the charge in uAh of the reference cell, and an average load that is no longer used.
constexpr char kLegacyRemainingKey[] = "rem_uah";
constexpr char kLegacyAverageKey[] = "avg_cma";
constexpr int32_t kLearnCorrectionMaxPpm = static_cast<int32_t>(kBatteryChargePpmFull);
}  // namespace

bool BatteryStateStore::begin() { return prefs_.begin(kBatteryNamespace, false); }

bool BatteryStateStore::load(BatteryGaugeState& state) const {
  BatteryGaugeState stored;
  if (prefs_.isKey(kChargeShareKey)) {
    stored.chargePpm = prefs_.getUInt(kChargeShareKey, 0);
    stored.loadScalePpm = prefs_.getUInt(kLoadScaleKey, 0);
    legacy_ = false;
  } else if (prefs_.isKey(kLegacyRemainingKey)) {
    const uint32_t remainingUah = prefs_.getUInt(kLegacyRemainingKey, 0);
    if (remainingUah > kBatteryReferenceCapacityUah) return false;
    stored.chargePpm = static_cast<uint32_t>(
        scaleToCapacity(remainingUah, kBatteryReferenceCapacityUah, kBatteryChargePpmFull));
    legacy_ = true;
  } else {
    return false;
  }
  if (!prefs_.isKey(kChargeRateKey)) return false;
  stored.chargeMsPerPercent = prefs_.getUInt(kChargeRateKey, 0);
  if (stored.chargePpm > kBatteryChargePpmFull) return false;
  // Zero means not learned yet; otherwise the value must be in range.
  if (stored.chargeMsPerPercent != 0 && (stored.chargeMsPerPercent < kBatteryChargeMsPerPercentMin ||
                                         stored.chargeMsPerPercent > kBatteryChargeMsPerPercentMax)) {
    return false;
  }
  if (stored.loadScalePpm != 0 &&
      (stored.loadScalePpm < kBatteryLoadScaleMinPpm || stored.loadScalePpm > kBatteryLoadScaleMaxPpm)) {
    return false;
  }
  // A span out of range is dropped, not the state: the span only feeds the next scale step.
  stored.learnUsedPpm = prefs_.getUInt(kLearnUsedKey, 0);
  stored.learnCorrectionPpm = prefs_.getInt(kLearnCorrectionKey, 0);
  if (stored.learnUsedPpm > kBatteryLearnUsedMaxPpm || stored.learnCorrectionPpm > kLearnCorrectionMaxPpm ||
      stored.learnCorrectionPpm < -kLearnCorrectionMaxPpm) {
    stored.learnUsedPpm = 0;
    stored.learnCorrectionPpm = 0;
  }
  state = stored;
  last_ = stored;
  haveLast_ = !legacy_;
  return true;
}

bool BatteryStateStore::save(const BatteryGaugeState& state) {
  if (!haveLast_ || state.chargePpm != last_.chargePpm) {
    if (prefs_.putUInt(kChargeShareKey, state.chargePpm) != sizeof(uint32_t)) return false;
    last_.chargePpm = state.chargePpm;
  }
  if (!haveLast_ || state.chargeMsPerPercent != last_.chargeMsPerPercent) {
    if (prefs_.putUInt(kChargeRateKey, state.chargeMsPerPercent) != sizeof(uint32_t)) return false;
    last_.chargeMsPerPercent = state.chargeMsPerPercent;
  }
  if (!haveLast_ || state.loadScalePpm != last_.loadScalePpm) {
    if (prefs_.putUInt(kLoadScaleKey, state.loadScalePpm) != sizeof(uint32_t)) return false;
    last_.loadScalePpm = state.loadScalePpm;
  }
  if (!haveLast_ || state.learnUsedPpm != last_.learnUsedPpm) {
    if (prefs_.putUInt(kLearnUsedKey, state.learnUsedPpm) != sizeof(uint32_t)) return false;
    last_.learnUsedPpm = state.learnUsedPpm;
  }
  if (!haveLast_ || state.learnCorrectionPpm != last_.learnCorrectionPpm) {
    if (prefs_.putInt(kLearnCorrectionKey, state.learnCorrectionPpm) != sizeof(int32_t)) return false;
    last_.learnCorrectionPpm = state.learnCorrectionPpm;
  }
  haveLast_ = true;
  // The new keys are written, so the first layout's keys can go. A failed removal leaves them unread.
  if (legacy_) {
    prefs_.remove(kLegacyRemainingKey);
    prefs_.remove(kLegacyAverageKey);
    legacy_ = false;
  }
  return true;
}

}  // namespace inventatory_scan
