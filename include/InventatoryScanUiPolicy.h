// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <cstddef>
#include <cstdint>

namespace inventatory_scan {

// Timestamps are millis() values. They are 32-bit on the ESP32 and wrap every
// 49.7 days, so the helpers below use fixed-width types. Their results then
// match on the device and on a 64-bit host, which the tests rely on.
constexpr uint32_t kUiActivityPeriodMs = 600U;
constexpr uint32_t kUiSuccessDwellMs = 3500U;
constexpr uint32_t kUiWarningDwellMs = 3000U;

constexpr bool displayHeaderCanShowStatus(int titleWidth, int statusWidth,
                                          int availableWidth, int gap) {
  return titleWidth >= 0 && statusWidth >= 0 && availableWidth >= 0 && gap >= 0 &&
         titleWidth + statusWidth + gap <= availableWidth;
}

constexpr bool displayQueueDepth(std::size_t queueDepth) { return queueDepth > 0; }

template <typename PrefixWidth>
std::size_t displayClippedPrefixLength(std::size_t textLength, int maxWidth,
                                       int ellipsisWidth, PrefixWidth prefixWidth) {
  if (maxWidth <= 0 || textLength == 0) return 0;
  if (prefixWidth(textLength) <= maxWidth) return textLength;
  for (std::size_t length = textLength; length > 0; --length) {
    if (prefixWidth(length - 1) + ellipsisWidth <= maxWidth) return length - 1;
  }
  return 0;
}

constexpr bool scanMayInterruptUi(bool setupMode, bool standby, bool otaActive,
                                  bool destructiveConfirmation, bool printing) {
  return !setupMode && !standby && !otaActive && !destructiveConfirmation && !printing;
}

constexpr bool uiActivityAlternate(uint32_t now) {
  return ((now / kUiActivityPeriodMs) % 2U) != 0U;
}

constexpr bool uiActivityEnabled(bool standby, bool activityOwnedByState) {
  return !standby && activityOwnedByState;
}

// Triangle wave over one sweep: 0, 1, ..., stepsPerLeg, ..., 1, then repeats.
constexpr uint32_t uiRailSweepLegStep(uint32_t now, uint32_t stepPeriodMs, int stepsPerLeg) {
  return ((now / stepPeriodMs) % (static_cast<uint32_t>(stepsPerLeg) * 2U)) <=
                 static_cast<uint32_t>(stepsPerLeg)
             ? (now / stepPeriodMs) % (static_cast<uint32_t>(stepsPerLeg) * 2U)
             : static_cast<uint32_t>(stepsPerLeg) * 2U -
                   ((now / stepPeriodMs) % (static_cast<uint32_t>(stepsPerLeg) * 2U));
}

constexpr int uiRailSweepOffset(uint32_t now, uint32_t stepPeriodMs, int travel, int stepsPerLeg) {
  return stepPeriodMs == 0 || travel <= 0 || stepsPerLeg <= 0
             ? 0
             : static_cast<int>((uiRailSweepLegStep(now, stepPeriodMs, stepsPerLeg) *
                                     static_cast<uint32_t>(travel) +
                                 static_cast<uint32_t>(stepsPerLeg / 2)) /
                                static_cast<uint32_t>(stepsPerLeg));
}

// A result dwell may only return to Ready while a result screen is still shown.
constexpr bool uiResultDwellMayExpire(bool resultScreenVisible) { return resultScreenVisible; }

// True once now has reached deadline. A deadline of 0 means "none". The signed
// difference keeps the comparison correct across millis() wrap-around.
constexpr bool uiDeadlineReached(uint32_t now, uint32_t deadline) {
  return deadline != 0 && static_cast<int32_t>(now - deadline) >= 0;
}

// The dot of the ›i mark is split into four slots, one bit each in reading
// order: bit 0 top-left, bit 1 top-right, bit 2 bottom-left, bit 3 bottom-right.
// The slots animate in whole steps, like pixels switching, as in the desktop app.
constexpr uint8_t kBrandDotFull = 0x0F;
// A fault: only the top-left and bottom-right slots, a static broken dot that
// still reads when the main loop has stopped.
constexpr uint8_t kBrandDotFault = 0x09;
constexpr unsigned long kBrandDotStepMs = 200UL;
constexpr unsigned long kBrandDotBlinkMs = 400UL;

// The helpers below are single-return constexpr functions: the ESP32 toolchain
// compiles them under C++11 rules.
constexpr uint8_t brandDotClockwise(unsigned long step) {
  return step == 0UL ? 0x01 : step == 1UL ? 0x02 : step == 2UL ? 0x08 : 0x04;
}

// Work of unknown length: one lit slot travels clockwise.
constexpr uint8_t brandDotBusy(unsigned long now) {
  return brandDotClockwise((now / kBrandDotStepMs) % 4UL);
}

constexpr uint8_t brandDotSlotsUpTo(int count) {
  return static_cast<uint8_t>((1U << (count < 0 ? 0 : count > 4 ? 4 : count)) - 1U);
}

constexpr int brandDotFilledSlots(int percent) {
  return (percent < 0 ? 0 : percent > 100 ? 100 : percent) * 4 / 100;
}

// Work of known size: slots fill in reading order, one per quarter, and the
// slot being filled blinks.
constexpr uint8_t brandDotProgress(int percent, unsigned long now) {
  return brandDotSlotsUpTo(brandDotFilledSlots(percent) +
                           ((now / kBrandDotBlinkMs) % 2UL == 0UL ? 1 : 0));
}

// Top bar: the right-hand slot cycles through the status, the battery level and,
// when a runtime rate is known, the time left. The status alone is shown when
// there is no battery reading.
enum class HeaderPage : uint8_t {
  Status = 0,
  Battery = 1,
  Estimate = 2,
};

constexpr uint32_t kHeaderStatusPageMs = 4000UL;
constexpr uint32_t kHeaderBatteryPageMs = 1500UL;
constexpr uint32_t kHeaderEstimatePageMs = 2000UL;

// The page shown at `nowMs`. The estimate page is skipped while hasEstimate is false.
inline HeaderPage headerPageAt(uint32_t nowMs, bool hasEstimate) {
  const uint32_t cycle =
      kHeaderStatusPageMs + kHeaderBatteryPageMs + (hasEstimate ? kHeaderEstimatePageMs : 0UL);
  uint32_t phase = nowMs % cycle;
  if (phase < kHeaderStatusPageMs) return HeaderPage::Status;
  phase -= kHeaderStatusPageMs;
  if (phase < kHeaderBatteryPageMs) return HeaderPage::Battery;
  return HeaderPage::Estimate;
}

// The battery card fills in steps, one per 100 ms, so each frame is a visible change.
constexpr uint32_t kBatteryCardFillStepMs = 100UL;
constexpr uint8_t kBatteryCardFillSteps = 7;

// Fill step for the card, `elapsedMs` after it appeared.
inline uint8_t batteryCardFillStep(uint32_t elapsedMs) {
  const uint32_t step = elapsedMs / kBatteryCardFillStepMs;
  return step >= kBatteryCardFillSteps ? kBatteryCardFillSteps : static_cast<uint8_t>(step);
}

// Backlight setting, stored as its numeric value. `On` is the default: it keeps
// the screen lit as before. Auto modes switch the backlight off after the idle time.
enum class BacklightMode : uint8_t {
  Off = 0,
  On = 1,
  Auto1Min = 2,
  Auto3Min = 3,
  Auto5Min = 4,
  Auto10Min = 5,
};

constexpr uint8_t kBacklightModeCount = 6;
constexpr uint8_t kBacklightModeDefault = static_cast<uint8_t>(BacklightMode::On);

constexpr uint32_t backlightIdleTimeoutMs(BacklightMode mode) {
  return mode == BacklightMode::Auto1Min    ? 60000UL
         : mode == BacklightMode::Auto3Min  ? 180000UL
         : mode == BacklightMode::Auto5Min  ? 300000UL
         : mode == BacklightMode::Auto10Min ? 600000UL
                                            : 0UL;
}

// A stored value outside the enum falls back to the default instead of
// selecting an undefined mode.
inline BacklightMode backlightModeFromStored(uint8_t value) {
  return value < kBacklightModeCount ? static_cast<BacklightMode>(value)
                                     : static_cast<BacklightMode>(kBacklightModeDefault);
}

// `forced` covers screens that must stay readable whatever the setting: the
// low-battery warning and pairing. Timestamps use unsigned differences, so the
// idle time survives the millis() wrap.
inline bool backlightLit(BacklightMode mode, bool forced, uint32_t nowMs, uint32_t lastActivityMs) {
  if (forced || mode == BacklightMode::On) return true;
  const uint32_t timeout = backlightIdleTimeoutMs(mode);
  return timeout != 0U && static_cast<uint32_t>(nowMs - lastActivityMs) < timeout;
}

// Names for the settings screens. The short form fits a row of the device settings list.
inline const char* backlightModeLabel(BacklightMode mode) {
  switch (mode) {
    case BacklightMode::Off: return "Off";
    case BacklightMode::On: return "On";
    case BacklightMode::Auto1Min: return "Auto 1 min";
    case BacklightMode::Auto3Min: return "Auto 3 min";
    case BacklightMode::Auto5Min: return "Auto 5 min";
    case BacklightMode::Auto10Min: return "Auto 10 min";
  }
  return "On";
}

inline const char* backlightModeShortLabel(BacklightMode mode) {
  switch (mode) {
    case BacklightMode::Off: return "Off";
    case BacklightMode::On: return "On";
    case BacklightMode::Auto1Min: return "Auto 1m";
    case BacklightMode::Auto3Min: return "Auto 3m";
    case BacklightMode::Auto5Min: return "Auto 5m";
    case BacklightMode::Auto10Min: return "Auto 10m";
  }
  return "On";
}

inline const char* backlightModeDetail(BacklightMode mode) {
  switch (mode) {
    case BacklightMode::Off: return "Dark, except warnings";
    case BacklightMode::On: return "Stays lit";
    case BacklightMode::Auto1Min: return "Dims after 1 min idle";
    case BacklightMode::Auto3Min: return "Dims after 3 min idle";
    case BacklightMode::Auto5Min: return "Dims after 5 min idle";
    case BacklightMode::Auto10Min: return "Dims after 10 min idle";
  }
  return "Stays lit";
}

// Soft power-off. Holding '*' alone for kPowerOffWarnMs shows a countdown; the power-off commits at
// kPowerOffCommitMs. Releasing '*', or pressing D or # while it is held, cancels before the commit.
constexpr unsigned long kPowerOffWarnMs = 1000UL;
constexpr unsigned long kPowerOffCommitMs = 4000UL;
constexpr int kPowerOffCountdownSeconds = 3;
constexpr unsigned long kPowerOffCountdownStepMs = 1000UL;

// The ›i sinks behind the glass in kPowerOffSinkSteps steps of kPowerOffSinkStepPx rows, one every
// kPowerOffSinkStepMs. It then rests for kPowerOffRestMs before the panel goes dark.
constexpr int kPowerOffSinkSteps = 6;
constexpr int kPowerOffSinkStepPx = 8;
constexpr unsigned long kPowerOffSinkStepMs = 60UL;
constexpr unsigned long kPowerOffRestMs = 700UL;

// Seconds left on the countdown, measured from the warning: 3, then 2, then 1.
constexpr int powerOffCountdownSeconds(unsigned long sinceWarningMs) {
  return sinceWarningMs < kPowerOffCountdownStepMs
             ? kPowerOffCountdownSeconds
             : sinceWarningMs < 2UL * kPowerOffCountdownStepMs ? kPowerOffCountdownSeconds - 1 : 1;
}

// The countdown bar: full at the first count, a third at the last.
constexpr int powerOffCountdownPercent(int seconds) {
  return seconds <= 0 ? 0
         : seconds >= kPowerOffCountdownSeconds ? 100
                                                : seconds * 100 / kPowerOffCountdownSeconds;
}

// How far the mark has dropped at a sink step. Step 0 is the commit, where the mark still stands.
constexpr int powerOffSinkOffset(int step) {
  return step <= 0 ? 0
         : step >= kPowerOffSinkSteps - 1 ? (kPowerOffSinkSteps - 1) * kPowerOffSinkStepPx
                                          : step * kPowerOffSinkStepPx;
}

// Wake bloom: the boot mark draws in kBootBloomStages stages, one every kBootBloomStepMs. The whole
// bloom fits inside the boot splash hold, so waking takes no longer than it did.
constexpr unsigned long kBootBloomStepMs = 150UL;
constexpr int kBootBloomStages = 6;
constexpr int bootBloomStage(unsigned long elapsedMs) {
  return elapsedMs >= kBootBloomStepMs * kBootBloomStages
             ? kBootBloomStages
             : static_cast<int>(elapsedMs / kBootBloomStepMs);
}

}  // namespace inventatory_scan
