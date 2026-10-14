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
  switch (delay) {
    case SleepDelay::Min1: return "1 min";
    case SleepDelay::Min3: return "3 min";
    case SleepDelay::Min5: return "5 min";
    case SleepDelay::Min10: return "10 min";
    case SleepDelay::Min30: return "30 min";
  }
  return "5 min";
}

inline const char* sleepDelayShortLabel(SleepDelay delay) {
  switch (delay) {
    case SleepDelay::Min1: return "1m";
    case SleepDelay::Min3: return "3m";
    case SleepDelay::Min5: return "5m";
    case SleepDelay::Min10: return "10m";
    case SleepDelay::Min30: return "30m";
  }
  return "5m";
}

inline const char* sleepDelayDetail(SleepDelay delay) {
  switch (delay) {
    case SleepDelay::Min1: return "Sleeps after 1 min";
    case SleepDelay::Min3: return "Sleeps after 3 min";
    case SleepDelay::Min5: return "Sleeps after 5 min";
    case SleepDelay::Min10: return "Sleeps after 10 min";
    case SleepDelay::Min30: return "Sleeps after 30 min";
  }
  return "Sleeps after 5 min";
}

inline const char* powerOffDelayLabel(PowerOffDelay delay) {
  switch (delay) {
    case PowerOffDelay::Min1: return "1 min";
    case PowerOffDelay::Min5: return "5 min";
    case PowerOffDelay::Min10: return "10 min";
    case PowerOffDelay::Min30: return "30 min";
    case PowerOffDelay::Hour1: return "1 hour";
    case PowerOffDelay::Never: return "Never";
  }
  return "Never";
}

inline const char* powerOffDelayShortLabel(PowerOffDelay delay) {
  switch (delay) {
    case PowerOffDelay::Min1: return "1m";
    case PowerOffDelay::Min5: return "5m";
    case PowerOffDelay::Min10: return "10m";
    case PowerOffDelay::Min30: return "30m";
    case PowerOffDelay::Hour1: return "1h";
    case PowerOffDelay::Never: return "Never";
  }
  return "Never";
}

inline const char* powerOffDelayDetail(PowerOffDelay delay) {
  switch (delay) {
    case PowerOffDelay::Min1: return "Off 1 min after sleep";
    case PowerOffDelay::Min5: return "Off 5 min after sleep";
    case PowerOffDelay::Min10: return "Off 10 min after sleep";
    case PowerOffDelay::Min30: return "Off 30 min after sleep";
    case PowerOffDelay::Hour1: return "Off 1 hour after sleep";
    case PowerOffDelay::Never: return "Stays asleep";
  }
  return "Stays asleep";
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
