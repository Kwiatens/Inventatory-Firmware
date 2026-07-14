#include <unity.h>

#include "InventatoryScanCore.h"
#include "InventatoryScanOutbox.h"
#include "InventatoryScanSync.h"
#include "InventatoryScanUiPolicy.h"
#include "display/display_style.h"

using namespace inventatory_scan;

void test_standardized_scanner_codes() {
  TEST_ASSERT_TRUE(isStandardMachineCode("0002"));
  TEST_ASSERT_FALSE(isStandardMachineCode("31"));
  TEST_ASSERT_FALSE(isStandardMachineCode("12A4"));
  TEST_ASSERT_TRUE(isStandardSupplierPartCode("718-2362-1-ND"));
  TEST_ASSERT_FALSE(isStandardSupplierPartCode("boot garbage 123"));
  TEST_ASSERT_FALSE(isStandardSupplierPartCode("718-2362-1"));
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
  TEST_ASSERT_EQUAL_STRING("9257", composer.displayText().c_str());
  TEST_ASSERT_EQUAL_UINT32(1, composer.cursor());
  TEST_ASSERT_EQUAL_INT(9257, composer.valueOrOne());

  TEST_ASSERT_EQUAL_INT(9257, composer.consume(true));
  TEST_ASSERT_TRUE(composer.empty());
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
                      "P718-2362-1-ND" + static_cast<char>(0x1D) + "Q2" + static_cast<char>(0x1E) +
                      static_cast<char>(0x04);
  const auto json = buildScanRequestJson("R1", "scan-1", code, 2);
  TEST_ASSERT_EQUAL_STRING(
      "{\"deviceId\":\"R1\",\"requestId\":\"scan-1\",\"code\":\"[)>\\u001e06\\u001dP718-2362-1-ND\\u001dQ2\\u001e\\u0004\",\"quantity\":2}",
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

void test_protocol_v1_sync_round_trip() {
  DeviceEventRecord event;
  event.eventId = "R1-42";
  event.sequence = 42;
  event.kind = DeviceEventKind::InventoryAdjust;
  event.code = "0002";
  event.value = -3;
  const String ack = "R1-41-result";
  const auto json = buildSyncRequestJson("sync-7", "R1", "0.2.0", "ready", -51, 1,
                                         &event, 1, &ack, 1, "lookup-9", "0002");
  TEST_ASSERT_TRUE(json.indexOf("\"protocolVersion\":1") >= 0);
  TEST_ASSERT_TRUE(json.indexOf("\"type\":\"inventory.adjust\"") >= 0);
  TEST_ASSERT_TRUE(json.indexOf("\"value\":-3") >= 0);
  TEST_ASSERT_TRUE(json.indexOf("\"lookup\":{\"lookupId\":\"lookup-9\",\"code\":\"0002\"}") >= 0);

  const auto response = parseSyncResponseJson(
      "{\"protocolVersion\":1,\"requestId\":\"sync-7\",\"acceptedEventIds\":[\"R1-42\"],"
      "\"results\":[{\"resultId\":\"R1-42-result\",\"eventId\":\"R1-42\","
      "\"status\":\"completed\",\"existing\":true,\"itemName\":\"10k resistor\","
      "\"requestedDelta\":-3,\"appliedDelta\":-3,\"quantity\":17,"
      "\"location\":\"R2-B4\",\"code\":\"\",\"message\":\"Quantity updated\"}],"
      "\"lookupResult\":{\"lookupId\":\"lookup-9\",\"status\":\"found\","
      "\"itemName\":\"10k resistor\"}}" );
  TEST_ASSERT_TRUE(response.valid);
  TEST_ASSERT_EQUAL_STRING("sync-7", response.requestId.c_str());
  TEST_ASSERT_EQUAL_UINT32(1, response.acceptedCount);
  TEST_ASSERT_TRUE(response.hasResult);
  TEST_ASSERT_TRUE(response.result.existing);
  TEST_ASSERT_EQUAL_INT(17, response.result.quantity);
  TEST_ASSERT_EQUAL_STRING("R2-B4", response.result.location.c_str());
  TEST_ASSERT_TRUE(response.hasLookupResult);
  TEST_ASSERT_EQUAL_STRING("lookup-9", response.lookupId.c_str());
  TEST_ASSERT_EQUAL_STRING("found", response.lookupStatus.c_str());
  TEST_ASSERT_EQUAL_STRING("10k resistor", response.lookupItemName.c_str());
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

void test_ui_header_prefers_full_title_over_pc_status() {
  TEST_ASSERT_TRUE(displayHeaderCanShowStatus(52, 39, 122, 5));
  TEST_ASSERT_FALSE(displayHeaderCanShowStatus(83, 39, 122, 5));
  TEST_ASSERT_FALSE(displayHeaderCanShowStatus(-1, 20, 122, 5));
}

void test_ui_clipping_and_queue_boundaries() {
  const auto monospaceWidth = [](std::size_t length) { return static_cast<int>(length * 5U); };
  TEST_ASSERT_EQUAL_UINT32(5, displayClippedPrefixLength(5, 25, 3, monospaceWidth));
  TEST_ASSERT_EQUAL_UINT32(4, displayClippedPrefixLength(5, 24, 3, monospaceWidth));
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

void setup() {
  UNITY_BEGIN();
  RUN_TEST(test_standardized_scanner_codes);
  RUN_TEST(test_quantity_composer_defaults_and_limit);
  RUN_TEST(test_quantity_composer_cursor_replaces_and_appends);
  RUN_TEST(test_quantity_composer_replaces_default_and_deletes_digits);
  RUN_TEST(test_quantity_composer_subtract_and_invalid_digits);
  RUN_TEST(test_request_json_serialization);
  RUN_TEST(test_scan_json_escapes_datamatrix_separators);
  RUN_TEST(test_request_id_format);
  RUN_TEST(test_fixed_ring_buffer_fifo_and_overwrite);
  RUN_TEST(test_protocol_v1_sync_round_trip);
  RUN_TEST(test_quick_label_protocol_extensions);
  RUN_TEST(test_ui_header_prefers_full_title_over_pc_status);
  RUN_TEST(test_ui_clipping_and_queue_boundaries);
  RUN_TEST(test_ui_timing_and_activity_policy);
  RUN_TEST(test_scan_preemption_policy);
  UNITY_END();
}

void loop() {}
