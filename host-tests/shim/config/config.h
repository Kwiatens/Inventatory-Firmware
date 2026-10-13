// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

// Host test configuration. The network code includes "config/config.h"; on the host
// that resolves to this file, so the build never reads a private device configuration.
// The values match config.example.h, and contain no secrets.
#pragma once

const char DEVICE_ID[] = "Inventatory-SCAN-R1";
const char MDNS_SERVICE[] = "inventatory";
const char MDNS_PROTOCOL[] = "tcp";
const unsigned long WIFI_RECONNECT_INTERVAL_MS = 15000;
const unsigned long ENDPOINT_RESOLVE_RETRY_INTERVAL_MS = 5000;
