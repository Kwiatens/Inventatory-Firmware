// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

#include <unity.h>

#include "InventatoryScanBattery.h"
#include "InventatoryScanCore.h"
#include "InventatoryScanHttp.h"
#include "InventatoryScanOutbox.h"
#include "InventatoryScanSync.h"
#include "InventatoryScanUiPolicy.h"
#include "display/display_style.h"

using namespace inventatory_scan;

void registerProtocolHardeningTests();

void test_standardized_scanner_codes() {
  TEST_ASSERT_TRUE(isStandardMachineCode("0002"));
  TEST_ASSERT_FALSE(isStandardMachineCode("31"));
  TEST_ASSERT_FALSE(isStandardMachineCode("12A4"));
  TEST_ASSERT_TRUE(isStandardManufacturerPartNumber("TPS7A2033PDBVR"));
  TEST_ASSERT_TRUE(isStandardManufacturerPartNumber("DS3231S#T&R"));
  TEST_ASSERT_TRUE(isStandardManufacturerPartNumber("SSM3K329R,LF"));
  TEST_ASSERT_TRUE(isStandardManufacturerPartNumber("LG R971-KN-1"));
  TEST_ASSERT_FALSE(isStandardManufacturerPartNumber("boot:garbage"));
  TEST_ASSERT_TRUE(isStandardManufacturerPartNumber("SN74LVC1G14DBVR"));
}

void test_datamatrix_decoding_fixtures() {
  const String separated = String("]d2[)>") + static_cast<char>(0x1E) + "06" + static_cast<char>(0x1D) +
      "1PTPS7A2033PDBVR" + static_cast<char>(0x1D) + "1VTexas Instruments" +
      static_cast<char>(0x1D) + "NTPS7A20 LDO" + static_cast<char>(0x1D) + "Q25" +
      static_cast<char>(0x1E) + static_cast<char>(0x04);
  DecodedComponentFields decoded;
  TEST_ASSERT_TRUE(decodeComponentDataMatrix(separated, decoded));
  TEST_ASSERT_EQUAL_STRING("TPS7A2033PDBVR", decoded.manufacturerPartNumber.c_str());
  TEST_ASSERT_EQUAL_STRING("Texas Instruments", decoded.manufacturer.c_str());
  TEST_ASSERT_EQUAL_STRING("TPS7A20 LDO", decoded.encodedPartName.c_str());
  TEST_ASSERT_EQUAL_INT(25, decoded.quantity);

  // A Digi-Key bag prints its own part number in `P` ahead of the manufacturer
  // part number in `1P`.
  const String distributorBag = String("]d2[)>") + static_cast<char>(0x1E) + "06" + static_cast<char>(0x1D) +
      "P296-1234-1-ND" + static_cast<char>(0x1D) + "1PSN74LVC1G14DBVR" + static_cast<char>(0x1D) +
      "30P296-1234-1-ND" + static_cast<char>(0x1D) + "1TLOT12345" + static_cast<char>(0x1D) + "4LMY" +
      static_cast<char>(0x1D) + "Q100" + static_cast<char>(0x1E) + static_cast<char>(0x04);
  TEST_ASSERT_TRUE(decodeComponentDataMatrix(distributorBag, decoded));
  TEST_ASSERT_EQUAL_STRING("SN74LVC1G14DBVR", decoded.manufacturerPartNumber.c_str());
  TEST_ASSERT_EQUAL_STRING("296-1234-1-ND", decoded.auxiliaryPartNumber.c_str());
  TEST_ASSERT_EQUAL_STRING("", decoded.encodedPartName.c_str());
  TEST_ASSERT_EQUAL_INT(100, decoded.quantity);

  TEST_ASSERT_TRUE(decodeComponentDataMatrix("[)>06PNE555P1PLOT123Q10", decoded));
  TEST_ASSERT_EQUAL_STRING("LOT123", decoded.manufacturerPartNumber.c_str());
  TEST_ASSERT_EQUAL_STRING("NE555P", decoded.auxiliaryPartNumber.c_str());
  TEST_ASSERT_EQUAL_INT(10, decoded.quantity);

  TEST_ASSERT_TRUE(decodeComponentDataMatrix("[)>06PNE555P1PLOT123Q9999", decoded));
  TEST_ASSERT_EQUAL_INT(9999, decoded.quantity);
  TEST_ASSERT_FALSE(decodeComponentDataMatrix("[)>06PNE555P1PLOT123Q10000", decoded));

  const String nameOnly = String("[)>") + static_cast<char>(0x1E) + "06" + static_cast<char>(0x1D) +
      "NUnidentified sensor" + static_cast<char>(0x1E) + static_cast<char>(0x04);
  TEST_ASSERT_TRUE(decodeComponentDataMatrix(nameOnly, decoded));
  TEST_ASSERT_EQUAL_STRING("", decoded.manufacturerPartNumber.c_str());
  TEST_ASSERT_EQUAL_STRING("Unidentified sensor", decoded.encodedPartName.c_str());
  TEST_ASSERT_EQUAL_INT(1, decoded.quantity);

  TEST_ASSERT_FALSE(decodeComponentDataMatrix("[)>05PNE555P", decoded));
  TEST_ASSERT_FALSE(decodeComponentDataMatrix("[)>06Q2", decoded));
  String oversized;
  for (int index = 0; index < 513; ++index) oversized += 'A';
  TEST_ASSERT_FALSE(decodeComponentDataMatrix(oversized, decoded));
}

void test_quantity_composer_defaults_and_limit() {
  QuantityComposer composer;
  TEST_ASSERT_EQUAL_STRING("1", composer.displayText().c_str());
  TEST_ASSERT_EQUAL_INT(1, composer.valueOrOne());

  composer.appendDigit('0');
  composer.appendDigit('2');
  composer.appendDigit('5');
  composer.appendDigit('7');
  composer.appendDigit('9');
  // The leading 0 is ignored, so the fifth digit wraps onto the first slot.
  TEST_ASSERT_EQUAL_STRING("2579", composer.displayText().c_str());
  TEST_ASSERT_EQUAL_UINT32(0, composer.cursor());
  TEST_ASSERT_EQUAL_INT(2579, composer.valueOrOne());

  TEST_ASSERT_EQUAL_INT(2579, composer.consume(true));
  TEST_ASSERT_TRUE(composer.empty());
}

void test_quantity_composer_never_submits_a_value_it_does_not_show() {
  QuantityComposer composer;
  composer.setDefaultValue(25);
  composer.appendDigit('0');
  TEST_ASSERT_EQUAL_STRING("25", composer.displayText().c_str());
  TEST_ASSERT_EQUAL_INT(25, composer.consume(true));

  composer.setDefaultValue(25);
  composer.appendDigit('0');
  composer.appendDigit('5');
  TEST_ASSERT_EQUAL_STRING("5", composer.displayText().c_str());
  TEST_ASSERT_EQUAL_INT(5, composer.consume(true));

  // Overwriting the only non-zero digit with 0 is ignored.
  composer.setDefaultValue(25);
  composer.appendDigit('5');
  composer.cycleCursor();
  composer.appendDigit('0');
  TEST_ASSERT_EQUAL_STRING("5", composer.displayText().c_str());
  TEST_ASSERT_EQUAL_INT(5, composer.consume(true));

  // Deleting down to an all-zero value resets to 1, matching the display.
  composer.setDefaultValue(10);
  composer.deleteDigit();
  TEST_ASSERT_EQUAL_STRING("1", composer.displayText().c_str());
  TEST_ASSERT_EQUAL_INT(1, composer.consume(true));
}

