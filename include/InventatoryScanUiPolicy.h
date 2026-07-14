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

constexpr bool uiDeadlineReached(unsigned long now, unsigned long deadline) {
  return deadline != 0 && static_cast<long>(now - deadline) >= 0;
}

}  // namespace inventatory_scan
