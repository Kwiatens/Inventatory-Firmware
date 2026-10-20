// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <Arduino.h>

#include "InventatoryScanLogLines.h"

namespace inventatory_scan {

// Set to false to keep the log on the USB serial port only.
constexpr bool kLogOverAirEnabled = true;
// The UDP port the log is broadcast on. tools/device_log_listener.py listens on it.
constexpr uint16_t kLogUdpPort = 5140;

// The device log. Every line goes to the USB serial port as before, and is also kept so that it can be sent over
// the air to the local network. Both cores write, so the buffer is guarded by a spinlock. serviceOverAir() runs on
// core 1 only, from the app loop, and never waits for the network.
class DeviceLog : public Print {
 public:
  size_t write(uint8_t byte) override;
  size_t write(const uint8_t* buffer, size_t size) override;
  // Broadcasts the waiting lines while Wi-Fi is up: a few lines per call, and at most once per send interval.
  void serviceOverAir(unsigned long nowMs);

 private:
  LogLineRing ring_;
  LogLineAssembler assembler_;
  unsigned long lastSendMs_ = 0;
};

}  // namespace inventatory_scan

extern inventatory_scan::DeviceLog deviceLog;
