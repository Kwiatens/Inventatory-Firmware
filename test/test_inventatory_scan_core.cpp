// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

#include <unity.h>

#include <cstddef>
#include <cstring>

#include "InventatoryScanBattery.h"
#include "InventatoryScanCore.h"
#include "InventatoryScanErrorCodes.h"
#include "InventatoryScanHttp.h"
#include "InventatoryScanOutbox.h"
#include "InventatoryScanSync.h"
#include "InventatoryScanUiPolicy.h"
#include "display/display_style.h"

using namespace inventatory_scan;

void registerProtocolHardeningTests();
void registerSyncResponseCorpusTests();
void registerGm65FrameTests();
void registerBleRequestTests();
void registerBlockedHeadTests();
void registerTextHelperTests();
void registerBatteryGaugeTests();

void test_bag_codes_for_sync() {
  // A DigiKey bag: the envelope, the DigiKey part number and the manufacturer part number, 43 characters.
  const auto digiKey = digiKeyBagCodeForSync("445-15749-1-ND", "MLZ2012M3R3HT000", kMaximumBagEventCodeBytes);
  TEST_ASSERT_EQUAL_UINT(43U, digiKey.length());
  TEST_ASSERT_EQUAL_INT(0, digiKey.indexOf("[)>"));
  TEST_ASSERT_EQUAL_INT(7, digiKey.indexOf("P445-15749-1-ND"));
  TEST_ASSERT_TRUE(digiKey.indexOf("1PMLZ2012M3R3HT000") > 0);
  TEST_ASSERT_EQUAL_INT(0x1E, static_cast<int>(static_cast<unsigned char>(digiKey[3])));
  TEST_ASSERT_EQUAL_INT(0x1D, static_cast<int>(static_cast<unsigned char>(digiKey[6])));
  TEST_ASSERT_EQUAL_INT(0x04, static_cast<int>(static_cast<unsigned char>(digiKey[digiKey.length() - 1U])));

  // Without a manufacturer part number the envelope and the DigiKey part number are still sent.
  const auto bare = digiKeyBagCodeForSync("445-15749-1-ND", "", kMaximumBagEventCodeBytes);
  TEST_ASSERT_EQUAL_UINT(24U, bare.length());
  // A manufacturer part number that does not fit is left out, and the DigiKey part number still identifies the bag.
  TEST_ASSERT_EQUAL_UINT(24U, digiKeyBagCodeForSync("445-15749-1-ND", "MLZ2012M3R3HT000", 30U).length());
  // Nothing is sent when the code would not fit at all.
  TEST_ASSERT_EQUAL_UINT(0U, digiKeyBagCodeForSync("445-15749-1-ND", "MLZ2012M3R3HT000", 10U).length());
  // A part number without the DigiKey -ND ending, or with a character a bag does not print, is not a DigiKey bag.
  TEST_ASSERT_EQUAL_UINT(0U, digiKeyBagCodeForSync("445-15749-1", "MLZ2012M3R3HT000", kMaximumBagEventCodeBytes).length());
  TEST_ASSERT_EQUAL_UINT(0U, digiKeyBagCodeForSync("445 15749-1-ND", "", kMaximumBagEventCodeBytes).length());
  TEST_ASSERT_EQUAL_UINT(0U, digiKeyBagCodeForSync("", "MLZ2012M3R3HT000", kMaximumBagEventCodeBytes).length());

  // A TME bag's QR text: the symbol, the manufacturer part number and the quantity.
  const String tmeText =
      "QTY:5 PN:0ZCM0010FF2G PO:34115735/9 MFR:BELFUSE MPN:0ZCM0010FF2G CoO:TW RoHS https://www.tme.eu/details/0ZCM0010FF2G";
  TmeBagLabel label;
  TEST_ASSERT_TRUE(parseTmeBagLabel(tmeText, label));
  TEST_ASSERT_TRUE(label.symbol == "0ZCM0010FF2G");
  TEST_ASSERT_TRUE(label.manufacturerPartNumber == "0ZCM0010FF2G");
  TEST_ASSERT_EQUAL_INT(5, label.quantity);
  const auto tmeCode = tmeBagCodeForSync(label, kMaximumBagEventCodeBytes);
  TEST_ASSERT_TRUE(tmeCode == "PN:0ZCM0010FF2G MPN:0ZCM0010FF2G");

  // The product page is optional, as the desktop reads it. The symbol and the manufacturer part number are needed.
  TmeBagLabel rejected;
  TEST_ASSERT_TRUE(parseTmeBagLabel("QTY:5 PN:0ZCM0010FF2G MPN:0ZCM0010FF2G", rejected));
  TEST_ASSERT_TRUE(rejected.symbol == "0ZCM0010FF2G");
  TEST_ASSERT_FALSE(parseTmeBagLabel("PN:0ZCM0010FF2G https://www.tme.eu/details/0ZCM0010FF2G", rejected));
  // A DigiKey bag's code is not a TME label.
  TEST_ASSERT_FALSE(parseTmeBagLabel(digiKey, rejected));
}

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