void test_quantity_composer_cursor_replaces_and_appends() {
  QuantityComposer composer;
  composer.setDefaultValue(25);
  TEST_ASSERT_EQUAL_STRING("25", composer.displayText().c_str());
  composer.cycleCursor();
  TEST_ASSERT_EQUAL_UINT32(1, composer.cursor());
  composer.appendDigit('7');
  TEST_ASSERT_EQUAL_STRING("27", composer.displayText().c_str());
  TEST_ASSERT_EQUAL_UINT32(2, composer.cursor());
  composer.appendDigit('9');
  TEST_ASSERT_EQUAL_STRING("279", composer.displayText().c_str());
  TEST_ASSERT_EQUAL_UINT32(3, composer.cursor());
  composer.cycleCursor();
  TEST_ASSERT_EQUAL_UINT32(0, composer.cursor());
}

void test_quantity_composer_replaces_default_and_deletes_digits() {
  QuantityComposer composer;
  composer.setDefaultValue(10);
  composer.appendDigit('1');
  TEST_ASSERT_EQUAL_STRING("1", composer.displayText().c_str());
  TEST_ASSERT_EQUAL_UINT32(1, composer.cursor());

  composer.appendDigit('2');
  TEST_ASSERT_EQUAL_STRING("12", composer.displayText().c_str());
  composer.deleteDigit();
  TEST_ASSERT_EQUAL_STRING("1", composer.displayText().c_str());
  TEST_ASSERT_EQUAL_UINT32(1, composer.cursor());

  composer.deleteDigit();
  TEST_ASSERT_EQUAL_STRING("1", composer.displayText().c_str());
  TEST_ASSERT_EQUAL_UINT32(0, composer.cursor());
}

void test_quantity_composer_subtract_and_invalid_digits() {
  QuantityComposer composer;
  composer.appendDigit('A');
  composer.appendDigit('#');
  composer.appendDigit('3');
  TEST_ASSERT_EQUAL_STRING("3", composer.displayText().c_str());
  TEST_ASSERT_EQUAL_INT(-3, composer.consume(false));
  TEST_ASSERT_TRUE(composer.empty());
}

void test_request_json_serialization() {
  const auto json = buildQuantityRequestJson("  R1  ", "id-123", "Inventatory-001", -4);
  TEST_ASSERT_EQUAL_STRING(
      "{\"deviceId\":\"R1\",\"requestId\":\"id-123\",\"code\":\"Inventatory-001\",\"delta\":-4}",
      json.c_str());
}

void test_scan_json_escapes_datamatrix_separators() {
  const String code = String("[)>") + static_cast<char>(0x1E) + "06" + static_cast<char>(0x1D) +
                      "1PTPS7A2033PDBVR" + static_cast<char>(0x1D) + "Q2" + static_cast<char>(0x1E) +
                      static_cast<char>(0x04);
  const auto json = buildScanRequestJson("R1", "scan-1", code, 2);
  TEST_ASSERT_EQUAL_STRING(
      "{\"deviceId\":\"R1\",\"requestId\":\"scan-1\",\"code\":\"[)>\\u001e06\\u001d1PTPS7A2033PDBVR\\u001dQ2\\u001e\\u0004\",\"quantity\":2}",
      json.c_str());
}

void test_request_id_format() {
  const auto requestId = makeRequestId("  Inventatory-SCAN-R1  ", 42);
  TEST_ASSERT_EQUAL_STRING("Inventatory-SCAN-R1-42", requestId.c_str());
}

void test_fixed_ring_buffer_fifo_and_overwrite() {
  FixedRingBuffer<int, 3> buffer;
  TEST_ASSERT_TRUE(buffer.push(1));
  TEST_ASSERT_TRUE(buffer.push(2));
  TEST_ASSERT_TRUE(buffer.push(3));
  TEST_ASSERT_TRUE(buffer.full());

  TEST_ASSERT_TRUE(buffer.push(4));

  int value = 0;
  TEST_ASSERT_TRUE(buffer.pop(value));
  TEST_ASSERT_EQUAL_INT(2, value);
  TEST_ASSERT_TRUE(buffer.pop(value));
  TEST_ASSERT_EQUAL_INT(3, value);
  TEST_ASSERT_TRUE(buffer.pop(value));
  TEST_ASSERT_EQUAL_INT(4, value);
  TEST_ASSERT_TRUE(buffer.empty());
}

void test_outbox_pop_transition_is_staged_before_commit() {
  const auto current = makeOutboxQueueState(14, 2, 0, 43);
  OutboxQueueState next;

  TEST_ASSERT_TRUE(prepareOutboxPop(current, 16, next));
  TEST_ASSERT_EQUAL_UINT32(14, current.head);
  TEST_ASSERT_EQUAL_UINT32(2, current.count);
  TEST_ASSERT_EQUAL_UINT32(0, current.tail);
  TEST_ASSERT_EQUAL_UINT32(43, current.nextSequence);
  TEST_ASSERT_EQUAL_UINT32(15, next.head);
  TEST_ASSERT_EQUAL_UINT32(1, next.count);
  TEST_ASSERT_EQUAL_UINT32(0, next.tail);
  TEST_ASSERT_EQUAL_UINT32(43, next.nextSequence);

  const auto last = makeOutboxQueueState(15, 1, 0, 9);
  TEST_ASSERT_TRUE(prepareOutboxPop(last, 16, next));
  TEST_ASSERT_EQUAL_UINT32(0, next.head);
  TEST_ASSERT_EQUAL_UINT32(0, next.count);
  TEST_ASSERT_EQUAL_UINT32(0, next.tail);
  TEST_ASSERT_EQUAL_UINT32(9, next.nextSequence);

  TEST_ASSERT_FALSE(prepareOutboxPop(makeOutboxQueueState(0, 0, 0, 1), 16, next));
  TEST_ASSERT_FALSE(prepareOutboxPop(current, 0, next));
}

void test_outbox_enqueue_transition_is_bounded() {
  OutboxQueueState next;
  const auto full = makeOutboxQueueState(0, 16, 0, 17);
  TEST_ASSERT_FALSE(prepareOutboxEnqueue(full, 16, next));

  const auto current = makeOutboxQueueState(14, 1, 15, 9);
  TEST_ASSERT_TRUE(prepareOutboxEnqueue(current, 16, next));
  TEST_ASSERT_EQUAL_UINT32(14, next.head);
  TEST_ASSERT_EQUAL_UINT32(2, next.count);
  TEST_ASSERT_EQUAL_UINT32(0, next.tail);
  TEST_ASSERT_EQUAL_UINT32(10, next.nextSequence);

  const auto inconsistent = makeOutboxQueueState(14, 1, 7, 9);
  TEST_ASSERT_FALSE(prepareOutboxEnqueue(inconsistent, 16, next));
}

