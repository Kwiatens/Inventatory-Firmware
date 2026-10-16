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
  // fail closed: padded body is parsed only up to max limit
  String valid = R"json({"protocolVersion":1,"requestId":"sync-pad","acceptedEventIds":[],"results":[]})json";
  while (valid.length() < kMaxHttpBodyBytes) valid += ' ';
  TEST_ASSERT_TRUE(parseSyncResponseJson(valid).valid);
  valid += ' ';
  TEST_ASSERT_FALSE(parseSyncResponseJson(valid).valid);
}

void test_parseSyncResponseJson_strict_prefix_rejected() {
  // fail closed: a truncated body is dropped
  const String valid = R"json({"protocolVersion":1,"requestId":"sync-pref","acceptedEventIds":[],"results":[]})json";
  for (std::size_t i = 0; i < valid.length(); ++i) {
    TEST_ASSERT_FALSE(parseSyncResponseJson(valid.substring(0, i)).valid);
  }
}

void test_parseSyncResponseJson_invalid_protocol_or_request_id() {
  // fail closed: protocolVersion != 1 or missing/empty requestId is rejected
  TEST_ASSERT_FALSE(parseSyncResponseJson(R"json({"protocolVersion":0,"requestId":"sync-1","acceptedEventIds":[],"results":[]})json").valid);
  TEST_ASSERT_FALSE(parseSyncResponseJson(R"json({"protocolVersion":2,"requestId":"sync-1","acceptedEventIds":[],"results":[]})json").valid);
  TEST_ASSERT_FALSE(parseSyncResponseJson(R"json({"protocolVersion":1,"acceptedEventIds":[],"results":[]})json").valid);
  TEST_ASSERT_FALSE(parseSyncResponseJson(R"json({"protocolVersion":1,"requestId":"","acceptedEventIds":[],"results":[]})json").valid);
}

void test_parseSyncResponseJson_duplicate_event_id_rejected() {
  // fail closed: duplicate accepted event id is rejected
  TEST_ASSERT_FALSE(parseSyncResponseJson(R"json({"protocolVersion":1,"requestId":"sync-1","acceptedEventIds":["ev-1","ev-2","ev-1"],"results":[]})json").valid);
}

void test_parseSyncResponseJson_integer_limits() {
  // int limit: integer field accepts INT_MAX and INT_MIN but not INT_MAX + 1
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
  // int limit: fractional number in an integer field is rejected
  String templateStart = R"json({"protocolVersion":1,"requestId":"sync-1","acceptedEventIds":[],"results":[{"resultId":"r1","eventId":"e1","status":"completed","quantity":)json";
  String templateEnd = R"json(}]})json";
  TEST_ASSERT_FALSE(parseSyncResponseJson(templateStart + "1.5" + templateEnd).valid);
}

void test_transportMacMatches_combinations() {
  // mac check: strictly identical 64-char strings match
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
  // validation: token format rules
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
  // fail closed: empty MAC for invalid token
  TEST_ASSERT_EQUAL_STRING("", transportMac("invalid", "client-to-server", "input").c_str());
  TEST_ASSERT_EQUAL_STRING("", deviceRequestMac("invalid", "POST", "/path", "dev1", 1, "body").c_str());
}

void test_deviceRequestMac_changes() {
  // mac check: varying any field changes the MAC
  String token = "0000000000000000000000000000000000000000000000000000000000000000";
  String baseMac = deviceRequestMac(token, "POST", "/path", "dev1", 1, "body");

  TEST_ASSERT_FALSE(baseMac == deviceRequestMac(token, "GET", "/path", "dev1", 1, "body"));
  TEST_ASSERT_FALSE(baseMac == deviceRequestMac(token, "POST", "/other", "dev1", 1, "body"));
  TEST_ASSERT_FALSE(baseMac == deviceRequestMac(token, "POST", "/path", "dev2", 1, "body"));
  TEST_ASSERT_FALSE(baseMac == deviceRequestMac(token, "POST", "/path", "dev1", 2, "body"));
  TEST_ASSERT_FALSE(baseMac == deviceRequestMac(token, "POST", "/path", "dev1", 1, "ody"));
}

void test_deviceResponseMac_changes() {
  // mac check: directional keys separate and fields matter
  String token = "0000000000000000000000000000000000000000000000000000000000000000";
  String baseMac = deviceResponseMac(token, 1, 200, "body");

  TEST_ASSERT_FALSE(baseMac == deviceResponseMac(token, 2, 200, "body"));
  TEST_ASSERT_FALSE(baseMac == deviceResponseMac(token, 1, 404, "body"));
  TEST_ASSERT_FALSE(baseMac == deviceResponseMac(token, 1, 200, "ody"));

  String reqMac = deviceRequestMac(token, "POST", "/path", "dev1", 1, "body");
  TEST_ASSERT_FALSE(baseMac == reqMac);
  // The same message under the two directional keys must not produce the same MAC.
  TEST_ASSERT_FALSE(transportMac(token, "client-to-server", "message") ==
                    transportMac(token, "server-to-client", "message"));
}

void test_decodeComponentDataMatrix_missing_prefix_and_empty() {
  // fail closed: rejects missing prefix or length 0
  DecodedComponentFields decoded;
  TEST_ASSERT_FALSE(decodeComponentDataMatrix("", decoded));

  const String noPrefix = String(static_cast<char>(0x1E)) + "06" + static_cast<char>(0x1D) +
      "1PTPS7A2033PDBVR" + static_cast<char>(0x1D) + "N" + static_cast<char>(0x1E) + static_cast<char>(0x04);
  TEST_ASSERT_FALSE(decodeComponentDataMatrix(noPrefix, decoded));
}

// A label that carries quantity text after the part number.
String labelWithQuantity(const char* quantityText) {
  return String("[)>") + static_cast<char>(0x1E) + "06" + static_cast<char>(0x1D) + "1PTPS7A2033PDBVR" +
         static_cast<char>(0x1D) + "Q" + quantityText + static_cast<char>(0x1E) + static_cast<char>(0x04);
}

void test_decodeComponentDataMatrix_quantity_bounds() {
  // fail closed: a quantity above 9999 rejects the scan; it is never truncated
  DecodedComponentFields decoded;
  TEST_ASSERT_FALSE(decodeComponentDataMatrix(labelWithQuantity("10000"), decoded));

  TEST_ASSERT_TRUE(decodeComponentDataMatrix(labelWithQuantity("9999"), decoded));
  TEST_ASSERT_EQUAL_INT(9999, decoded.quantity);

  // zero is not a quantity, so the default of one stays in place
  TEST_ASSERT_TRUE(decodeComponentDataMatrix(labelWithQuantity("0"), decoded));
  TEST_ASSERT_EQUAL_INT(1, decoded.quantity);
}

void test_quantity_composer_limit() {
  // quantity limit: composer never yields > 9999
  QuantityComposer composer;
  composer.appendDigit('9');
  composer.appendDigit('9');
  composer.appendDigit('9');
  composer.appendDigit('9');
  composer.appendDigit('9');
  TEST_ASSERT_EQUAL_INT(9999, composer.valueOrOne());
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
}