void test_digikey_bag_without_aim_prefix_is_a_vendor_bag() {
  // The GM65 sends a DigiKey bag's Data Matrix with no AIM prefix. The frame ends at its RS EOT trailer, and the
  // parser reads the DigiKey part number from P and the manufacturer part number from 1P.
  const String bag = String("[)>") + static_cast<char>(0x1E) + "06" + static_cast<char>(0x1D) +
      "PIRLML6344TRPBFCT-ND" + static_cast<char>(0x1D) + "1PIRLML6344TRPBF" + static_cast<char>(0x1D) +
      "30PIRLML6344TRPBFCT-ND" + static_cast<char>(0x1D) + "K" + static_cast<char>(0x1D) + "1K92776734" +
      static_cast<char>(0x1D) + "Q3" + static_cast<char>(0x1D) + "20Z0000" + static_cast<char>(0x1E);
  DecodedComponentFields decoded;
  TEST_ASSERT_TRUE(decodeComponentDataMatrix(bag, decoded));
  TEST_ASSERT_EQUAL_STRING("IRLML6344TRPBFCT-ND", decoded.auxiliaryPartNumber.c_str());
  TEST_ASSERT_EQUAL_STRING("IRLML6344TRPBF", decoded.manufacturerPartNumber.c_str());
  TEST_ASSERT_EQUAL_INT(3, decoded.quantity);

  const String expected = String("[)>") + static_cast<char>(0x1E) + "06" + static_cast<char>(0x1D) +
      "PIRLML6344TRPBFCT-ND" + static_cast<char>(0x1D) + "1PIRLML6344TRPBF" + static_cast<char>(0x1E) +
      static_cast<char>(0x04);
  const auto code = digiKeyBagCodeForSync(decoded.auxiliaryPartNumber, decoded.manufacturerPartNumber,
                                          kMaximumBagEventCodeBytes);
  TEST_ASSERT_TRUE(code == expected);
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

void test_json_escapes_datamatrix_separators() {
  const String code = String("[)>") + static_cast<char>(0x1E) + "06" + static_cast<char>(0x1D) +
                      "1PTPS7A2033PDBVR" + static_cast<char>(0x1D) + "Q2" + static_cast<char>(0x1E) +
                      static_cast<char>(0x04);
  TEST_ASSERT_EQUAL_STRING("[)>\\u001e06\\u001d1PTPS7A2033PDBVR\\u001dQ2\\u001e\\u0004", escapeJson(code).c_str());
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

  static const auto unavailable = parseSyncResponseJson(
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

// A parsed response holds about thirty String fields. The device runs each test on the 8 KiB
// Arduino loop stack. A test that kept many responses as locals overflowed it, and the board
// reset at that test. Validity checks therefore parse inside this helper, so only one response
// is live in the test's own frame.
static bool syncResponseIsValid(const String& body) {
  return parseSyncResponseJson(body).valid;
}

void test_sync_response_json_is_strictly_parsed() {
  const String valid =
      R"json({"protocolVersion":1,"requestId":"sync-9","acceptedEventIds":[],"results":[]})json";
  TEST_ASSERT_FALSE(syncResponseIsValid(valid + " trailing"));

  TEST_ASSERT_FALSE(syncResponseIsValid(
      R"json({"requestId":"sync-evil, \"protocolVersion\":1","protocolVersion":0,"acceptedEventIds":[],"results":[]})json"));

  TEST_ASSERT_FALSE(syncResponseIsValid(
      R"json({"protocolVersion":"1","requestId":"sync-10","acceptedEventIds":[],"results":[]})json"));

  TEST_ASSERT_FALSE(syncResponseIsValid(
      R"json({"protocolVersion":1,"requestId":"sync-\q","acceptedEventIds":[],"results":[]})json"));

  TEST_ASSERT_FALSE(syncResponseIsValid(
      R"json({"protocolVersion":1,"requestId":"sync-11","acceptedEventIds":[true],"results":[]})json"));

  TEST_ASSERT_FALSE(syncResponseIsValid(
      R"json({"protocolVersion":1,"requestId":"sync-12","acceptedEventIds":[],"results":[{"resultId":"r1","eventId":"e1","status":"completed"},{"resultId":"r2","eventId":"e2","status":"pending"}]})json"));

  // Optional objects serialised as null are treated as absent; other kinds stay invalid.
  const auto nullOptionals = parseSyncResponseJson(
      R"json({"protocolVersion":1,"requestId":"sync-14","acceptedEventIds":[],"results":[],"lookupResult":null,"quickLabels":null,"quickLabelPrintResult":null})json");
  TEST_ASSERT_TRUE(nullOptionals.valid);
  TEST_ASSERT_FALSE(nullOptionals.hasLookupResult);
  TEST_ASSERT_FALSE(nullOptionals.hasQuickLabels);
  TEST_ASSERT_FALSE(nullOptionals.hasQuickLabelPrintResult);
  TEST_ASSERT_FALSE(syncResponseIsValid(
      R"json({"protocolVersion":1,"requestId":"sync-15","acceptedEventIds":[],"results":[],"lookupResult":"found"})json"));
  TEST_ASSERT_FALSE(syncResponseIsValid(
      R"json({"protocolVersion":1,"requestId":"sync-16","acceptedEventIds":[],"results":[],"quickLabels":[]})json"));
  TEST_ASSERT_FALSE(syncResponseIsValid(
      R"json({"protocolVersion":1,"requestId":"sync-17","acceptedEventIds":[],"results":[],"quickLabelPrintResult":true})json"));
  TEST_ASSERT_FALSE(syncResponseIsValid(
      R"json({"protocolVersion":null,"requestId":"sync-18","acceptedEventIds":[],"results":[]})json"));

  TEST_ASSERT_FALSE(syncResponseIsValid(
      R"json({"protocolVersion":1,"requestId":"sync-13","acceptedEventIds":[],"results":[{"resultId":"r1","eventId":"e1","status":"completed","quantity":7tail}]})json"));

  static const auto unicodeEscape = parseSyncResponseJson(
      R"json({"protocolVersion":1,"requestId":"sync-\u0041","acceptedEventIds":[],"results":[]})json");
  TEST_ASSERT_TRUE(unicodeEscape.valid);
  TEST_ASSERT_EQUAL_STRING("sync-A", unicodeEscape.requestId.c_str());

  const String validUtf8 = String(R"json({"protocolVersion":1,"requestId":"sync-)json") +
      static_cast<char>(0xC3) + static_cast<char>(0xBC) + R"json(","acceptedEventIds":[],"results":[]})json";
  TEST_ASSERT_TRUE(syncResponseIsValid(validUtf8));

  const String malformedUtf8 = String(R"json({"protocolVersion":1,"requestId":"sync-)json") +
      static_cast<char>(0xC0) + static_cast<char>(0xAF) + R"json(","acceptedEventIds":[],"results":[]})json";
  TEST_ASSERT_FALSE(syncResponseIsValid(malformedUtf8));
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