void test_protocol_v1_sync_round_trip() {
  DeviceEventRecord event;
  event.eventId = "R1-42";
  event.sequence = 42;
  event.kind = DeviceEventKind::InventoryAdjust;
  event.code = "0002";
  event.manufacturer = "Texas Instruments";
  event.manufacturerPartNumber = "TPS7A2033PDBVR";
  event.encodedPartName = "TPS7A20 LDO";
  event.value = -3;
  const String ack = "R1-41-result";
  const auto json = buildSyncRequestJson("sync-7", "R1", "0.2.0", "ready", -51, 1,
                                         &event, 1, &ack, 1, "lookup-9", "0002");
  TEST_ASSERT_TRUE(json.indexOf("\"protocolVersion\":1") >= 0);
  TEST_ASSERT_TRUE(json.indexOf("\"type\":\"inventory.adjust\"") >= 0);
  TEST_ASSERT_TRUE(json.indexOf("\"value\":-3") >= 0);
  TEST_ASSERT_TRUE(json.indexOf("\"manufacturerPartNumber\":\"TPS7A2033PDBVR\"") >= 0);
  TEST_ASSERT_TRUE(json.indexOf("\"lookup\":{\"lookupId\":\"lookup-9\",\"code\":\"0002\"}") >= 0);

  const auto response = parseSyncResponseJson(
      "{\"protocolVersion\":1,\"requestId\":\"sync-7\",\"acceptedEventIds\":[\"R1-42\"],"
      "\"results\":[{\"resultId\":\"R1-42-result\",\"eventId\":\"R1-42\","
      "\"status\":\"completed\",\"existing\":true,\"itemName\":\"10k resistor\","
      "\"purposeLabel\":\"Resistor\","
      "\"requestedDelta\":-3,\"appliedDelta\":-3,\"quantity\":17,"
      "\"location\":\"R2-B4\",\"code\":\"\",\"message\":\"Quantity updated\"}],"
      "\"lookupResult\":{\"lookupId\":\"lookup-9\",\"status\":\"found\","
      "\"itemName\":\"10k resistor\",\"purposeLabel\":\"Resistor\"}}" );
  TEST_ASSERT_TRUE(response.valid);
  TEST_ASSERT_EQUAL_STRING("sync-7", response.requestId.c_str());
  TEST_ASSERT_EQUAL_UINT32(1, response.acceptedCount);
  TEST_ASSERT_TRUE(response.hasResult);
  TEST_ASSERT_TRUE(response.result.existing);
  TEST_ASSERT_EQUAL_STRING("Resistor", response.result.purposeLabel.c_str());
  TEST_ASSERT_EQUAL_INT(17, response.result.quantity);
  TEST_ASSERT_EQUAL_STRING("R2-B4", response.result.location.c_str());
  TEST_ASSERT_TRUE(response.hasLookupResult);
  TEST_ASSERT_EQUAL_STRING("lookup-9", response.lookupId.c_str());
  TEST_ASSERT_EQUAL_STRING("found", response.lookupStatus.c_str());
  TEST_ASSERT_EQUAL_STRING("10k resistor", response.lookupItemName.c_str());
  TEST_ASSERT_EQUAL_STRING("Resistor", response.lookupPurposeLabel.c_str());
  TEST_ASSERT_TRUE(isCurrentLookupResult(response, "lookup-9"));
  TEST_ASSERT_FALSE(isCurrentLookupResult(response, "lookup-stale"));

  const auto unavailable = parseSyncResponseJson(
      "{\"protocolVersion\":1,\"requestId\":\"sync-8\",\"acceptedEventIds\":[],"
      "\"results\":[],\"lookupResult\":{\"lookupId\":\"lookup-10\","
      "\"status\":\"unavailable\",\"itemName\":\"\"}}");
  TEST_ASSERT_TRUE(unavailable.hasLookupResult);
  TEST_ASSERT_EQUAL_STRING("unavailable", unavailable.lookupStatus.c_str());
}

void test_quick_label_protocol_extensions() {
  QuickLabelPrintRequest print;
  print.requestId = "R1-label-7";
  print.presetIndex = 2;
  print.revision = 4;
  const auto request = buildSyncRequestJson("sync-label", "R1", "0.4.0", "label_print", -40, 0,
                                            nullptr, 0, nullptr, 0, {}, {}, &print);
  TEST_ASSERT_TRUE(request.indexOf("\"quickLabelPrint\":{\"requestId\":\"R1-label-7\"") >= 0);
  const auto response = parseSyncResponseJson(
      "{\"protocolVersion\":1,\"requestId\":\"sync-label\",\"acceptedEventIds\":[],\"results\":[],"
      "\"quickLabels\":{\"revision\":4,\"presets\":[\"5V\",\"12V\"]},"
      "\"quickLabelPrintResult\":{\"requestId\":\"R1-label-7\",\"status\":\"completed\","
      "\"code\":\"\",\"message\":\"Label sent\"}}");
  TEST_ASSERT_TRUE(response.valid);
  TEST_ASSERT_TRUE(response.hasQuickLabels);
  TEST_ASSERT_EQUAL_UINT32(4, response.quickLabelRevision);
  TEST_ASSERT_EQUAL_UINT32(2, response.quickLabelPresetCount);
  TEST_ASSERT_EQUAL_STRING("12V", response.quickLabelPresets[1].c_str());
  TEST_ASSERT_TRUE(response.hasQuickLabelPrintResult);
  TEST_ASSERT_EQUAL_STRING("completed", response.quickLabelPrintStatus.c_str());
}

void test_transport_authentication_vectors() {
  const String token = "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";
  TEST_ASSERT_EQUAL_STRING(
      "d99b3246f156ffc7b1cf9507f98aeb47603e93f103945badc3e95641e5c8f685",
      deviceRequestMac(token, "POST", "/api/v1/device/sync", "r1-test", 42, "{\"protocolVersion\":1}").c_str());
  TEST_ASSERT_EQUAL_STRING("ec5e858f7b38260c65df43b570e13a7dceab39361d4f17ebf3fe8d4523024423",
                           deviceResponseMac(token, 42, 200, "{\"ok\":true}").c_str());
  TEST_ASSERT_FALSE(transportMacMatches(deviceRequestMac(token, "POST", "/api/v1/device/sync", "r1-test", 42,
                                                            "{\"protocolVersion\":1}"),
                                        "0000000000000000000000000000000000000000000000000000000000000000"));
  TEST_ASSERT_TRUE(isValidTransportToken(token));
  TEST_ASSERT_FALSE(isValidTransportToken(token.substring(0, 63)));
  TEST_ASSERT_FALSE(isValidTransportToken(String("z") + token.substring(1)));
}

void test_unknown_result_status_is_rejected() {
  const auto unknown = parseSyncResponseJson(
      "{\"protocolVersion\":1,\"requestId\":\"sync-unknown\",\"acceptedEventIds\":[],"
      "\"results\":[{\"resultId\":\"result-1\",\"eventId\":\"event-1\","
      "\"status\":\"pending\"}]}" );
  TEST_ASSERT_FALSE(unknown.valid);

  const auto unknownPrint = parseSyncResponseJson(
      "{\"protocolVersion\":1,\"requestId\":\"sync-print\",\"acceptedEventIds\":[],\"results\":[],"
      "\"quickLabelPrintResult\":{\"requestId\":\"print-1\",\"status\":\"queued\"}}" );
  TEST_ASSERT_FALSE(unknownPrint.valid);
}

