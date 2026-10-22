// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

#include <unity.h>

#include <climits>

#include "InventatoryScanCore.h"
#include "InventatoryScanHttp.h"
#include "InventatoryScanSecurity.h"
#include "InventatoryScanSync.h"

using namespace inventatory_scan;

void test_parseSyncResponseJson_padded_body() {
  String valid = R"json({"protocolVersion":1,"requestId":"sync-pad","acceptedEventIds":[],"results":[]})json";
  while (valid.length() < kMaxHttpBodyBytes) valid += ' ';
  TEST_ASSERT_TRUE(parseSyncResponseJson(valid).valid);
  valid += ' ';
  TEST_ASSERT_FALSE(parseSyncResponseJson(valid).valid);
}

void test_parseSyncResponseJson_strict_prefix_rejected() {
  const String valid = R"json({"protocolVersion":1,"requestId":"sync-pref","acceptedEventIds":[],"results":[]})json";
  for (std::size_t i = 0; i < valid.length(); ++i) {
    TEST_ASSERT_FALSE(parseSyncResponseJson(valid.substring(0, i)).valid);
  }
}

void test_parseSyncResponseJson_invalid_protocol_or_request_id() {
  TEST_ASSERT_FALSE(parseSyncResponseJson(R"json({"protocolVersion":0,"requestId":"sync-1","acceptedEventIds":[],"results":[]})json").valid);
  TEST_ASSERT_FALSE(parseSyncResponseJson(R"json({"protocolVersion":2,"requestId":"sync-1","acceptedEventIds":[],"results":[]})json").valid);
  TEST_ASSERT_FALSE(parseSyncResponseJson(R"json({"protocolVersion":1,"acceptedEventIds":[],"results":[]})json").valid);
  TEST_ASSERT_FALSE(parseSyncResponseJson(R"json({"protocolVersion":1,"requestId":"","acceptedEventIds":[],"results":[]})json").valid);
}

void test_parseSyncResponseJson_duplicate_event_id_rejected() {
  TEST_ASSERT_FALSE(parseSyncResponseJson(R"json({"protocolVersion":1,"requestId":"sync-1","acceptedEventIds":["ev-1","ev-2","ev-1"],"results":[]})json").valid);
}

void test_parseSyncResponseJson_integer_limits() {
  String intMaxStr = String(INT_MAX);
  String intMinStr = String(INT_MIN);
  String intMaxPlusOne = "2147483648";

  String templateStart = R"json({"protocolVersion":1,"requestId":"sync-1","acceptedEventIds":[],"results":[{"resultId":"r1","eventId":"e1","status":"completed","quantity":)json";
  String templateEnd = R"json(}]})json";

  TEST_ASSERT_TRUE(parseSyncResponseJson(templateStart + intMaxStr + templateEnd).valid);
  TEST_ASSERT_TRUE(parseSyncResponseJson(templateStart + intMinStr + templateEnd).valid);
  TEST_ASSERT_FALSE(parseSyncResponseJson(templateStart + intMaxPlusOne + templateEnd).valid);
}

void test_parseSyncResponseJson_fractional_rejected() {
  String templateStart = R"json({"protocolVersion":1,"requestId":"sync-1","acceptedEventIds":[],"results":[{"resultId":"r1","eventId":"e1","status":"completed","quantity":)json";
  String templateEnd = R"json(}]})json";
  TEST_ASSERT_FALSE(parseSyncResponseJson(templateStart + "1.5" + templateEnd).valid);
}

void test_transportMacMatches_combinations() {
  const String base = "0000000000000000000000000000000000000000000000000000000000000000";
  TEST_ASSERT_TRUE(transportMacMatches(base, base));

  String diffChar = base;
  diffChar[15] = '1';
  TEST_ASSERT_FALSE(transportMacMatches(base, diffChar));

  String diffLen = base + "0";
  TEST_ASSERT_FALSE(transportMacMatches(base, diffLen));

  String diffLen2 = base.substring(0, 63);
  TEST_ASSERT_FALSE(transportMacMatches(base, diffLen2));

  String allOnes = "1111111111111111111111111111111111111111111111111111111111111111";
  TEST_ASSERT_FALSE(transportMacMatches(base, allOnes));
}

