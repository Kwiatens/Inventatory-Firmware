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

void test_parseTmeBagLabel_quantity_bounds() {
  // A TME quantity above 9999 refuses the label. Before, "10000" showed and was added as 1000.
  TmeBagLabel label;
  TEST_ASSERT_FALSE(parseTmeBagLabel("QTY:10000 PN:0ZCM0010FF2G MPN:0ZCM0010FF2G", label));
  TEST_ASSERT_FALSE(parseTmeBagLabel("QTY:999999 PN:0ZCM0010FF2G MPN:0ZCM0010FF2G", label));
  TEST_ASSERT_FALSE(parseTmeBagLabel("QTY:99999999999999999999 PN:0ZCM0010FF2G MPN:0ZCM0010FF2G", label));

  TEST_ASSERT_TRUE(parseTmeBagLabel("QTY:9999 PN:0ZCM0010FF2G MPN:0ZCM0010FF2G", label));
  TEST_ASSERT_EQUAL_INT(9999, label.quantity);
  QuantityComposer composer;
  composer.setDefaultValue(label.quantity);
  TEST_ASSERT_EQUAL_STRING("9999", composer.displayText().c_str());
  TEST_ASSERT_EQUAL_INT(9999, composer.consume(true));

  // Leading zeros are digits, not extra quantity; zero keeps the default of one.
  TEST_ASSERT_TRUE(parseTmeBagLabel("QTY:0005 PN:0ZCM0010FF2G MPN:0ZCM0010FF2G", label));
  TEST_ASSERT_EQUAL_INT(5, label.quantity);
  TEST_ASSERT_TRUE(parseTmeBagLabel("QTY:0 PN:0ZCM0010FF2G MPN:0ZCM0010FF2G", label));
  TEST_ASSERT_EQUAL_INT(1, label.quantity);
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

void test_latin1DisplayText_reencodes_utf8_for_the_lcd() {
  // ASCII is unchanged.
  TEST_ASSERT_TRUE(latin1DisplayText("Resistor 10k 0603") == "Resistor 10k 0603");
  // Latin-1 code points become one byte each: the font draws them as one glyph.
  TEST_ASSERT_TRUE(latin1DisplayText("10\xC2\xB5" "F") == String("10\xB5" "F"));
  TEST_ASSERT_TRUE(latin1DisplayText("\xC2\xB1" "5%") == String("\xB1" "5%"));
  TEST_ASSERT_TRUE(latin1DisplayText("Gr\xC3\xBC" "n") == String("Gr\xFC" "n"));
  // Greek mu is the micro sign; both ohm characters are spelled out; dashes become '-'.
  TEST_ASSERT_TRUE(latin1DisplayText("4.7\xCE\xBC" "F") == String("4.7\xB5" "F"));
  TEST_ASSERT_TRUE(latin1DisplayText("10k\xCE\xA9") == "10kOhm");
  TEST_ASSERT_TRUE(latin1DisplayText("10k\xE2\x84\xA6") == "10kOhm");
  TEST_ASSERT_TRUE(latin1DisplayText("A\xE2\x80\x93" "B") == "A-B");
  // Other characters, C1 controls and malformed bytes become '?', never raw bytes.
  TEST_ASSERT_TRUE(latin1DisplayText("\xE6\x97\xA5") == "?");
  TEST_ASSERT_TRUE(latin1DisplayText("\xF0\x9F\x94\x8C" "x") == "?x");
  TEST_ASSERT_TRUE(latin1DisplayText("\xC2\x85") == "?");
  TEST_ASSERT_TRUE(latin1DisplayText("a\xC3") == "a?");
  TEST_ASSERT_TRUE(latin1DisplayText("\xB5" "F") == "?F");
  TEST_ASSERT_TRUE(latin1DisplayText("\xE2\x84" "A") == "??A");
}

void test_sync_response_display_fields_are_reencoded() {
  const String body =
      "{\"protocolVersion\":1,\"requestId\":\"sync-u\",\"acceptedEventIds\":[\"e1\"],"
      "\"results\":[{\"resultId\":\"r-\xC2\xB5\",\"eventId\":\"e1\",\"status\":\"completed\","
      "\"itemName\":\"10\xC2\xB5" "F 0805\",\"location\":\"Box \xCE\xA9\",\"message\":\"OK\"}],"
      "\"quickLabels\":{\"revision\":1,\"presets\":[\"1k\xCE\xA9\"]}}";
  auto response = parseSyncResponseJson(body);
  TEST_ASSERT_TRUE(response.valid);
  prepareSyncResponseForDisplay(response);
  TEST_ASSERT_TRUE(response.result.itemName == String("10\xB5" "F 0805"));
  TEST_ASSERT_TRUE(response.result.location == "Box Ohm");
  TEST_ASSERT_TRUE(response.quickLabelPresets[0] == "1kOhm");
  // Identifiers stay as the desktop sent them: the acknowledgement must match its records.
  TEST_ASSERT_TRUE(response.result.resultId == String("r-\xC2\xB5"));
  TEST_ASSERT_TRUE(response.acceptedEventIds[0] == "e1");
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
  RUN_TEST(test_parseTmeBagLabel_quantity_bounds);
  RUN_TEST(test_quantity_composer_limit);
  RUN_TEST(test_wipeSecret_clears_buffer);
  RUN_TEST(test_http_host_name_rejects_header_injection);
  RUN_TEST(test_latin1DisplayText_reencodes_utf8_for_the_lcd);
  RUN_TEST(test_sync_response_display_fields_are_reencoded);
}