void test_sync_response_json_is_strictly_parsed() {
  const String valid =
      R"json({"protocolVersion":1,"requestId":"sync-9","acceptedEventIds":[],"results":[]})json";
  TEST_ASSERT_FALSE(parseSyncResponseJson(valid + " trailing").valid);

  const auto fieldLikeText = parseSyncResponseJson(
      R"json({"requestId":"sync-evil, \"protocolVersion\":1","protocolVersion":0,"acceptedEventIds":[],"results":[]})json");
  TEST_ASSERT_FALSE(fieldLikeText.valid);

  const auto wrongType = parseSyncResponseJson(
      R"json({"protocolVersion":"1","requestId":"sync-10","acceptedEventIds":[],"results":[]})json");
  TEST_ASSERT_FALSE(wrongType.valid);

  const auto malformedEscape = parseSyncResponseJson(
      R"json({"protocolVersion":1,"requestId":"sync-\q","acceptedEventIds":[],"results":[]})json");
  TEST_ASSERT_FALSE(malformedEscape.valid);

  const auto nonStringAcknowledgement = parseSyncResponseJson(
      R"json({"protocolVersion":1,"requestId":"sync-11","acceptedEventIds":[true],"results":[]})json");
  TEST_ASSERT_FALSE(nonStringAcknowledgement.valid);

  const auto laterUnknownStatus = parseSyncResponseJson(
      R"json({"protocolVersion":1,"requestId":"sync-12","acceptedEventIds":[],"results":[{"resultId":"r1","eventId":"e1","status":"completed"},{"resultId":"r2","eventId":"e2","status":"pending"}]})json");
  TEST_ASSERT_FALSE(laterUnknownStatus.valid);

  // Optional objects serialised as null are treated as absent; other kinds stay invalid.
  const auto nullOptionals = parseSyncResponseJson(
      R"json({"protocolVersion":1,"requestId":"sync-14","acceptedEventIds":[],"results":[],"lookupResult":null,"quickLabels":null,"quickLabelPrintResult":null})json");
  TEST_ASSERT_TRUE(nullOptionals.valid);
  TEST_ASSERT_FALSE(nullOptionals.hasLookupResult);
  TEST_ASSERT_FALSE(nullOptionals.hasQuickLabels);
  TEST_ASSERT_FALSE(nullOptionals.hasQuickLabelPrintResult);
  TEST_ASSERT_FALSE(parseSyncResponseJson(
      R"json({"protocolVersion":1,"requestId":"sync-15","acceptedEventIds":[],"results":[],"lookupResult":"found"})json").valid);
  TEST_ASSERT_FALSE(parseSyncResponseJson(
      R"json({"protocolVersion":1,"requestId":"sync-16","acceptedEventIds":[],"results":[],"quickLabels":[]})json").valid);
  TEST_ASSERT_FALSE(parseSyncResponseJson(
      R"json({"protocolVersion":1,"requestId":"sync-17","acceptedEventIds":[],"results":[],"quickLabelPrintResult":true})json").valid);
  TEST_ASSERT_FALSE(parseSyncResponseJson(
      R"json({"protocolVersion":null,"requestId":"sync-18","acceptedEventIds":[],"results":[]})json").valid);

  const auto malformedNumber = parseSyncResponseJson(
      R"json({"protocolVersion":1,"requestId":"sync-13","acceptedEventIds":[],"results":[{"resultId":"r1","eventId":"e1","status":"completed","quantity":7tail}]})json");
  TEST_ASSERT_FALSE(malformedNumber.valid);

  const auto unicodeEscape = parseSyncResponseJson(
      R"json({"protocolVersion":1,"requestId":"sync-\u0041","acceptedEventIds":[],"results":[]})json");
  TEST_ASSERT_TRUE(unicodeEscape.valid);
  TEST_ASSERT_EQUAL_STRING("sync-A", unicodeEscape.requestId.c_str());

  const String validUtf8 = String(R"json({"protocolVersion":1,"requestId":"sync-)json") +
      static_cast<char>(0xC3) + static_cast<char>(0xBC) + R"json(","acceptedEventIds":[],"results":[]})json";
  TEST_ASSERT_TRUE(parseSyncResponseJson(validUtf8).valid);

  const String malformedUtf8 = String(R"json({"protocolVersion":1,"requestId":"sync-)json") +
      static_cast<char>(0xC0) + static_cast<char>(0xAF) + R"json(","acceptedEventIds":[],"results":[]})json";
  TEST_ASSERT_FALSE(parseSyncResponseJson(malformedUtf8).valid);
}

void test_http_response_limits_are_bounded() {
  // Keep this literal synchronized with the desktop protocol contract in
  // include/InventatoryScanHttp.h. The device must accept every valid bounded
  // desktop response before it begins parsing the authenticated body.
  TEST_ASSERT_EQUAL_UINT32(1024U, kMaxHttpHeaderBytes);
  TEST_ASSERT_EQUAL_UINT32(8192U, kMaxHttpBodyBytes);
  TEST_ASSERT_TRUE(httpHeaderWithinLimit(1024U));
  TEST_ASSERT_FALSE(httpHeaderWithinLimit(1025U));
  TEST_ASSERT_TRUE(httpBodyWithinLimit(kMaxHttpBodyBytes));
  TEST_ASSERT_FALSE(httpBodyWithinLimit(kMaxHttpBodyBytes + 1U));
  TEST_ASSERT_TRUE(httpResponseAppendAllowed(kMaxHttpResponseBytes - 1U));
  TEST_ASSERT_FALSE(httpResponseAppendAllowed(kMaxHttpResponseBytes));
}

void test_ui_header_prefers_full_title_over_pc_status() {
  TEST_ASSERT_TRUE(displayHeaderCanShowStatus(52, 39, 122, 5));
  TEST_ASSERT_FALSE(displayHeaderCanShowStatus(83, 39, 122, 5));
  TEST_ASSERT_FALSE(displayHeaderCanShowStatus(-1, 20, 122, 5));
}

void test_ui_clipping_and_queue_boundaries() {
  const auto monospaceWidth = [](std::size_t length) { return static_cast<int>(length * 5U); };
  // Full fit: 5 chars = 25 px <= 25 px, so the whole string is kept.
  TEST_ASSERT_EQUAL_UINT32(5, displayClippedPrefixLength(5, 25, 3, monospaceWidth));
  // Full fit with room to spare: 3 chars = 15 px <= 100 px.
  TEST_ASSERT_EQUAL_UINT32(3, displayClippedPrefixLength(3, 100, 3, monospaceWidth));
  // Clipped: 5 chars = 25 px > 24 px. 4 chars = 20 px, plus 3 px ellipsis = 23 px <= 24 px.
  TEST_ASSERT_EQUAL_UINT32(4, displayClippedPrefixLength(5, 24, 3, monospaceWidth));
  // Clipped, ellipsis matters: 4 chars = 20 px fits 22 px on its own, but
  // 20 + 3 px ellipsis = 23 px > 22 px, so only 3 chars (15 + 3 = 18 px) remain.
  TEST_ASSERT_EQUAL_UINT32(3, displayClippedPrefixLength(5, 22, 3, monospaceWidth));
  // Nothing fits: 2 px is smaller than even the 3 px ellipsis, so the result is 0.
  TEST_ASSERT_EQUAL_UINT32(0, displayClippedPrefixLength(5, 2, 3, monospaceWidth));
  TEST_ASSERT_FALSE(displayQueueDepth(0));
  TEST_ASSERT_TRUE(displayQueueDepth(1));
}

void test_ui_timing_and_activity_policy() {
  TEST_ASSERT_EQUAL_UINT32(3500, kUiSuccessDwellMs);
  TEST_ASSERT_EQUAL_UINT32(3000, kUiWarningDwellMs);
  TEST_ASSERT_FALSE(uiActivityAlternate(0));
  TEST_ASSERT_FALSE(uiActivityAlternate(599));
  TEST_ASSERT_TRUE(uiActivityAlternate(600));
  TEST_ASSERT_TRUE(uiActivityAlternate(1199));
  TEST_ASSERT_FALSE(uiActivityAlternate(1200));
  TEST_ASSERT_TRUE(uiActivityEnabled(false, true));
  TEST_ASSERT_FALSE(uiActivityEnabled(true, true));
  TEST_ASSERT_FALSE(uiActivityEnabled(false, false));
  TEST_ASSERT_EQUAL_UINT32(250, style::kRailActivityStepMs);
  TEST_ASSERT_EQUAL_INT(1, style::kChipPadX);
  TEST_ASSERT_EQUAL_INT(1, style::kChipTopPadY);
  TEST_ASSERT_EQUAL_INT(1, style::kChipBottomPadY);
  TEST_ASSERT_EQUAL_INT(0, uiRailSweepOffset(0, style::kRailActivityStepMs,
                                              style::kRailActivityTravel,
                                              style::kRailActivityStepsPerLeg));
  TEST_ASSERT_EQUAL_INT(4, uiRailSweepOffset(250, style::kRailActivityStepMs,
                                              style::kRailActivityTravel,
                                              style::kRailActivityStepsPerLeg));
  TEST_ASSERT_EQUAL_INT(style::kRailActivityTravel,
                        uiRailSweepOffset(1500, style::kRailActivityStepMs,
                                          style::kRailActivityTravel,
                                          style::kRailActivityStepsPerLeg));
  TEST_ASSERT_EQUAL_INT(0, uiRailSweepOffset(3000, style::kRailActivityStepMs,
                                              style::kRailActivityTravel,
                                              style::kRailActivityStepsPerLeg));
  TEST_ASSERT_FALSE(uiDeadlineReached(1000, 0));
  TEST_ASSERT_FALSE(uiDeadlineReached(1499, 1500));
  TEST_ASSERT_TRUE(uiDeadlineReached(1500, 1500));
  TEST_ASSERT_TRUE(uiResultDwellMayExpire(true));
  TEST_ASSERT_FALSE(uiResultDwellMayExpire(false));
  TEST_ASSERT_TRUE(uiDeadlineReached(10, 0xFFFFFFF0UL));
}

