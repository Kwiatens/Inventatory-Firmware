// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

// Direct tests for the helpers that build the desktop JSON and read barcode identifiers.
#include <unity.h>

#include "InventatoryScanCore.h"

using namespace inventatory_scan;

void registerTextHelperTests();

void test_text_escape_json_escapes_quotes_backslashes_and_short_controls();
void test_text_escape_json_passes_utf8_and_printable_bytes_through();
void test_text_strip_aim_prefix_removes_only_a_full_identifier();
void test_text_iso15434_fields_need_their_identifier_and_a_value();
void test_text_separators_are_the_three_iso15434_controls();
void test_text_standard_machine_codes_are_four_digits();
void test_text_standard_manufacturer_part_numbers();
void test_text_trim_copy_removes_only_whitespace_at_the_ends();

void test_text_escape_json_escapes_quotes_backslashes_and_short_controls() {
  TEST_ASSERT_EQUAL_STRING("a\\\"b\\\\c", escapeJson(String("a\"b\\c")).c_str());
  TEST_ASSERT_EQUAL_STRING("\\n\\r\\t", escapeJson(String("\n\r\t")).c_str());
  String controls;
  controls += static_cast<char>(0x01);
  controls += static_cast<char>(0x1D);
  controls += static_cast<char>(0x1F);
  TEST_ASSERT_EQUAL_STRING("\\u0001\\u001d\\u001f", escapeJson(controls).c_str());
}

void test_text_escape_json_passes_utf8_and_printable_bytes_through() {
  TEST_ASSERT_EQUAL_STRING("za\xC5\xBC\xC3\xB3\xC5\x82\xC4\x87", escapeJson(String("za\xC5\xBC\xC3\xB3\xC5\x82\xC4\x87")).c_str());
  String edge;
  edge += static_cast<char>(0x20);
  edge += static_cast<char>(0x7F);
  TEST_ASSERT_EQUAL_UINT32(2, escapeJson(edge).length());
}

void test_text_strip_aim_prefix_removes_only_a_full_identifier() {
  TEST_ASSERT_EQUAL_STRING("abc", stripAimSymbologyPrefix(String("]C1abc")).c_str());
  TEST_ASSERT_EQUAL_STRING("abc", stripAimSymbologyPrefix(String("]d2abc")).c_str());
  // An identifier needs ']' and two letters or digits. Anything else is left alone.
  TEST_ASSERT_EQUAL_STRING("]C", stripAimSymbologyPrefix(String("]C")).c_str());
  TEST_ASSERT_EQUAL_STRING("]-1abc", stripAimSymbologyPrefix(String("]-1abc")).c_str());
  TEST_ASSERT_EQUAL_STRING("1]C1abc", stripAimSymbologyPrefix(String("1]C1abc")).c_str());
}

void test_text_iso15434_fields_need_their_identifier_and_a_value() {
  String value;
  TEST_ASSERT_TRUE(iso15434Field(String("1PABC"), "1P", value));
  TEST_ASSERT_EQUAL_STRING("ABC", value.c_str());
  // Surrounding blanks are removed, inner blanks are kept.
  TEST_ASSERT_TRUE(iso15434Field(String("1P  X  Y  "), "1P", value));
  TEST_ASSERT_EQUAL_STRING("X  Y", value.c_str());
  // An identifier with no value, or only blanks, is not a field.
  TEST_ASSERT_FALSE(iso15434Field(String("1P"), "1P", value));
  TEST_ASSERT_FALSE(iso15434Field(String("1P   "), "1P", value));
  TEST_ASSERT_FALSE(iso15434Field(String("P123"), "1P", value));
}

void test_text_separators_are_the_three_iso15434_controls() {
  TEST_ASSERT_TRUE(isIso15434Separator(static_cast<char>(0x1D)));
  TEST_ASSERT_TRUE(isIso15434Separator(static_cast<char>(0x1E)));
  TEST_ASSERT_TRUE(isIso15434Separator(static_cast<char>(0x04)));
  TEST_ASSERT_FALSE(isIso15434Separator(' '));
  TEST_ASSERT_FALSE(isIso15434Separator('\n'));
}

void test_text_standard_machine_codes_are_four_digits() {
  TEST_ASSERT_TRUE(isStandardMachineCode(String("0042")));
  TEST_ASSERT_TRUE(isStandardMachineCode(String(" 0042 ")));
  TEST_ASSERT_FALSE(isStandardMachineCode(String("42")));
  TEST_ASSERT_FALSE(isStandardMachineCode(String("00A2")));
  TEST_ASSERT_FALSE(isStandardMachineCode(String("00425")));
  TEST_ASSERT_FALSE(isStandardMachineCode(String("")));
}

void test_text_standard_manufacturer_part_numbers() {
  TEST_ASSERT_TRUE(isStandardManufacturerPartNumber(String("TPS7A2033PDBVR")));
  TEST_ASSERT_TRUE(isStandardManufacturerPartNumber(String("A B")));
  TEST_ASSERT_TRUE(isStandardManufacturerPartNumber(String("R-1.0/2+3#&,()_")));
  TEST_ASSERT_FALSE(isStandardManufacturerPartNumber(String("A")));
  TEST_ASSERT_FALSE(isStandardManufacturerPartNumber(String("---")));
  TEST_ASSERT_FALSE(isStandardManufacturerPartNumber(String("ABC!")));
  TEST_ASSERT_FALSE(isStandardManufacturerPartNumber(String("ABC\x7F")));
}

void test_text_trim_copy_removes_only_whitespace_at_the_ends() {
  TEST_ASSERT_EQUAL_STRING("a b", trimCopy(String("  a b  ")).c_str());
  TEST_ASSERT_EQUAL_STRING("", trimCopy(String("\t\r\n ")).c_str());
  TEST_ASSERT_EQUAL_STRING("x", trimCopy(String("x")).c_str());
}

void registerTextHelperTests() {
  RUN_TEST(test_text_escape_json_escapes_quotes_backslashes_and_short_controls);
  RUN_TEST(test_text_escape_json_passes_utf8_and_printable_bytes_through);
  RUN_TEST(test_text_strip_aim_prefix_removes_only_a_full_identifier);
  RUN_TEST(test_text_iso15434_fields_need_their_identifier_and_a_value);
  RUN_TEST(test_text_separators_are_the_three_iso15434_controls);
  RUN_TEST(test_text_standard_machine_codes_are_four_digits);
  RUN_TEST(test_text_standard_manufacturer_part_numbers);
  RUN_TEST(test_text_trim_copy_removes_only_whitespace_at_the_ends);
}