void test_scanner_fault_warning_policy() {
  // A scanner that is not running is a fault, except in standby, where it is off on purpose.
  TEST_ASSERT_TRUE(scannerFaultShown(false, false));
  TEST_ASSERT_FALSE(scannerFaultShown(false, true));
  TEST_ASSERT_FALSE(scannerFaultShown(true, false));
  TEST_ASSERT_FALSE(scannerFaultShown(true, true));
  TEST_ASSERT_EQUAL_STRING("SCN-01", kScannerFaultCode);
}

void test_error_code_catalogue_is_well_formed_and_unique() {
  // Every code is three capital letters, a dash and two digits, and no two codes are the same.
  // Inside each area the numbers run 01, 02, ... with no gap, in the order listed here.
  const char* const codes[] = {
      kScannerFaultCode,         kScanRejectedCode,         kQueueFullCode,           kScanNotSavedCode,
      kQueueUnreadableCode,      kRepairFailedCode,         kDiscardFailedCode,       kSyncBlockedCode,
      kPairingLockedCode,        kSetupNotSavedCode,        kWifiNotJoinedCode,       kPairingClearFailedCode,
      kNetworkStartFailedCode,   kBluetoothStartFailedCode, kUpdateFailedCode,        kNoUpdateCode,
      kPrintNoReplyCode,         kPrintPcOfflineCode,       kPresetsNotLoadedCode,    kBatteryUnreadableCode,
  };
  const std::size_t count = sizeof(codes) / sizeof(codes[0]);
  TEST_ASSERT_EQUAL_UINT(20U, count);
  int previousNumber = 0;
  for (std::size_t index = 0; index < count; ++index) {
    const char* code = codes[index];
    TEST_ASSERT_EQUAL_UINT(6U, std::strlen(code));
    TEST_ASSERT_TRUE(code[0] >= 'A' && code[0] <= 'Z');
    TEST_ASSERT_TRUE(code[1] >= 'A' && code[1] <= 'Z');
    TEST_ASSERT_TRUE(code[2] >= 'A' && code[2] <= 'Z');
    TEST_ASSERT_EQUAL_CHAR('-', code[3]);
    TEST_ASSERT_TRUE(code[4] >= '0' && code[4] <= '9');
    TEST_ASSERT_TRUE(code[5] >= '0' && code[5] <= '9');
    const bool sameArea = index > 0 && std::strncmp(code, codes[index - 1], 3) == 0;
    const int number = (code[4] - '0') * 10 + (code[5] - '0');
    TEST_ASSERT_EQUAL_INT(sameArea ? previousNumber + 1 : 1, number);
    previousNumber = number;
    for (std::size_t other = index + 1; other < count; ++other) {
      TEST_ASSERT_TRUE(std::strcmp(code, codes[other]) != 0);
    }
  }
}

