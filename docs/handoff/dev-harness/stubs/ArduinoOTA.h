// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

#pragma once
#include <functional>
enum ota_error_t { OTA_AUTH_ERROR, OTA_BEGIN_ERROR, OTA_CONNECT_ERROR, OTA_RECEIVE_ERROR, OTA_END_ERROR };
struct ArduinoOTAClass {
  void setHostname(const char*) {} void setPassword(const char*) {} void begin() {} void end() {} void handle() {}
  void onStart(std::function<void()>) {} void onEnd(std::function<void()>) {}
  void onProgress(std::function<void(unsigned, unsigned)>) {} void onError(std::function<void(ota_error_t)>) {}
};
inline ArduinoOTAClass ArduinoOTA;
