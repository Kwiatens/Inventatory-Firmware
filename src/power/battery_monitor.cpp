// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

#include "power/battery_monitor.h"

#include "config/config.h"
#include "config/hardware.h"

namespace inventatory_scan {

namespace {

constexpr uint8_t kSamples = 8;
constexpr uint8_t kBatterySamples = 32;

// ADC1 (GPIO1-10) keeps reading while Wi-Fi is active; ADC2 does not.
static_assert(BATTERY_ADC_PIN >= 1 && BATTERY_ADC_PIN <= 10, "BATTERY_ADC_PIN must be an ADC1 pin (GPIO1-10)");
static_assert(BATTERY_DIVIDER_BOTTOM_OHMS > 0UL, "BATTERY_DIVIDER_BOTTOM_OHMS must be non-zero");
static_assert(hardware::kConnectorSensePin >= 1 && hardware::kConnectorSensePin <= 10,
              "kConnectorSensePin must be an ADC1 pin (GPIO1-10)");

}  // namespace

void BatteryMonitor::begin() {
  // 12 dB attenuation covers the divider output of a LiPo (about 2.1 V at 4.2 V).
  analogSetPinAttenuation(BATTERY_ADC_PIN, ADC_11db);
  // Charger signals go through the ADC: a digital input reads their ~2.2 V high level as low.
  // No pull resistors: a pull-down pulled the LED signal to 0 V.
  analogSetPinAttenuation(hardware::kConnectorSensePin, ADC_11db);
  analogSetPinAttenuation(hardware::kChargingLedPin, ADC_11db);
}

uint32_t BatteryMonitor::readMillivolts() const {
  // The middle half of kBatterySamples conversions, so a spike from a radio burst or the backlight is dropped.
  uint16_t samples[kBatterySamples];
  for (uint8_t index = 0; index < kBatterySamples; ++index) {
    const uint16_t value = static_cast<uint16_t>(analogReadMilliVolts(BATTERY_ADC_PIN));
    uint8_t slot = index;
    while (slot > 0 && samples[slot - 1U] > value) {
      samples[slot] = samples[slot - 1U];
      --slot;
    }
    samples[slot] = value;
  }
  uint32_t pinSum = 0;
  for (uint8_t index = kBatterySamples / 4U; index < kBatterySamples * 3U / 4U; ++index) pinSum += samples[index];
  const uint64_t pinMillivolts = (pinSum + kBatterySamples / 4U) / (kBatterySamples / 2U);
  // The pin sees battery * bottom / (top + bottom). The scale corrects this board's measured error.
  const uint64_t cellMillivolts =
      pinMillivolts * (BATTERY_DIVIDER_TOP_OHMS + BATTERY_DIVIDER_BOTTOM_OHMS) / BATTERY_DIVIDER_BOTTOM_OHMS;
  return static_cast<uint32_t>(cellMillivolts * hardware::kBatteryReadingScalePpm / 1000000ULL);
}

uint32_t BatteryMonitor::chargingLedMillivolts() const {
  uint32_t pinSum = 0;
  for (uint8_t sample = 0; sample < kSamples; ++sample) {
    pinSum += analogReadMilliVolts(hardware::kChargingLedPin);
  }
  return pinSum / kSamples;
}


uint32_t BatteryMonitor::connectorMillivolts() const {
  uint32_t pinSum = 0;
  for (uint8_t sample = 0; sample < kSamples; ++sample) {
    pinSum += analogReadMilliVolts(hardware::kConnectorSensePin);
  }
  return pinSum / kSamples;
}

}  // namespace inventatory_scan
