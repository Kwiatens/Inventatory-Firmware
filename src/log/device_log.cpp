// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

#include "log/device_log.h"

#include <WiFi.h>
#include <WiFiUdp.h>
#include <freertos/FreeRTOS.h>

#include <cstdio>
#include <cstring>

namespace inventatory_scan {

namespace {

// At most this many lines per call, so the loop is never held for long.
constexpr int kLogLinesPerSend = 4;
// The loop sends at most this often.
constexpr unsigned long kLogSendIntervalMs = 100UL;

// Guards the ring and the line being built. Both cores write to the log, so a task switch cannot split a line.
portMUX_TYPE gLogMux = portMUX_INITIALIZER_UNLOCKED;
WiFiUDP gLogUdp;

}  // namespace

size_t DeviceLog::write(uint8_t byte) {
  const size_t written = Serial.write(byte);
  if (!kLogOverAirEnabled) return written;
  const uint32_t now = millis();
  portENTER_CRITICAL(&gLogMux);
  assembler_.add(byte, now, ring_);
  portEXIT_CRITICAL(&gLogMux);
  return written;
}

size_t DeviceLog::write(const uint8_t* buffer, size_t size) {
  const size_t written = Serial.write(buffer, size);
  if (!kLogOverAirEnabled) return written;
  const uint32_t now = millis();
  portENTER_CRITICAL(&gLogMux);
  for (size_t index = 0; index < size; ++index) assembler_.add(buffer[index], now, ring_);
  portEXIT_CRITICAL(&gLogMux);
  return written;
}

void DeviceLog::serviceOverAir(unsigned long nowMs) {
  if (!kLogOverAirEnabled || nowMs - lastSendMs_ < kLogSendIntervalMs) return;
  lastSendMs_ = nowMs;
  if (!WiFi.isConnected()) return;
  // The broadcast address of the local network: the device address with the host bits set. Each line is one
  // datagram, "<milliseconds> <text>", so the listener on the PC needs no framing.
  const IPAddress broadcast(static_cast<uint32_t>(WiFi.localIP()) | ~static_cast<uint32_t>(WiFi.subnetMask()));
  for (int sent = 0; sent < kLogLinesPerSend; ++sent) {
    LogLine line;
    portENTER_CRITICAL(&gLogMux);
    const bool found = ring_.pop(line);
    portEXIT_CRITICAL(&gLogMux);
    if (!found) return;
    char prefix[16];
    snprintf(prefix, sizeof(prefix), "%lu ", static_cast<unsigned long>(line.timestampMs));
    if (gLogUdp.beginPacket(broadcast, kLogUdpPort) == 0) return;
    gLogUdp.write(reinterpret_cast<const uint8_t*>(prefix), std::strlen(prefix));
    gLogUdp.write(reinterpret_cast<const uint8_t*>(line.text), line.length);
    gLogUdp.endPacket();
  }
}

}  // namespace inventatory_scan

inventatory_scan::DeviceLog deviceLog;