void test_fault_rotation_takes_turns() {
  // One problem, or none, never moves.
  TEST_ASSERT_EQUAL_UINT(0U, faultRotationIndex(0, 0, kFaultRotateMs));
  TEST_ASSERT_EQUAL_UINT(0U, faultRotationIndex(10UL * kFaultRotateMs, 1, kFaultRotateMs));
  // Several problems take turns, each for kFaultRotateMs, and then the first comes round again.
  TEST_ASSERT_EQUAL_UINT(0U, faultRotationIndex(0, 3, kFaultRotateMs));
  TEST_ASSERT_EQUAL_UINT(0U, faultRotationIndex(kFaultRotateMs - 1, 3, kFaultRotateMs));
  TEST_ASSERT_EQUAL_UINT(1U, faultRotationIndex(kFaultRotateMs, 3, kFaultRotateMs));
  TEST_ASSERT_EQUAL_UINT(1U, faultRotationIndex(2UL * kFaultRotateMs - 1, 3, kFaultRotateMs));
  TEST_ASSERT_EQUAL_UINT(2U, faultRotationIndex(2UL * kFaultRotateMs, 3, kFaultRotateMs));
  TEST_ASSERT_EQUAL_UINT(0U, faultRotationIndex(3UL * kFaultRotateMs, 3, kFaultRotateMs));
  TEST_ASSERT_EQUAL_UINT(1U, faultRotationIndex(kFaultRotateMs, 2, kFaultRotateMs));
  TEST_ASSERT_EQUAL_UINT(0U, faultRotationIndex(2UL * kFaultRotateMs, 2, kFaultRotateMs));
}

void test_mark_layout() {
  // The update column fits beside the mark on the 128 px screen.
  TEST_ASSERT_TRUE(style::kMarkCols * style::kUpdateMarkUnit + style::kUpdateColumnGap +
                       style::kUpdateColumnW <=
                   style::kScreenW);
  TEST_ASSERT_TRUE(style::kMarkBodyRows * style::kBootMarkUnit < style::kScreenH);
}