void test_scan_preemption_policy() {
  TEST_ASSERT_TRUE(scanMayInterruptUi(false, false, false, false, false));
  TEST_ASSERT_FALSE(scanMayInterruptUi(true, false, false, false, false));
  TEST_ASSERT_FALSE(scanMayInterruptUi(false, true, false, false, false));
  TEST_ASSERT_FALSE(scanMayInterruptUi(false, false, true, false, false));
  TEST_ASSERT_FALSE(scanMayInterruptUi(false, false, false, true, false));
  TEST_ASSERT_FALSE(scanMayInterruptUi(false, false, false, false, true));
}

void test_brand_dot_policy() {
  // Busy: one slot travels clockwise TL -> TR -> BR -> BL in 200 ms steps.
  TEST_ASSERT_EQUAL_HEX8(0x01, brandDotBusy(0));
  TEST_ASSERT_EQUAL_HEX8(0x01, brandDotBusy(199));
  TEST_ASSERT_EQUAL_HEX8(0x02, brandDotBusy(200));
  TEST_ASSERT_EQUAL_HEX8(0x08, brandDotBusy(400));
  TEST_ASSERT_EQUAL_HEX8(0x04, brandDotBusy(600));
  TEST_ASSERT_EQUAL_HEX8(0x01, brandDotBusy(800));
  // Progress: one slot per quarter, the next one blinks every 400 ms.
  TEST_ASSERT_EQUAL_HEX8(0x01, brandDotProgress(0, 0));
  TEST_ASSERT_EQUAL_HEX8(0x00, brandDotProgress(0, 400));
  TEST_ASSERT_EQUAL_HEX8(0x03, brandDotProgress(42, 0));
  TEST_ASSERT_EQUAL_HEX8(0x01, brandDotProgress(42, 400));
  TEST_ASSERT_EQUAL_HEX8(0x07, brandDotProgress(50, 0));
  TEST_ASSERT_EQUAL_HEX8(0x03, brandDotProgress(50, 799));
  TEST_ASSERT_EQUAL_HEX8(0x0F, brandDotProgress(99, 0));
  TEST_ASSERT_EQUAL_HEX8(0x07, brandDotProgress(99, 400));
  TEST_ASSERT_EQUAL_HEX8(kBrandDotFull, brandDotProgress(100, 0));
  TEST_ASSERT_EQUAL_HEX8(kBrandDotFull, brandDotProgress(100, 400));
  TEST_ASSERT_EQUAL_HEX8(kBrandDotFull, brandDotProgress(250, 400));
  TEST_ASSERT_EQUAL_HEX8(0x00, brandDotProgress(-5, 400));
  TEST_ASSERT_EQUAL_HEX8(0x09, kBrandDotFault);
  // The update column fits beside the mark on the 128 px screen.
  TEST_ASSERT_TRUE(style::kMarkCols * style::kUpdateMarkUnit + style::kUpdateColumnGap +
                       style::kUpdateColumnW <=
                   style::kScreenW);
  TEST_ASSERT_TRUE(style::kMarkRows * style::kBootMarkUnit < style::kScreenH);
}

void test_soft_power_off_policy() {
  // The warning opens at a 1 s hold and the power-off commits at a 4 s hold: a 3 s countdown.
  TEST_ASSERT_EQUAL_UINT32(1000UL, kPowerOffWarnMs);
  TEST_ASSERT_EQUAL_UINT32(4000UL, kPowerOffCommitMs);
  TEST_ASSERT_EQUAL_UINT32(3000UL, kPowerOffCommitMs - kPowerOffWarnMs);
  // The count reads 3, 2, 1 from the warning.
  TEST_ASSERT_EQUAL_INT(3, powerOffCountdownSeconds(0));
  TEST_ASSERT_EQUAL_INT(3, powerOffCountdownSeconds(999));
  TEST_ASSERT_EQUAL_INT(2, powerOffCountdownSeconds(1000));
  TEST_ASSERT_EQUAL_INT(2, powerOffCountdownSeconds(1999));
  TEST_ASSERT_EQUAL_INT(1, powerOffCountdownSeconds(2000));
  TEST_ASSERT_EQUAL_INT(1, powerOffCountdownSeconds(2999));
  // The bar empties with the count: full at 3, two-thirds at 2, a third at 1.
  TEST_ASSERT_EQUAL_INT(100, powerOffCountdownPercent(3));
  TEST_ASSERT_EQUAL_INT(66, powerOffCountdownPercent(2));
  TEST_ASSERT_EQUAL_INT(33, powerOffCountdownPercent(1));
  TEST_ASSERT_EQUAL_INT(0, powerOffCountdownPercent(0));
  // The sink drops 8 rows a step over six steps, then stays fully behind the glass.
  TEST_ASSERT_EQUAL_INT(6, kPowerOffSinkSteps);
  TEST_ASSERT_EQUAL_UINT32(60UL, kPowerOffSinkStepMs);
  TEST_ASSERT_EQUAL_UINT32(700UL, kPowerOffRestMs);
  TEST_ASSERT_EQUAL_INT(0, powerOffSinkOffset(-1));
  TEST_ASSERT_EQUAL_INT(0, powerOffSinkOffset(0));
  TEST_ASSERT_EQUAL_INT(8, powerOffSinkOffset(1));
  TEST_ASSERT_EQUAL_INT(40, powerOffSinkOffset(5));
  TEST_ASSERT_EQUAL_INT(40, powerOffSinkOffset(9));
}

void test_boot_bloom_policy() {
  TEST_ASSERT_EQUAL_INT(0, bootBloomStage(0));
  TEST_ASSERT_EQUAL_INT(0, bootBloomStage(149));
  TEST_ASSERT_EQUAL_INT(1, bootBloomStage(150));
  TEST_ASSERT_EQUAL_INT(3, bootBloomStage(450));
  TEST_ASSERT_EQUAL_INT(5, bootBloomStage(899));
  TEST_ASSERT_EQUAL_INT(kBootBloomStages, bootBloomStage(900));
  TEST_ASSERT_EQUAL_INT(kBootBloomStages, bootBloomStage(1200));
  // The whole bloom fits inside the 1200 ms boot splash hold (kBootSplashHoldMs in the app).
  TEST_ASSERT_TRUE(kBootBloomStepMs * kBootBloomStages <= 1200UL);
}

void test_header_page_cycle_policy() {
  // Status for 4 s, the battery for 1.5 s, then the estimate for 2 s when it is known.
  TEST_ASSERT_TRUE(headerPageAt(0, true) == HeaderPage::Status);
  TEST_ASSERT_TRUE(headerPageAt(3999, true) == HeaderPage::Status);
  TEST_ASSERT_TRUE(headerPageAt(4000, true) == HeaderPage::Battery);
  TEST_ASSERT_TRUE(headerPageAt(5499, true) == HeaderPage::Battery);
  TEST_ASSERT_TRUE(headerPageAt(5500, true) == HeaderPage::Estimate);
  TEST_ASSERT_TRUE(headerPageAt(7499, true) == HeaderPage::Estimate);
  TEST_ASSERT_TRUE(headerPageAt(7500, true) == HeaderPage::Status);
  // Without an estimate the cycle is status and battery only.
  TEST_ASSERT_TRUE(headerPageAt(5499, false) == HeaderPage::Battery);
  TEST_ASSERT_TRUE(headerPageAt(5500, false) == HeaderPage::Status);
  TEST_ASSERT_TRUE(headerPageAt(9500, false) == HeaderPage::Battery);
}

