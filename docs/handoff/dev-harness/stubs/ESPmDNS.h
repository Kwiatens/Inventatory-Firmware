// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

#pragma once
#include <Arduino.h>
struct MDNSResponder { bool begin(const char*) { return true; } int queryService(const char*, const char*) { return 0; } IPAddress IP(int) { return IPAddress(); } uint16_t port(int) { return 0; } String hostname(int) { return String(); } };
inline MDNSResponder MDNS;