void test_soft_power_off_policy() {
  // The warning opens at a 1 s hold and the power-off commits at a 4 s hold: a 3 s countdown.
  TEST_ASSERT_EQUAL_UINT32(1000UL, kPowerOffWarnMs);
  TEST_ASSERT_EQUAL_UINT32(4000UL, kPowerOffCommitMs);
  TEST_ASSERT_EQUAL_UINT32(3000UL, kPowerOffCommitMs - kPowerOffWarnMs);
  // The bar drains across the countdown: full at the warning, empty at the commit. The inside of
  // the bar is 74 px wide.
  TEST_ASSERT_EQUAL_INT(74, powerOffBarFillPx(0, 74));
  TEST_ASSERT_EQUAL_INT(37, powerOffBarFillPx(1500, 74));
  TEST_ASSERT_EQUAL_INT(0, powerOffBarFillPx(3000, 74));
  TEST_ASSERT_EQUAL_INT(0, powerOffBarFillPx(5000, 74));
  // It only shrinks, a pixel at a time at most, and never goes below zero.
  int previous = powerOffBarFillPx(0, 74);
  for (unsigned long t = 10UL; t <= 3500UL; t += 10UL) {
    const int fill = powerOffBarFillPx(t, 74);
    TEST_ASSERT_TRUE(fill <= previous && previous - fill <= 1 && fill >= 0);
    previous = fill;
  }
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
  // A value outside the enum reads as the default, as the stored-value helpers do.
  TEST_ASSERT_EQUAL_STRING("On", backlightModeLabel(static_cast<BacklightMode>(99)));
  TEST_ASSERT_EQUAL_STRING("30 min", powerOffDelayLabel(static_cast<PowerOffDelay>(99)));
  TEST_ASSERT_EQUAL_STRING("Off after 5 min idle", backlightModeDetail(BacklightMode::Auto5Min));
}

static EnergyInputs energyCase(bool enabled, bool powerOffSet, bool idleReady, bool updating, uint32_t sleepLeftMs,
                               uint32_t offLeftMs) {
  EnergyInputs in{enabled, powerOffSet, idleReady, updating, sleepLeftMs, offLeftMs};
  return in;
}