void test_battery_card_fill_steps() {
  TEST_ASSERT_EQUAL_UINT8(0, batteryCardFillStep(0));
  TEST_ASSERT_EQUAL_UINT8(0, batteryCardFillStep(99));
  TEST_ASSERT_EQUAL_UINT8(1, batteryCardFillStep(100));
  TEST_ASSERT_EQUAL_UINT8(6, batteryCardFillStep(650));
  TEST_ASSERT_EQUAL_UINT8(kBatteryCardFillSteps, batteryCardFillStep(700));
  TEST_ASSERT_EQUAL_UINT8(kBatteryCardFillSteps, batteryCardFillStep(60000));
}

void test_backlight_policy() {
  // Stored values outside the enum fall back to the default, which keeps the screen lit.
  TEST_ASSERT_TRUE(backlightModeFromStored(3) == BacklightMode::Auto3Min);
  TEST_ASSERT_TRUE(backlightModeFromStored(9) == BacklightMode::On);
  TEST_ASSERT_EQUAL_UINT32(60000, backlightIdleTimeoutMs(BacklightMode::Auto1Min));
  TEST_ASSERT_EQUAL_UINT32(600000, backlightIdleTimeoutMs(BacklightMode::Auto10Min));
  TEST_ASSERT_EQUAL_UINT32(0, backlightIdleTimeoutMs(BacklightMode::Off));
  TEST_ASSERT_TRUE(backlightLit(BacklightMode::On, false, 0, 0));
  TEST_ASSERT_FALSE(backlightLit(BacklightMode::Off, false, 0, 0));
  // Warnings and pairing keep the backlight on whatever the setting.
  TEST_ASSERT_TRUE(backlightLit(BacklightMode::Off, true, 0, 0));
  TEST_ASSERT_TRUE(backlightLit(BacklightMode::Auto1Min, true, 900000, 0));
  TEST_ASSERT_TRUE(backlightLit(BacklightMode::Auto3Min, false, 179999, 0));
  TEST_ASSERT_FALSE(backlightLit(BacklightMode::Auto3Min, false, 180000, 0));
  TEST_ASSERT_TRUE(backlightLit(BacklightMode::Auto1Min, false, 1000, 500));
  // Idle time survives the millis() wrap: activity 1000 ms before the wrap, 3000 ms idle.
  TEST_ASSERT_TRUE(backlightLit(BacklightMode::Auto1Min, false, 2000U, 0xFFFFFC18UL));
  TEST_ASSERT_EQUAL_STRING("Auto 3 min", backlightModeLabel(BacklightMode::Auto3Min));
  TEST_ASSERT_EQUAL_STRING("Auto 3m", backlightModeShortLabel(BacklightMode::Auto3Min));
}

void test_battery_curve_policy() {
  TEST_ASSERT_EQUAL_UINT8(100, batteryPercentFromMillivolts(4300));
  TEST_ASSERT_EQUAL_UINT8(100, batteryPercentFromMillivolts(4200));
  TEST_ASSERT_EQUAL_UINT8(90, batteryPercentFromMillivolts(4100));
  // Halfway between 3.7 V (28%) and 3.8 V (45%): 36.5% rounds to 37.
  TEST_ASSERT_EQUAL_UINT8(37, batteryPercentFromMillivolts(3750));
  TEST_ASSERT_EQUAL_UINT8(0, batteryPercentFromMillivolts(3300));
  TEST_ASSERT_EQUAL_UINT8(0, batteryPercentFromMillivolts(2000));
  uint8_t previous = 0;
  for (uint32_t millivolts = 3300; millivolts <= 4300; millivolts += 5) {
    const uint8_t percent = batteryPercentFromMillivolts(millivolts);
    TEST_ASSERT_TRUE(percent >= previous);
    previous = percent;
  }
}

void test_battery_tracker_guards_and_debounce() {
  BatteryTracker tracker;
  TEST_ASSERT_EQUAL_INT(-1, tracker.percent());
  // An unconnected divider reads near zero: unknown, and never a warning.
  tracker.update(0, false, 0);
  TEST_ASSERT_FALSE(tracker.valid());
  TEST_ASSERT_EQUAL_INT(-1, tracker.percent());
  TEST_ASSERT_TRUE(tracker.level() == BatteryLevel::Ok);

  tracker.update(4000, false, 1000);
  TEST_ASSERT_EQUAL_INT(78, tracker.percent());
  // A single dip is smoothed: 4000 mV moves a quarter of the way to 3600 mV, to 3900 mV (62%).
  tracker.update(3600, false, 2000);
  TEST_ASSERT_EQUAL_INT(62, tracker.percent());

  // Low needs four consecutive seconds below 10%; a charging cell never warns.
  BatteryTracker low;
  for (uint32_t second = 0; second < 3; ++second) low.update(3450, false, second * 1000UL);
  TEST_ASSERT_TRUE(low.level() == BatteryLevel::Ok);
  low.update(3450, false, 3000);
  TEST_ASSERT_TRUE(low.level() == BatteryLevel::Low);

  BatteryTracker empty;
  for (uint32_t second = 0; second < 4; ++second) empty.update(3320, false, second * 1000UL);
  TEST_ASSERT_TRUE(empty.level() == BatteryLevel::Critical);

  BatteryTracker docked;
  for (uint32_t second = 0; second < 10; ++second) docked.update(3320, true, second * 1000UL);
  TEST_ASSERT_TRUE(docked.level() == BatteryLevel::Ok);
  TEST_ASSERT_TRUE(docked.charging());

  // A bouncing dock contact: one reading of the other state does not change the state.
  BatteryTracker bounce;
  bounce.update(4000, false, 0);
  bounce.update(4000, true, 1000);
  TEST_ASSERT_FALSE(bounce.charging());
  bounce.update(4000, false, 2000);
  TEST_ASSERT_FALSE(bounce.charging());
  bounce.update(4000, true, 3000);
  bounce.update(4000, true, 4000);
  TEST_ASSERT_TRUE(bounce.charging());
}

void test_battery_tracker_learns_runtime() {
  // A steady discharge from 4.00 V (78%) to 3.90 V (62%) over 20 minutes is about
  // 75 s per percent, so about 77 minutes remain at the end.
  BatteryTracker tracker;
  for (uint32_t second = 0; second <= 1200; ++second) {
    tracker.update(4000U - second / 12U, false, second * 1000UL);
    if (second < 290) TEST_ASSERT_EQUAL_INT32(-1, tracker.minutesRemaining());
  }
  TEST_ASSERT_EQUAL_INT(62, tracker.percent());
  const int32_t remaining = tracker.minutesRemaining();
  TEST_ASSERT_TRUE(remaining >= 60 && remaining <= 95);

  // Charging from 3.90 V (62%) to 4.10 V (90%) over 20 minutes: about 43 s per
  // percent, with 10% left, so roughly 7 minutes to full.
  BatteryTracker charging;
  for (uint32_t second = 0; second <= 1200; ++second) {
    charging.update(3900U + second / 6U, true, second * 1000UL);
  }
  TEST_ASSERT_TRUE(charging.charging());
  const int32_t toFull = charging.minutesRemaining();
  TEST_ASSERT_TRUE(toFull >= 4 && toFull <= 12);
}

