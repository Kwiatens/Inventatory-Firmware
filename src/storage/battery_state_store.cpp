// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

// Inventatory Scan R1 battery gauge state, kept across power-off.

#include "storage/battery_state_store.h"

namespace inventatory_scan {

namespace {
constexpr char kBatteryNamespace[] = "inv_battery";
constexpr char kRemainingKey[] = "rem_uah";
constexpr char kChargeKey[] = "chg_ms_pct";
constexpr char kAverageKey[] = "avg_cma";
// Largest stored average load, in 1/100 mA (1000 mA).
constexpr uint32_t kMaxAverageCentiMa = 100000UL;
}  // namespace

bool BatteryStateStore::begin() { return prefs_.begin(kBatteryNamespace, false); }

bool BatteryStateStore::load(BatteryGaugeState& state) const {
  if (!prefs_.isKey(kRemainingKey) || !prefs_.isKey(kChargeKey) || !prefs_.isKey(kAverageKey)) return false;
  BatteryGaugeState stored;
  stored.remainingUah = prefs_.getUInt(kRemainingKey, 0);
  stored.chargeMsPerPercent = prefs_.getUInt(kChargeKey, 0);
  stored.averageLoadCentiMa = prefs_.getUInt(kAverageKey, 0);
  if (stored.remainingUah > kBatteryReferenceCapacityUah) return false;
  // Zero means no charge timed yet; otherwise the value must be in range on the reference cell's scale.
  if (stored.chargeMsPerPercent != 0 && (stored.chargeMsPerPercent < kBatteryChargeMsPerPercentMin ||
                                         stored.chargeMsPerPercent > kBatteryChargeMsPerPercentMax)) {
    return false;
  }
  if (stored.averageLoadCentiMa > kMaxAverageCentiMa) return false;
  state = stored;
  last_ = stored;
  haveLast_ = true;
  return true;
}

bool BatteryStateStore::save(const BatteryGaugeState& state) {
  if (!haveLast_ || state.remainingUah != last_.remainingUah) {
    if (prefs_.putUInt(kRemainingKey, state.remainingUah) != sizeof(uint32_t)) return false;
    last_.remainingUah = state.remainingUah;
  }
  if (!haveLast_ || state.chargeMsPerPercent != last_.chargeMsPerPercent) {
    if (prefs_.putUInt(kChargeKey, state.chargeMsPerPercent) != sizeof(uint32_t)) return false;
    last_.chargeMsPerPercent = state.chargeMsPerPercent;
  }
  if (!haveLast_ || state.averageLoadCentiMa != last_.averageLoadCentiMa) {
    if (prefs_.putUInt(kAverageKey, state.averageLoadCentiMa) != sizeof(uint32_t)) return false;
    last_.averageLoadCentiMa = state.averageLoadCentiMa;
  }
  haveLast_ = true;
  return true;
}

}  // namespace inventatory_scan
