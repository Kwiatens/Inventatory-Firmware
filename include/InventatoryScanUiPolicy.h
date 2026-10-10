// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <cstddef>
#include <cstdint>

namespace inventatory_scan {

constexpr unsigned long kUiActivityPeriodMs = 600UL;
constexpr unsigned long kUiSuccessDwellMs = 3500UL;
constexpr unsigned long kUiWarningDwellMs = 3000UL;
constexpr unsigned long kUiQueuedDwellMs = 3500UL;

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

constexpr bool uiActivityAlternate(unsigned long now) {
  return ((now / kUiActivityPeriodMs) % 2UL) != 0UL;
}

constexpr bool uiActivityEnabled(bool standby, bool activityOwnedByState) {
  return !standby && activityOwnedByState;
}

constexpr unsigned long uiRailSweepLegStep(unsigned long now, unsigned long stepPeriodMs,
                                           int stepsPerLeg) {
  return ((now / stepPeriodMs) % (static_cast<unsigned long>(stepsPerLeg) * 2UL)) <=
                 static_cast<unsigned long>(stepsPerLeg)
             ? (now / stepPeriodMs) % (static_cast<unsigned long>(stepsPerLeg) * 2UL)
             : static_cast<unsigned long>(stepsPerLeg) * 2UL -
                   ((now / stepPeriodMs) % (static_cast<unsigned long>(stepsPerLeg) * 2UL));
}

constexpr int uiRailSweepOffset(unsigned long now, unsigned long stepPeriodMs, int travel,
                                int stepsPerLeg) {
  return stepPeriodMs == 0 || travel <= 0 || stepsPerLeg <= 0
             ? 0
             : static_cast<int>((uiRailSweepLegStep(now, stepPeriodMs, stepsPerLeg) *
                                 static_cast<unsigned long>(travel) +
                                 static_cast<unsigned long>(stepsPerLeg / 2)) /
                                static_cast<unsigned long>(stepsPerLeg));
}

// A result dwell may only return to Ready while a result screen is still shown.
constexpr bool uiResultDwellMayExpire(bool resultScreenVisible) { return resultScreenVisible; }

constexpr bool uiDeadlineReached(unsigned long now, unsigned long deadline) {
  return deadline != 0 && static_cast<long>(now - deadline) >= 0;
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

// Boot intro, timed like the website's lockup: the chevron's five pixels top to
// bottom, the stem's five rows bottom-up, the dot's four slots, then the name.
constexpr int kBrandIntroChevronCells = 5;
constexpr int kBrandIntroBodyCells = 10;
constexpr int kBrandIntroSteps = kBrandIntroBodyCells + 4 + 1;

// Elapsed time at which the intro reaches `step` (1 .. kBrandIntroSteps).
constexpr unsigned long brandIntroStepAt(int step) {
  return step <= kBrandIntroChevronCells
             ? 150UL + 90UL * static_cast<unsigned long>(step - 1)
         : step <= kBrandIntroBodyCells
             ? 700UL + 70UL * static_cast<unsigned long>(step - kBrandIntroChevronCells - 1)
         : step < kBrandIntroSteps
             ? 1100UL + 80UL * static_cast<unsigned long>(step - kBrandIntroBodyCells - 1)
             : 1450UL;
}

// Number of intro elements built after `elapsedMs`; kBrandIntroSteps is the full lockup.
constexpr int brandIntroStepFrom(int step, unsigned long elapsedMs) {
  return step < kBrandIntroSteps && elapsedMs >= brandIntroStepAt(step + 1)
             ? brandIntroStepFrom(step + 1, elapsedMs)
             : step;
}

constexpr int brandIntroStep(unsigned long elapsedMs) { return brandIntroStepFrom(0, elapsedMs); }

// Dot slots switched on so far, in reading order.
constexpr uint8_t brandIntroDot(int step) { return brandDotSlotsUpTo(step - kBrandIntroBodyCells); }

}  // namespace inventatory_scan
