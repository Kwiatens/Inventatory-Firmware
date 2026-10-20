// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

// Host entry point: runs the shared device suite (setup()) and the host-only NVS fault
// tests (runHostSuite()). Exits non-zero if either fails.
#include <unity.h>

#include "Preferences.h"

void setup();
void runHostSuite();

// Each test starts from empty NVS, as on a freshly flashed board.
extern "C" void setUp(void) { host_nvs::reset(); }
extern "C" void tearDown(void) {}

int main() {
  setup();
  // UNITY_BEGIN resets the failure count, so keep the first session's result.
  const int sharedFailures = Unity.TestFailures;
  runHostSuite();
  return (sharedFailures + Unity.TestFailures) == 0 ? 0 : 1;
}