void test_decodeTransportToken_hex_cases() {
  uint8_t bytes[32];
  String validLower = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
  String validUpper = "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF";
  TEST_ASSERT_TRUE(decodeTransportToken(validLower, bytes));
  TEST_ASSERT_TRUE(decodeTransportToken(validUpper, bytes));

  TEST_ASSERT_FALSE(decodeTransportToken(validLower.substring(0, 63), bytes));
  TEST_ASSERT_FALSE(decodeTransportToken(validLower + "0", bytes));

  String nonHex = validLower;
  nonHex[10] = 'z';
  TEST_ASSERT_FALSE(decodeTransportToken(nonHex, bytes));

  String withSpace = validLower;
  withSpace[15] = ' ';
  TEST_ASSERT_FALSE(decodeTransportToken(withSpace, bytes));
}

void test_transportMac_invalid_token() {
  TEST_ASSERT_EQUAL_STRING("", transportMac("invalid", "client-to-server", "input").c_str());
  TEST_ASSERT_EQUAL_STRING("", deviceRequestMac("invalid", "POST", "/path", "dev1", 1, "body").c_str());
}

void test_deviceRequestMac_changes() {
  String token = "0000000000000000000000000000000000000000000000000000000000000000";
  String baseMac = deviceRequestMac(token, "POST", "/path", "dev1", 1, "body");

  TEST_ASSERT_FALSE(baseMac == deviceRequestMac(token, "GET", "/path", "dev1", 1, "body"));
  TEST_ASSERT_FALSE(baseMac == deviceRequestMac(token, "POST", "/other", "dev1", 1, "body"));
  TEST_ASSERT_FALSE(baseMac == deviceRequestMac(token, "POST", "/path", "dev2", 1, "body"));
  TEST_ASSERT_FALSE(baseMac == deviceRequestMac(token, "POST", "/path", "dev1", 2, "body"));
  TEST_ASSERT_FALSE(baseMac == deviceRequestMac(token, "POST", "/path", "dev1", 1, "ody"));
}

void test_deviceResponseMac_changes() {
  String token = "0000000000000000000000000000000000000000000000000000000000000000";
  String baseMac = deviceResponseMac(token, 1, 200, "body");

  TEST_ASSERT_FALSE(baseMac == deviceResponseMac(token, 2, 200, "body"));
  TEST_ASSERT_FALSE(baseMac == deviceResponseMac(token, 1, 404, "body"));
  TEST_ASSERT_FALSE(baseMac == deviceResponseMac(token, 1, 200, "ody"));

  String reqMac = deviceRequestMac(token, "POST", "/path", "dev1", 1, "body");
  TEST_ASSERT_FALSE(baseMac == reqMac);
  // Directional keys must give different MACs for the same message.
  TEST_ASSERT_FALSE(transportMac(token, "client-to-server", "message") ==
                    transportMac(token, "server-to-client", "message"));
}

void test_decodeComponentDataMatrix_missing_prefix_and_empty() {
  DecodedComponentFields decoded;
  TEST_ASSERT_FALSE(decodeComponentDataMatrix("", decoded));

  const String noPrefix = String(static_cast<char>(0x1E)) + "06" + static_cast<char>(0x1D) +
      "1PTPS7A2033PDBVR" + static_cast<char>(0x1D) + "N" + static_cast<char>(0x1E) + static_cast<char>(0x04);
  TEST_ASSERT_FALSE(decodeComponentDataMatrix(noPrefix, decoded));
}

String labelWithQuantity(const char* quantityText) {
  return String("[)>") + static_cast<char>(0x1E) + "06" + static_cast<char>(0x1D) + "1PTPS7A2033PDBVR" +
         static_cast<char>(0x1D) + "Q" + quantityText + static_cast<char>(0x1E) + static_cast<char>(0x04);
}

void test_decodeComponentDataMatrix_quantity_bounds() {
  // A quantity above 9999 rejects the scan rather than truncating it.
  DecodedComponentFields decoded;
  TEST_ASSERT_FALSE(decodeComponentDataMatrix(labelWithQuantity("10000"), decoded));

  TEST_ASSERT_TRUE(decodeComponentDataMatrix(labelWithQuantity("9999"), decoded));
  TEST_ASSERT_EQUAL_INT(9999, decoded.quantity);

  // Zero is not a quantity; the default of one stays.
  TEST_ASSERT_TRUE(decodeComponentDataMatrix(labelWithQuantity("0"), decoded));
  TEST_ASSERT_EQUAL_INT(1, decoded.quantity);
}

