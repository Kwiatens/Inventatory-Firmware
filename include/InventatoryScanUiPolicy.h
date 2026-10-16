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

// Quick labels are paged four to a screen, and keys 1 to 4 pick one.
constexpr std::size_t kQuickLabelsPerPage = 4U;

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

// The fault mark shows while the scanner is not running. Standby switches the scanner off on
// purpose, so that is not a fault.
constexpr bool scannerFaultShown(bool standby, bool scannerActive) {
  return !standby && !scannerActive;
}

// When several problems stand at once, the Ready and fault screens show one code at a time and
// move to the next code every kFaultRotateMs.
constexpr uint32_t kFaultRotateMs = 3000UL;

// The position of the problem on show at nowMs, when count problems take turns. A single problem
// never moves, so the result is zero for one problem or none.
inline std::size_t faultRotationIndex(uint32_t nowMs, std::size_t count, uint32_t periodMs) {
  return count < 2 || periodMs == 0 ? 0 : (nowMs / periodMs) % count;
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

// Text for an option from a table in enum order; a value outside the table gets the default's text.
template <std::size_t Count>
inline const char* optionText(const char* const (&table)[Count], uint8_t index, uint8_t fallback) {
  return table[index < Count ? index : fallback];
}

// Names for the settings screens. The short form fits a row of the device settings list.
inline const char* backlightModeLabel(BacklightMode mode) {
  static const char* const kText[] = {"Off", "On", "Auto 1 min", "Auto 3 min", "Auto 5 min", "Auto 10 min"};
  return optionText(kText, static_cast<uint8_t>(mode), kBacklightModeDefault);
}

inline const char* backlightModeShortLabel(BacklightMode mode) {
  static const char* const kText[] = {"Off", "On", "Auto 1m", "Auto 3m", "Auto 5m", "Auto 10m"};
  return optionText(kText, static_cast<uint8_t>(mode), kBacklightModeDefault);
}

// The backlight only switches on and off; it does not dim.
inline const char* backlightModeDetail(BacklightMode mode) {
  static const char* const kText[] = {"Dark, except warnings", "Stays lit",           "Off after 1 min idle",
                                      "Off after 3 min idle",   "Off after 5 min idle", "Off after 10 min idle"};
  return optionText(kText, static_cast<uint8_t>(mode), kBacklightModeDefault);
}

// Energy saving. The device sleeps (standby) after an idle time, then turns off (deep sleep) after a
// further time asleep. A countdown warns before each step. Both delays are stored as their index.
enum class SleepDelay : uint8_t {
  Min1 = 0,
  Min3 = 1,
  Min5 = 2,
  Min10 = 3,
  Min30 = 4,
};

constexpr uint8_t kSleepDelayCount = 5;
constexpr uint8_t kSleepDelayDefault = static_cast<uint8_t>(SleepDelay::Min5);

// Time asleep before the device turns off. `Never` keeps it asleep until '*' wakes it.
enum class PowerOffDelay : uint8_t {
  Min1 = 0,
  Min5 = 1,
  Min10 = 2,
  Min30 = 3,
  Hour1 = 4,
  Never = 5,
};

constexpr uint8_t kPowerOffDelayCount = 6;
constexpr uint8_t kPowerOffDelayDefault = static_cast<uint8_t>(PowerOffDelay::Min30);

// Each warning countdown runs this long before the step it warns about.
constexpr uint32_t kEnergyWarningLeadMs = 30000UL;

constexpr uint32_t sleepDelayMs(SleepDelay delay) {
  return delay == SleepDelay::Min1    ? 60000UL
         : delay == SleepDelay::Min3  ? 180000UL
         : delay == SleepDelay::Min5  ? 300000UL
         : delay == SleepDelay::Min10 ? 600000UL
                                      : 1800000UL;
}

// Zero for Never; check powerOffEnabled() first.
constexpr uint32_t powerOffDelayMs(PowerOffDelay delay) {
  return delay == PowerOffDelay::Min1    ? 60000UL
         : delay == PowerOffDelay::Min5  ? 300000UL
         : delay == PowerOffDelay::Min10 ? 600000UL
         : delay == PowerOffDelay::Min30 ? 1800000UL
         : delay == PowerOffDelay::Hour1 ? 3600000UL
                                         : 0UL;
}

constexpr bool powerOffEnabled(PowerOffDelay delay) { return delay != PowerOffDelay::Never; }

// A countdown must be longer than its warning, or the warning would start before the countdown does.
static_assert(kEnergyWarningLeadMs < sleepDelayMs(SleepDelay::Min1), "sleep warning longer than the sleep delay");
static_assert(kEnergyWarningLeadMs < powerOffDelayMs(PowerOffDelay::Min1),
              "power-off warning longer than the power-off delay");

// A stored value outside the enum falls back to the default instead of selecting an undefined delay.
inline SleepDelay sleepDelayFromStored(uint8_t value) {
  return value < kSleepDelayCount ? static_cast<SleepDelay>(value) : static_cast<SleepDelay>(kSleepDelayDefault);
}

inline PowerOffDelay powerOffDelayFromStored(uint8_t value) {
  return value < kPowerOffDelayCount ? static_cast<PowerOffDelay>(value)
                                     : static_cast<PowerOffDelay>(kPowerOffDelayDefault);
}

// Names for the settings screens. The short form fits a row of the energy saving list.
inline const char* sleepDelayLabel(SleepDelay delay) {
  static const char* const kText[] = {"1 min", "3 min", "5 min", "10 min", "30 min"};
  return optionText(kText, static_cast<uint8_t>(delay), kSleepDelayDefault);
}

inline const char* sleepDelayShortLabel(SleepDelay delay) {
  static const char* const kText[] = {"1m", "3m", "5m", "10m", "30m"};
  return optionText(kText, static_cast<uint8_t>(delay), kSleepDelayDefault);
}

inline const char* sleepDelayDetail(SleepDelay delay) {
  static const char* const kText[] = {"Sleeps after 1 min", "Sleeps after 3 min", "Sleeps after 5 min",
                                      "Sleeps after 10 min", "Sleeps after 30 min"};
  return optionText(kText, static_cast<uint8_t>(delay), kSleepDelayDefault);
}

inline const char* powerOffDelayLabel(PowerOffDelay delay) {
  static const char* const kText[] = {"1 min", "5 min", "10 min", "30 min", "1 hour", "Never"};
  return optionText(kText, static_cast<uint8_t>(delay), kPowerOffDelayDefault);
}

inline const char* powerOffDelayShortLabel(PowerOffDelay delay) {
  static const char* const kText[] = {"1m", "5m", "10m", "30m", "1h", "Never"};
  return optionText(kText, static_cast<uint8_t>(delay), kPowerOffDelayDefault);
}

inline const char* powerOffDelayDetail(PowerOffDelay delay) {
  static const char* const kText[] = {"Off 1 min after sleep",  "Off 5 min after sleep", "Off 10 min after sleep",
                                      "Off 30 min after sleep", "Off 1 hour after sleep", "Stays asleep"};
  return optionText(kText, static_cast<uint8_t>(delay), kPowerOffDelayDefault);
}

// Time left on a countdown that began at startMs and lasts spanMs, zero once it has run out. The
// unsigned difference keeps the count correct across the millis() wrap.
inline uint32_t energyRemainingMs(uint32_t nowMs, uint32_t startMs, uint32_t spanMs) {
  const uint32_t elapsed = nowMs - startMs;
  return elapsed >= spanMs ? 0U : spanMs - elapsed;
}

// The warning appears once the countdown has no more than the lead time left.
inline bool energyWarningDue(uint32_t remainingMs) { return remainingMs <= kEnergyWarningLeadMs; }

// Whole seconds for the countdown line, rounded up so it reads 30 down to 1.
inline uint32_t energyCountdownSeconds(uint32_t remainingMs) { return (remainingMs + 999U) / 1000U; }

// Fill of the countdown bar, 100 at the start of the warning and 0 when it runs out.
inline int energyCountdownPercent(uint32_t remainingMs) {
  return remainingMs >= kEnergyWarningLeadMs ? 100 : static_cast<int>(remainingMs * 100U / kEnergyWarningLeadMs);
}

// Where the energy saving sequence stands. `Asleep` is the standby that the sequence started; a manual
// standby never enters it, so it never powers off by itself.
enum class EnergyPhase : uint8_t {
  Awake,
  SleepWarning,
  Asleep,
  OffWarning,
};

// What the sequence does next. `CancelWarning` drops a warning that can no longer run.
enum class EnergyAction : uint8_t {
  None,
  StartSleepWarning,
  Sleep,
  StartOffWarning,
  PowerOff,
  CancelWarning,
};

struct EnergyInputs {
  // Energy saving is switched on.
  bool enabled;
  // Power off after sleep is not Never.
  bool powerOffSet;
  // The Ready screen with nothing in progress: no menu, quantity, error, warning or manual standby.
  bool idleReady;
  // An update is requested or running. Nothing sleeps or powers off during one.
  bool updating;
  // Time left before the device sleeps, counted from the last key press or scan.
  uint32_t sleepRemainingMs;
  // Time left before the device turns off, counted from the moment it slept.
  uint32_t offRemainingMs;
};

// The energy saving sequence: awake, warned, asleep, warned again, then powered off. A key press or a
// scan during a warning drops it back to awake, and the idle time starts again.
inline EnergyAction energyNextAction(EnergyPhase phase, const EnergyInputs& in) {
  switch (phase) {
    case EnergyPhase::Awake:
      if (!in.enabled || !in.idleReady || in.updating) return EnergyAction::None;
      if (in.sleepRemainingMs == 0U) return EnergyAction::Sleep;
      return energyWarningDue(in.sleepRemainingMs) ? EnergyAction::StartSleepWarning : EnergyAction::None;
    case EnergyPhase::SleepWarning:
      if (!in.enabled || !in.idleReady || in.updating) return EnergyAction::CancelWarning;
      return in.sleepRemainingMs == 0U ? EnergyAction::Sleep : EnergyAction::None;
    case EnergyPhase::Asleep:
      if (!in.enabled || !in.powerOffSet || in.updating) return EnergyAction::None;
      return energyWarningDue(in.offRemainingMs) ? EnergyAction::StartOffWarning : EnergyAction::None;
    case EnergyPhase::OffWarning:
      if (!in.enabled || in.updating) return EnergyAction::CancelWarning;
      return in.offRemainingMs == 0U ? EnergyAction::PowerOff : EnergyAction::None;
  }
  return EnergyAction::None;
}

// Soft power-off. Holding '*' alone for kPowerOffWarnMs shows a countdown; the power-off commits at
// kPowerOffCommitMs. Releasing '*', or pressing D or # while it is held, cancels before the commit.
constexpr unsigned long kPowerOffWarnMs = 1000UL;
constexpr unsigned long kPowerOffCommitMs = 4000UL;
// The countdown runs from the warning to the commit; its bar drains across that window.
constexpr unsigned long kPowerOffCountdownMs = kPowerOffCommitMs - kPowerOffWarnMs;

// The ›i sinks behind the glass in kPowerOffSinkSteps steps of kPowerOffSinkStepPx rows, one every
// kPowerOffSinkStepMs. It then rests for kPowerOffRestMs before the panel goes dark.
constexpr int kPowerOffSinkSteps = 6;
constexpr int kPowerOffSinkStepPx = 8;
constexpr unsigned long kPowerOffSinkStepMs = 60UL;
constexpr unsigned long kPowerOffRestMs = 700UL;

// Filled width of the countdown bar, `sinceWarningMs` after the warning: all of fullWidthPx at the
// warning, none from the commit on. It drains a pixel at a time, so the screen is redrawn only when
// this changes.
constexpr int powerOffBarFillPx(unsigned long sinceWarningMs, int fullWidthPx) {
  return sinceWarningMs >= kPowerOffCountdownMs
             ? 0
             : static_cast<int>((kPowerOffCountdownMs - sinceWarningMs) * static_cast<unsigned long>(fullWidthPx) /
                                kPowerOffCountdownMs);
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