void test_energy_saving_policy() {
  // Stored values outside the enums fall back to the defaults (Min5 and Min30).
  TEST_ASSERT_TRUE(sleepDelayFromStored(0) == SleepDelay::Min1);
  TEST_ASSERT_TRUE(sleepDelayFromStored(4) == SleepDelay::Min30);
  TEST_ASSERT_TRUE(sleepDelayFromStored(5) == SleepDelay::Min5);
  TEST_ASSERT_TRUE(powerOffDelayFromStored(5) == PowerOffDelay::Never);
  TEST_ASSERT_TRUE(powerOffDelayFromStored(6) == PowerOffDelay::Min30);
  TEST_ASSERT_EQUAL_UINT32(60000, sleepDelayMs(SleepDelay::Min1));
  TEST_ASSERT_EQUAL_UINT32(3600000, powerOffDelayMs(PowerOffDelay::Hour1));
  TEST_ASSERT_FALSE(powerOffEnabled(PowerOffDelay::Never));
  TEST_ASSERT_TRUE(powerOffEnabled(PowerOffDelay::Min1));

  // A countdown runs down from its span to zero, and the unsigned difference survives the millis() wrap.
  TEST_ASSERT_EQUAL_UINT32(60000, energyRemainingMs(0, 0, 60000));
  TEST_ASSERT_EQUAL_UINT32(30000, energyRemainingMs(30000, 0, 60000));
  TEST_ASSERT_EQUAL_UINT32(0, energyRemainingMs(60000, 0, 60000));
  TEST_ASSERT_EQUAL_UINT32(0, energyRemainingMs(70000, 0, 60000));
  // Started 1000 ms before the wrap: 58000 ms later 1000 ms are left, 59000 ms later none.
  TEST_ASSERT_EQUAL_UINT32(1000, energyRemainingMs(58000, 0xFFFFFC18UL, 60000));
  TEST_ASSERT_EQUAL_UINT32(0, energyRemainingMs(59000, 0xFFFFFC18UL, 60000));

  // The warning appears with 30 s left, and the countdown reads 30 down to 1 in whole seconds.
  TEST_ASSERT_FALSE(energyWarningDue(30001));
  TEST_ASSERT_TRUE(energyWarningDue(30000));
  TEST_ASSERT_TRUE(energyWarningDue(0));
  TEST_ASSERT_EQUAL_UINT32(30, energyCountdownSeconds(30000));
  TEST_ASSERT_EQUAL_UINT32(30, energyCountdownSeconds(29001));
  TEST_ASSERT_EQUAL_UINT32(29, energyCountdownSeconds(29000));
  TEST_ASSERT_EQUAL_UINT32(1, energyCountdownSeconds(1));
  TEST_ASSERT_EQUAL_UINT32(0, energyCountdownSeconds(0));
  TEST_ASSERT_EQUAL_INT(100, energyCountdownPercent(30000));
  TEST_ASSERT_EQUAL_INT(50, energyCountdownPercent(15000));
  TEST_ASSERT_EQUAL_INT(0, energyCountdownPercent(0));

  // Awake: nothing until the sleep warning is due, then sleep when the time runs out.
  TEST_ASSERT_TRUE(energyNextAction(EnergyPhase::Awake, energyCase(true, true, true, false, 31000, 0)) ==
                   EnergyAction::None);
  TEST_ASSERT_TRUE(energyNextAction(EnergyPhase::Awake, energyCase(true, true, true, false, 30000, 0)) ==
                   EnergyAction::StartSleepWarning);
  TEST_ASSERT_TRUE(energyNextAction(EnergyPhase::Awake, energyCase(true, true, true, false, 0, 0)) ==
                   EnergyAction::Sleep);
  // Off, not ready (a menu, error, warning or manual standby) or updating: the device stays awake.
  TEST_ASSERT_TRUE(energyNextAction(EnergyPhase::Awake, energyCase(false, true, true, false, 0, 0)) ==
                   EnergyAction::None);
  TEST_ASSERT_TRUE(energyNextAction(EnergyPhase::Awake, energyCase(true, true, false, false, 0, 0)) ==
                   EnergyAction::None);
  TEST_ASSERT_TRUE(energyNextAction(EnergyPhase::Awake, energyCase(true, true, true, true, 0, 0)) ==
                   EnergyAction::None);

  // A sleep warning that can no longer run is dropped, and otherwise it sleeps when its time is up.
  TEST_ASSERT_TRUE(energyNextAction(EnergyPhase::SleepWarning, energyCase(true, true, false, false, 10000, 0)) ==
                   EnergyAction::CancelWarning);
  TEST_ASSERT_TRUE(energyNextAction(EnergyPhase::SleepWarning, energyCase(true, true, true, true, 10000, 0)) ==
                   EnergyAction::CancelWarning);
  TEST_ASSERT_TRUE(energyNextAction(EnergyPhase::SleepWarning, energyCase(true, true, true, false, 10000, 0)) ==
                   EnergyAction::None);
  TEST_ASSERT_TRUE(energyNextAction(EnergyPhase::SleepWarning, energyCase(true, true, true, false, 0, 0)) ==
                   EnergyAction::Sleep);

  // Asleep: the power-off warning comes 30 s before the power-off. Never keeps it asleep.
  TEST_ASSERT_TRUE(energyNextAction(EnergyPhase::Asleep, energyCase(true, true, true, false, 0, 60000)) ==
                   EnergyAction::None);
  TEST_ASSERT_TRUE(energyNextAction(EnergyPhase::Asleep, energyCase(true, true, true, false, 0, 30000)) ==
                   EnergyAction::StartOffWarning);
  TEST_ASSERT_TRUE(energyNextAction(EnergyPhase::Asleep, energyCase(true, true, true, false, 0, 0)) ==
                   EnergyAction::StartOffWarning);
  TEST_ASSERT_TRUE(energyNextAction(EnergyPhase::Asleep, energyCase(true, false, true, false, 0, 0)) ==
                   EnergyAction::None);
  TEST_ASSERT_TRUE(energyNextAction(EnergyPhase::Asleep, energyCase(true, true, true, true, 0, 0)) ==
                   EnergyAction::None);
  TEST_ASSERT_TRUE(energyNextAction(EnergyPhase::Asleep, energyCase(false, true, true, false, 0, 0)) ==
                   EnergyAction::None);

  // The power-off warning powers off when its time is up, and is dropped if the update or the setting changes.
  TEST_ASSERT_TRUE(energyNextAction(EnergyPhase::OffWarning, energyCase(true, true, true, false, 0, 5000)) ==
                   EnergyAction::None);
  TEST_ASSERT_TRUE(energyNextAction(EnergyPhase::OffWarning, energyCase(true, true, true, false, 0, 0)) ==
                   EnergyAction::PowerOff);
  TEST_ASSERT_TRUE(energyNextAction(EnergyPhase::OffWarning, energyCase(true, true, true, true, 0, 5000)) ==
                   EnergyAction::CancelWarning);
  TEST_ASSERT_TRUE(energyNextAction(EnergyPhase::OffWarning, energyCase(false, true, true, false, 0, 0)) ==
                   EnergyAction::CancelWarning);

  // A full run from a key press at t = 0 with 1 min sleep and 1 min power off, nothing else happening.
  // The warnings come 30 s before each step: sleep at 60 s, power off 60 s after sleeping.
  EnergyPhase phase = EnergyPhase::Awake;
  uint32_t warnSleepAt = 0;
  uint32_t sleptAt = 0;
  uint32_t warnOffAt = 0;
  uint32_t offAt = 0;
  for (uint32_t now = 1000; now <= 200000 && offAt == 0; now += 1000) {
    const uint32_t sleepLeft = energyRemainingMs(now, 0, sleepDelayMs(SleepDelay::Min1));
    const uint32_t offLeft = energyRemainingMs(now, sleptAt, powerOffDelayMs(PowerOffDelay::Min1));
    switch (energyNextAction(phase, energyCase(true, true, true, false, sleepLeft, offLeft))) {
      case EnergyAction::None:
        break;
      case EnergyAction::StartSleepWarning:
        phase = EnergyPhase::SleepWarning;
        warnSleepAt = now;
        break;
      case EnergyAction::Sleep:
        phase = EnergyPhase::Asleep;
        sleptAt = now;
        break;
      case EnergyAction::StartOffWarning:
        phase = EnergyPhase::OffWarning;
        warnOffAt = now;
        break;
      case EnergyAction::PowerOff:
        offAt = now;
        break;
      case EnergyAction::CancelWarning:
        phase = EnergyPhase::Awake;
        break;
    }
  }
  TEST_ASSERT_EQUAL_UINT32(30000, warnSleepAt);
  TEST_ASSERT_EQUAL_UINT32(60000, sleptAt);
  TEST_ASSERT_EQUAL_UINT32(90000, warnOffAt);
  TEST_ASSERT_EQUAL_UINT32(120000, offAt);

  TEST_ASSERT_EQUAL_STRING("10 min", sleepDelayLabel(SleepDelay::Min10));
  TEST_ASSERT_EQUAL_STRING("Never", powerOffDelayShortLabel(PowerOffDelay::Never));
  TEST_ASSERT_EQUAL_STRING("1h", powerOffDelayShortLabel(PowerOffDelay::Hour1));
  TEST_ASSERT_EQUAL_STRING("Stays asleep", powerOffDelayDetail(PowerOffDelay::Never));
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
  // A dip is not a change of charge: a sag from the load never moves the percent (see test_battery_gauge.cpp).
  tracker.update(3600, false, 2000);
  TEST_ASSERT_EQUAL_INT(78, tracker.percent());

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

void test_battery_unreadable_needs_four_bad_readings_in_a_row() {
  // A reading above the range is a wrong sense, the same as a missing divider.
  BatteryTracker high;
  high.update(kBatteryMaximumValidMv + 1U, false, 0);
  TEST_ASSERT_FALSE(high.valid());
  TEST_ASSERT_EQUAL_INT(-1, high.percent());
  // The top of the range is still a cell.
  BatteryTracker full;
  full.update(kBatteryMaximumValidMv, false, 0);
  TEST_ASSERT_TRUE(full.valid());

  // Unreadable shows only after kBatteryTripSamples bad readings in a row.
  BatteryTracker tracker;
  for (uint32_t second = 0; second < kBatteryTripSamples - 1U; ++second) {
    tracker.update(0, false, second * 1000UL);
  }
  TEST_ASSERT_FALSE(tracker.unreadable());
  tracker.update(0, false, 3000);
  TEST_ASSERT_TRUE(tracker.unreadable());

  // One good reading clears it at once.
  tracker.update(3900, false, 4000);
  TEST_ASSERT_FALSE(tracker.unreadable());

  // A single bad sample between good ones never counts.
  BatteryTracker blip;
  blip.update(3900, false, 0);
  blip.update(0, false, 1000);
  blip.update(3900, false, 2000);
  blip.update(0, false, 3000);
  TEST_ASSERT_FALSE(blip.unreadable());
}

void test_charge_state_from_status_leds() {
  TEST_ASSERT_TRUE(chargeStateFromLeds(false, false) == ChargeState::Unplugged);
  TEST_ASSERT_TRUE(chargeStateFromLeds(true, false) == ChargeState::Charging);
  TEST_ASSERT_TRUE(chargeStateFromLeds(false, true) == ChargeState::Done);
  // Both lit is a wiring fault; the red LED wins, so the device keeps charging.
  TEST_ASSERT_TRUE(chargeStateFromLeds(true, true) == ChargeState::Charging);
}

void test_battery_tracker_debounces_charge_done() {
  BatteryTracker tracker;
  tracker.update(4000, true, 0);
  TEST_ASSERT_FALSE(tracker.chargeDone());
  // One sample of the done LED is a flicker, not the end of charging.
  tracker.update(4000, true, 1000, true);
  TEST_ASSERT_FALSE(tracker.chargeDone());
  tracker.update(4000, true, 2000, false);
  tracker.update(4000, true, 3000, true);
  TEST_ASSERT_FALSE(tracker.chargeDone());
  tracker.update(4000, true, 4000, true);
  TEST_ASSERT_TRUE(tracker.chargeDone());
  // It reads done only while charging.
  tracker.update(4000, false, 5000, true);
  tracker.update(4000, false, 6000, true);
  TEST_ASSERT_FALSE(tracker.chargeDone());
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
  // From the typical rate: marked with "~".
  TEST_ASSERT_TRUE(formatBatteryEstimate(text, sizeof(text), 380, false, 60, true));
  TEST_ASSERT_EQUAL_STRING("~6h 20m left", text);
  TEST_ASSERT_TRUE(formatBatteryEstimate(text, sizeof(text), 80, true, 60, true));
  TEST_ASSERT_EQUAL_STRING("Full: ~1h 20m", text);
  // No reading: nothing is shown.
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
  registerSyncResponseCorpusTests();
  registerGm65FrameTests();
  registerBleRequestTests();
  registerBlockedHeadTests();
  registerTextHelperTests();
  registerBatteryGaugeTests();
  RUN_TEST(test_standardized_scanner_codes);
  RUN_TEST(test_bag_codes_for_sync);
  RUN_TEST(test_datamatrix_decoding_fixtures);
  RUN_TEST(test_digikey_bag_without_aim_prefix_is_a_vendor_bag);
  RUN_TEST(test_quantity_composer_defaults_and_limit);
  RUN_TEST(test_quantity_composer_never_submits_a_value_it_does_not_show);
  RUN_TEST(test_quantity_composer_cursor_replaces_and_appends);
  RUN_TEST(test_quantity_composer_replaces_default_and_deletes_digits);
  RUN_TEST(test_quantity_composer_subtract_and_invalid_digits);
  RUN_TEST(test_json_escapes_datamatrix_separators);
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
  RUN_TEST(test_scanner_fault_warning_policy);
  RUN_TEST(test_error_code_catalogue_is_well_formed_and_unique);
  RUN_TEST(test_fault_rotation_takes_turns);
  RUN_TEST(test_mark_layout);
  RUN_TEST(test_soft_power_off_policy);
  RUN_TEST(test_boot_bloom_policy);
  RUN_TEST(test_header_page_cycle_policy);
  RUN_TEST(test_battery_card_fill_steps);
  RUN_TEST(test_backlight_policy);
  RUN_TEST(test_energy_saving_policy);
  RUN_TEST(test_battery_curve_policy);
  RUN_TEST(test_battery_tracker_guards_and_debounce);
  RUN_TEST(test_battery_unreadable_needs_four_bad_readings_in_a_row);
  RUN_TEST(test_charge_state_from_status_leds);
  RUN_TEST(test_battery_tracker_debounces_charge_done);
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
