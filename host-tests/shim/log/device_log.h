// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

// Host stand-in for src/log/device_log.h. The host build keeps the serial output stand-in and sends nothing over
// the air, so the firmware's log calls compile the same way on both builds.
#pragma once

#include "Arduino.h"

inline HostSerial deviceLog;
