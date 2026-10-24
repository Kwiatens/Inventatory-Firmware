// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

// Public keys that may sign updates over Wi-Fi (docs/ota-protocol.md). Each is 130 hex characters, an
// uncompressed P-256 point, as `tools/sign_firmware.py pubkey` prints it. Two slots allow a key to be
// rotated: ship a release that trusts both keys, then sign with the new one.
//
// With both slots empty the firmware accepts no update over Wi-Fi; USB flashing always works.
// Building your own firmware? Put your own public key here, or build with
// -D INVENTATORY_OTA_KEY_0='"04…"' (and _1), and sign your packages with your own key.
#pragma once

namespace inventatory_scan {

#ifdef INVENTATORY_OTA_KEY_0
constexpr char kOtaTrustedKey0[] = INVENTATORY_OTA_KEY_0;
#else
constexpr char kOtaTrustedKey0[] = "";
#endif

#ifdef INVENTATORY_OTA_KEY_1
constexpr char kOtaTrustedKey1[] = INVENTATORY_OTA_KEY_1;
#else
constexpr char kOtaTrustedKey1[] = "";
#endif

}  // namespace inventatory_scan