void test_battery_estimate_text() {
  char text[32];
  TEST_ASSERT_TRUE(formatBatteryDuration(text, sizeof(text), 42));
  TEST_ASSERT_EQUAL_STRING("42 min", text);
  TEST_ASSERT_TRUE(formatBatteryDuration(text, sizeof(text), 65));
  TEST_ASSERT_EQUAL_STRING("1h 05m", text);
  TEST_ASSERT_FALSE(formatBatteryDuration(text, 4, 42));

  TEST_ASSERT_TRUE(formatBatteryEstimate(text, sizeof(text), 380, false, 60));
  TEST_ASSERT_EQUAL_STRING("6h 20m left", text);
  TEST_ASSERT_TRUE(formatBatteryEstimate(text, sizeof(text), 80, true, 60));
  TEST_ASSERT_EQUAL_STRING("Full: 1h 20m", text);
  TEST_ASSERT_TRUE(formatBatteryEstimate(text, sizeof(text), 0, true, 100));
  TEST_ASSERT_EQUAL_STRING("Fully charged", text);
  // No rate yet: nothing is shown rather than a guess.
  TEST_ASSERT_FALSE(formatBatteryEstimate(text, sizeof(text), -1, false, 50));
  TEST_ASSERT_EQUAL_STRING("", text);
}

// Builds an ISO 15434 DataMatrix whose raw length is exactly totalBytes. The
// fixed framing is 27 bytes: "[)>" RS "06" GS (7), "1PTPS7A2033PDBVR" (16),
// GS (1), "N" (1) and RS EOT (2). Padding goes into the free-text N field.
static String makeSizedComponentDataMatrix(std::size_t totalBytes) {
  String payload = String("[)>") + static_cast<char>(0x1E) + "06" + static_cast<char>(0x1D) +
      "1PTPS7A2033PDBVR" + static_cast<char>(0x1D) + "N";
  const String trailer = String(static_cast<char>(0x1E)) + static_cast<char>(0x04);
  while (payload.length() + trailer.length() < totalBytes) payload += 'X';
  payload += trailer;
  return payload;
}

void test_datamatrix_size_guard_is_exact_at_512_bytes() {
  DecodedComponentFields decoded;
  // 512 bytes: 27 fixed + 485 padding. This is the largest accepted raw payload.
  const String atLimit = makeSizedComponentDataMatrix(512U);
  TEST_ASSERT_EQUAL_UINT32(512U, atLimit.length());
  TEST_ASSERT_TRUE(decodeComponentDataMatrix(atLimit, decoded));
  TEST_ASSERT_EQUAL_STRING("TPS7A2033PDBVR", decoded.manufacturerPartNumber.c_str());
  TEST_ASSERT_EQUAL_UINT32(485U, decoded.encodedPartName.length());
  TEST_ASSERT_EQUAL_INT(1, decoded.quantity);

  // 513 bytes: identical framing with one more padding byte. Only the size guard differs.
  const String overLimit = makeSizedComponentDataMatrix(513U);
  TEST_ASSERT_EQUAL_UINT32(513U, overLimit.length());
  TEST_ASSERT_FALSE(decodeComponentDataMatrix(overLimit, decoded));
}

void test_manufacturer_part_number_length_bounds() {
  String sixtyFour;
  for (int index = 0; index < 64; ++index) sixtyFour += 'A';
  TEST_ASSERT_TRUE(isStandardManufacturerPartNumber(sixtyFour));

  String sixtyFive = sixtyFour;
  sixtyFive += 'A';
  TEST_ASSERT_FALSE(isStandardManufacturerPartNumber(sixtyFive));

  // Allowed punctuation counts toward the 64-character limit.
  String mixed;
  for (int index = 0; index < 63; ++index) mixed += 'A';
  mixed += '#';
  TEST_ASSERT_TRUE(isStandardManufacturerPartNumber(mixed));

  TEST_ASSERT_FALSE(isStandardManufacturerPartNumber("A"));
  TEST_ASSERT_TRUE(isStandardManufacturerPartNumber("A1"));
  // Surrounding whitespace is trimmed before the length check.
  TEST_ASSERT_FALSE(isStandardManufacturerPartNumber(" A "));
  TEST_ASSERT_TRUE(isStandardManufacturerPartNumber(" A1 "));
  // Punctuation alone has no alphanumeric character and is rejected.
  TEST_ASSERT_FALSE(isStandardManufacturerPartNumber("--"));
  TEST_ASSERT_FALSE(isStandardManufacturerPartNumber(""));
}

void test_http_limit_helpers_accept_zero_and_one_below_limit() {
  TEST_ASSERT_TRUE(httpHeaderWithinLimit(0U));
  TEST_ASSERT_TRUE(httpHeaderWithinLimit(kMaxHttpHeaderBytes - 1U));
  TEST_ASSERT_TRUE(httpBodyWithinLimit(0U));
  TEST_ASSERT_TRUE(httpBodyWithinLimit(kMaxHttpBodyBytes - 1U));
  // Total response budget as defined in the header: 1024 + 4 + 8192.
  TEST_ASSERT_EQUAL_UINT32(9220U, kMaxHttpResponseBytes);
  TEST_ASSERT_TRUE(httpResponseAppendAllowed(0U));
  TEST_ASSERT_FALSE(httpResponseAppendAllowed(kMaxHttpResponseBytes + 1U));
}

void test_outbox_pop_keeps_tail_and_sequence_across_wrap() {
  OutboxQueueState next;
  // Entries occupy slots 15, 0 and 1; tail is 2, which matches count and head.
  const auto wrapped = makeOutboxQueueState(15, 3, 2, 7);
  TEST_ASSERT_TRUE(prepareOutboxPop(wrapped, 16, next));
  TEST_ASSERT_EQUAL_UINT32(0, next.head);
  TEST_ASSERT_EQUAL_UINT32(2, next.count);
  TEST_ASSERT_EQUAL_UINT32(2, next.tail);
  TEST_ASSERT_EQUAL_UINT32(7, next.nextSequence);

  // Popping the last entry resets head and tail to zero, even when tail was not zero.
  const auto single = makeOutboxQueueState(7, 1, 8, 5);
  TEST_ASSERT_TRUE(prepareOutboxPop(single, 16, next));
  TEST_ASSERT_EQUAL_UINT32(0, next.head);
  TEST_ASSERT_EQUAL_UINT32(0, next.count);
  TEST_ASSERT_EQUAL_UINT32(0, next.tail);
  TEST_ASSERT_EQUAL_UINT32(5, next.nextSequence);

  // Rejected pops (empty, head out of range, count above capacity, tail out of range)
  // must leave the output untouched.
  next = makeOutboxQueueState(9, 4, 13, 77);
  TEST_ASSERT_FALSE(prepareOutboxPop(makeOutboxQueueState(3, 0, 3, 1), 16, next));
  TEST_ASSERT_FALSE(prepareOutboxPop(makeOutboxQueueState(16, 1, 0, 1), 16, next));
  TEST_ASSERT_FALSE(prepareOutboxPop(makeOutboxQueueState(0, 17, 0, 1), 16, next));
  TEST_ASSERT_FALSE(prepareOutboxPop(makeOutboxQueueState(0, 1, 16, 1), 16, next));
  TEST_ASSERT_EQUAL_UINT32(9, next.head);
  TEST_ASSERT_EQUAL_UINT32(4, next.count);
  TEST_ASSERT_EQUAL_UINT32(13, next.tail);
  TEST_ASSERT_EQUAL_UINT32(77, next.nextSequence);
}