void test_quantity_composer_limit() {
  QuantityComposer composer;
  composer.appendDigit('9');
  composer.appendDigit('9');
  composer.appendDigit('9');
  composer.appendDigit('9');
  composer.appendDigit('9');
  TEST_ASSERT_EQUAL_INT(9999, composer.valueOrOne());
}

void test_ota_password_policy() {
  // The example placeholder is public; a build that keeps it must not accept an update.
  TEST_ASSERT_FALSE(isUsableOtaPassword(kOtaPasswordPlaceholder));
  TEST_ASSERT_FALSE(isUsableOtaPassword("paste-your-ota-password-here"));
  TEST_ASSERT_FALSE(isUsableOtaPassword(nullptr));
  TEST_ASSERT_FALSE(isUsableOtaPassword(""));
  TEST_ASSERT_FALSE(isUsableOtaPassword("elevenchars"));
  TEST_ASSERT_TRUE(isUsableOtaPassword("twelve-chars"));
  TEST_ASSERT_TRUE(isUsableOtaPassword("a much longer private OTA password"));
}

void test_wipeSecret_clears_buffer() {
  uint8_t secret[32];
  for (auto& byte : secret) byte = 0xA5U;
  wipeSecret(secret, sizeof(secret));
  for (const auto byte : secret) TEST_ASSERT_EQUAL_UINT8(0U, byte);
}

void test_http_host_name_rejects_header_injection() {
  TEST_ASSERT_TRUE(isSafeHttpHostName("inventatory-pc", 14));
  TEST_ASSERT_TRUE(isSafeHttpHostName("inventatory-pc.local", 20));
  const char injected[] = "pc\r\nX-Inventatory-Mac: 00";
  TEST_ASSERT_FALSE(isSafeHttpHostName(injected, sizeof(injected) - 1U));
  TEST_ASSERT_FALSE(isSafeHttpHostName("pc name", 7));
  TEST_ASSERT_FALSE(isSafeHttpHostName("pc:80", 5));
  const char embeddedNul[] = {'p', 'c', '\0', 'x'};
  TEST_ASSERT_FALSE(isSafeHttpHostName(embeddedNul, sizeof(embeddedNul)));
  TEST_ASSERT_FALSE(isSafeHttpHostName("", 0));
  TEST_ASSERT_FALSE(isSafeHttpHostName(nullptr, 3));
  String longest;
  while (longest.length() < kMaxHttpHostNameBytes) longest += 'a';
  TEST_ASSERT_TRUE(isSafeHttpHostName(longest.c_str(), longest.length()));
  const String tooLong = longest + "c";
  TEST_ASSERT_FALSE(isSafeHttpHostName(tooLong.c_str(), tooLong.length()));
}

void registerProtocolHardeningTests() {
  RUN_TEST(test_parseSyncResponseJson_padded_body);
  RUN_TEST(test_parseSyncResponseJson_strict_prefix_rejected);
  RUN_TEST(test_parseSyncResponseJson_invalid_protocol_or_request_id);
  RUN_TEST(test_parseSyncResponseJson_duplicate_event_id_rejected);
  RUN_TEST(test_parseSyncResponseJson_integer_limits);
  RUN_TEST(test_parseSyncResponseJson_fractional_rejected);
  RUN_TEST(test_transportMacMatches_combinations);
  RUN_TEST(test_decodeTransportToken_hex_cases);
  RUN_TEST(test_transportMac_invalid_token);
  RUN_TEST(test_deviceRequestMac_changes);
  RUN_TEST(test_deviceResponseMac_changes);
  RUN_TEST(test_decodeComponentDataMatrix_missing_prefix_and_empty);
  RUN_TEST(test_decodeComponentDataMatrix_quantity_bounds);
  RUN_TEST(test_quantity_composer_limit);
  RUN_TEST(test_ota_password_policy);
  RUN_TEST(test_wipeSecret_clears_buffer);
  RUN_TEST(test_http_host_name_rejects_header_injection);
}
