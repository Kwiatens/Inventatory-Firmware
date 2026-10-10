// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

// Host entry point. It runs two Unity sessions and exits non-zero if either
// failed, so CI can gate on the result:
//   1. setup(): the shared suite in ../test, the same file the device runs.
//   2. runHostSuite(): host-only tests that need the NVS fault injection.
#include <unity.h>

#include "Preferences.h"

void setup();
void runHostSuite();

// Unity's RUN_TEST calls these around every test. Each test starts from an
// empty NVS, as a freshly flashed board would, so no test depends on another.
extern "C" void setUp(void) { host_nvs::reset(); }
extern "C" void tearDown(void) {}

int main() {
  setup();
  // UNITY_BEGIN resets the failure count, so keep the first session's result.
  const int sharedFailures = Unity.TestFailures;
  runHostSuite();
  return (sharedFailures + Unity.TestFailures) == 0 ? 0 : 1;
}