void test_outbox_enqueue_wraps_tail_and_rejects_full_queue() {
  OutboxQueueState next;
  // Empty queue away from slot 0: the write lands at head and tail advances by one.
  TEST_ASSERT_TRUE(prepareOutboxEnqueue(makeOutboxQueueState(5, 0, 5, 1), 16, next));
  TEST_ASSERT_EQUAL_UINT32(5, next.head);
  TEST_ASSERT_EQUAL_UINT32(1, next.count);
  TEST_ASSERT_EQUAL_UINT32(6, next.tail);
  TEST_ASSERT_EQUAL_UINT32(2, next.nextSequence);

  // Filling the last free slot wraps tail from 15 to 0.
  TEST_ASSERT_TRUE(prepareOutboxEnqueue(makeOutboxQueueState(0, 15, 15, 20), 16, next));
  TEST_ASSERT_EQUAL_UINT32(0, next.head);
  TEST_ASSERT_EQUAL_UINT32(16, next.count);
  TEST_ASSERT_EQUAL_UINT32(0, next.tail);
  TEST_ASSERT_EQUAL_UINT32(21, next.nextSequence);

  // A full wrapped queue is rejected, and the output is not written.
  next = makeOutboxQueueState(9, 1, 9, 3);
  TEST_ASSERT_FALSE(prepareOutboxEnqueue(makeOutboxQueueState(15, 16, 15, 5), 16, next));
  TEST_ASSERT_EQUAL_UINT32(9, next.head);
  TEST_ASSERT_EQUAL_UINT32(1, next.count);
  TEST_ASSERT_EQUAL_UINT32(9, next.tail);
  TEST_ASSERT_EQUAL_UINT32(3, next.nextSequence);

  // A wrapped tail that disagrees with head and count is rejected.
  // Head 15 with count 1 implies tail 0, not 15.
  TEST_ASSERT_FALSE(prepareOutboxEnqueue(makeOutboxQueueState(15, 1, 15, 5), 16, next));
  // Zero capacity never accepts an enqueue.
  TEST_ASSERT_FALSE(prepareOutboxEnqueue(makeOutboxQueueState(0, 0, 0, 1), 0, next));

  // The last representable sequence is usable; UINT32_MAX and 0 are refused.
  TEST_ASSERT_TRUE(prepareOutboxEnqueue(makeOutboxQueueState(0, 0, 0, 0xFFFFFFFEU), 16, next));
  TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFU, next.nextSequence);
  TEST_ASSERT_FALSE(prepareOutboxEnqueue(makeOutboxQueueState(0, 0, 0, 0xFFFFFFFFU), 16, next));
  TEST_ASSERT_FALSE(prepareOutboxEnqueue(makeOutboxQueueState(0, 0, 0, 0U), 16, next));
}

void test_outbox_round_trip_across_wrap_keeps_queue_consistent() {
  OutboxQueueState state = makeOutboxQueueState(15, 0, 15, 1);
  OutboxQueueState next;

  // Two enqueues: tail moves 15 -> 0 -> 1 while head stays at 15.
  TEST_ASSERT_TRUE(prepareOutboxEnqueue(state, 16, next));
  state = next;
  TEST_ASSERT_EQUAL_UINT32(15, state.head);
  TEST_ASSERT_EQUAL_UINT32(1, state.count);
  TEST_ASSERT_EQUAL_UINT32(0, state.tail);
  TEST_ASSERT_TRUE(prepareOutboxEnqueue(state, 16, next));
  state = next;
  TEST_ASSERT_EQUAL_UINT32(15, state.head);
  TEST_ASSERT_EQUAL_UINT32(2, state.count);
  TEST_ASSERT_EQUAL_UINT32(1, state.tail);
  TEST_ASSERT_EQUAL_UINT32(3, state.nextSequence);

  // Pop wraps head to 0 while tail stays at 1. The next enqueue still lands at the tail.
  TEST_ASSERT_TRUE(prepareOutboxPop(state, 16, next));
  state = next;
  TEST_ASSERT_EQUAL_UINT32(0, state.head);
  TEST_ASSERT_EQUAL_UINT32(1, state.count);
  TEST_ASSERT_EQUAL_UINT32(1, state.tail);
  TEST_ASSERT_TRUE(prepareOutboxEnqueue(state, 16, next));
  state = next;
  TEST_ASSERT_EQUAL_UINT32(0, state.head);
  TEST_ASSERT_EQUAL_UINT32(2, state.count);
  TEST_ASSERT_EQUAL_UINT32(2, state.tail);
  TEST_ASSERT_EQUAL_UINT32(4, state.nextSequence);

  // Draining: head walks 0 -> 1 -> 2, then resets to zero when the queue is empty.
  TEST_ASSERT_TRUE(prepareOutboxPop(state, 16, next));
  state = next;
  TEST_ASSERT_EQUAL_UINT32(1, state.head);
  TEST_ASSERT_EQUAL_UINT32(1, state.count);
  TEST_ASSERT_EQUAL_UINT32(2, state.tail);
  TEST_ASSERT_TRUE(prepareOutboxPop(state, 16, next));
  state = next;
  TEST_ASSERT_EQUAL_UINT32(0, state.head);
  TEST_ASSERT_EQUAL_UINT32(0, state.count);
  TEST_ASSERT_EQUAL_UINT32(0, state.tail);
  TEST_ASSERT_EQUAL_UINT32(4, state.nextSequence);
  TEST_ASSERT_FALSE(prepareOutboxPop(state, 16, next));
}

void setup() {
  UNITY_BEGIN();
  registerProtocolHardeningTests();
  RUN_TEST(test_standardized_scanner_codes);
  RUN_TEST(test_datamatrix_decoding_fixtures);
  RUN_TEST(test_quantity_composer_defaults_and_limit);
  RUN_TEST(test_quantity_composer_never_submits_a_value_it_does_not_show);
  RUN_TEST(test_quantity_composer_cursor_replaces_and_appends);
  RUN_TEST(test_quantity_composer_replaces_default_and_deletes_digits);
  RUN_TEST(test_quantity_composer_subtract_and_invalid_digits);
  RUN_TEST(test_request_json_serialization);
  RUN_TEST(test_scan_json_escapes_datamatrix_separators);
  RUN_TEST(test_request_id_format);
  RUN_TEST(test_fixed_ring_buffer_fifo_and_overwrite);
  RUN_TEST(test_outbox_pop_transition_is_staged_before_commit);
  RUN_TEST(test_outbox_enqueue_transition_is_bounded);
  RUN_TEST(test_protocol_v1_sync_round_trip);
  RUN_TEST(test_transport_authentication_vectors);
  RUN_TEST(test_quick_label_protocol_extensions);
  RUN_TEST(test_unknown_result_status_is_rejected);
  RUN_TEST(test_sync_response_json_is_strictly_parsed);
  RUN_TEST(test_http_response_limits_are_bounded);
  RUN_TEST(test_ui_header_prefers_full_title_over_pc_status);
  RUN_TEST(test_ui_clipping_and_queue_boundaries);
  RUN_TEST(test_ui_timing_and_activity_policy);
  RUN_TEST(test_scan_preemption_policy);
  RUN_TEST(test_brand_dot_policy);
  RUN_TEST(test_soft_power_off_policy);
  RUN_TEST(test_boot_bloom_policy);
  RUN_TEST(test_header_page_cycle_policy);
  RUN_TEST(test_battery_card_fill_steps);
  RUN_TEST(test_backlight_policy);
  RUN_TEST(test_battery_curve_policy);
  RUN_TEST(test_battery_tracker_guards_and_debounce);
  RUN_TEST(test_battery_tracker_learns_runtime);
  RUN_TEST(test_battery_estimate_text);
  RUN_TEST(test_datamatrix_size_guard_is_exact_at_512_bytes);
  RUN_TEST(test_manufacturer_part_number_length_bounds);
  RUN_TEST(test_http_limit_helpers_accept_zero_and_one_below_limit);
  RUN_TEST(test_outbox_pop_keeps_tail_and_sequence_across_wrap);
  RUN_TEST(test_outbox_enqueue_wraps_tail_and_rejects_full_queue);
  RUN_TEST(test_outbox_round_trip_across_wrap_keeps_queue_consistent);
  UNITY_END();
}

void loop() {}
