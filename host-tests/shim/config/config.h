// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

// Host config: includes resolve here, so the build never reads the private config.h.
// Values match config.example.h and contain no secrets.
#pragma once

const char DEVICE_ID[] = "Inventatory-SCAN-R1";
const char MDNS_SERVICE[] = "inventatory";
const char MDNS_PROTOCOL[] = "tcp";
const unsigned long WIFI_RECONNECT_INTERVAL_MS = 15000;
const unsigned long ENDPOINT_RESOLVE_RETRY_INTERVAL_MS = 5000;
