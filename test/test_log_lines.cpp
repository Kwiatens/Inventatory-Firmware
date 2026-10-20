// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

// The lines kept for the over-the-air log: the assembler turns the bytes of the serial log into lines, and the
// ring keeps the newest of them, dropping the oldest when it is full.

#include <unity.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "InventatoryScanLogLines.h"

using namespace inventatory_scan;

namespace {

// Feeds text through the assembler one byte at a time, as the serial writes arrive.
void feed(LogLineAssembler& assembler, LogLineRing& ring, const char* text, uint32_t timestampMs) {
  for (const char* p = text; *p != '\0'; ++p) assembler.add(static_cast<uint8_t>(*p), timestampMs, ring);
}

bool lineIs(const LogLine& line, const char* text) {
  const std::size_t length = std::strlen(text);
  return line.length == length && std::memcmp(line.text, text, length) == 0;
}

}  // namespace

void test_log_lines_end_at_newline_and_drop_carriage_return() {
  LogLineAssembler assembler;
  LogLineRing ring;
  feed(assembler, ring, "Battery 3553 mV\r\nOutbox ready\r\n", 1200);
  TEST_ASSERT_EQUAL_UINT32(2, ring.size());
  LogLine line;
  TEST_ASSERT_TRUE(ring.pop(line));
  TEST_ASSERT_EQUAL_UINT32(1200, line.timestampMs);
  TEST_ASSERT_TRUE(lineIs(line, "Battery 3553 mV"));
  TEST_ASSERT_TRUE(ring.pop(line));
  TEST_ASSERT_TRUE(lineIs(line, "Outbox ready"));
  TEST_ASSERT_FALSE(ring.pop(line));
}

void test_log_partial_line_waits_for_its_newline() {
  LogLineAssembler assembler;
  LogLineRing ring;
  feed(assembler, ring, "Battery", 100);
  TEST_ASSERT_EQUAL_UINT32(0, ring.size());
  feed(assembler, ring, " 3553\n", 200);
  TEST_ASSERT_EQUAL_UINT32(1, ring.size());
  LogLine line;
  TEST_ASSERT_TRUE(ring.pop(line));
  // The line takes the time of its first byte.
  TEST_ASSERT_EQUAL_UINT32(100, line.timestampMs);
  TEST_ASSERT_TRUE(lineIs(line, "Battery 3553"));
}

void test_log_empty_line_is_kept() {
  LogLineAssembler assembler;
  LogLineRing ring;
  feed(assembler, ring, "\r\n\r\n", 5);
  TEST_ASSERT_EQUAL_UINT32(2, ring.size());
  LogLine line;
  TEST_ASSERT_TRUE(ring.pop(line));
  TEST_ASSERT_EQUAL_UINT32(0, line.length);
}

void test_log_control_bytes_are_sent_as_dots() {
  // A terminal escape and a tab do not reach the network as control bytes.
  LogLineAssembler assembler;
  LogLineRing ring;
  feed(assembler, ring, "a\x1b[0m\t\n", 7);
  LogLine line;
  TEST_ASSERT_TRUE(ring.pop(line));
  TEST_ASSERT_TRUE(lineIs(line, "a.[0m."));
}

void test_log_line_is_cut_at_capacity() {
  LogLineAssembler assembler;
  LogLineRing ring;
  std::size_t longLine = kLogLineBytes * 2;
  for (std::size_t index = 0; index < longLine; ++index) assembler.add('x', 9, ring);
  assembler.add('\n', 9, ring);
  LogLine line;
  TEST_ASSERT_TRUE(ring.pop(line));
  TEST_ASSERT_EQUAL_UINT32(kLogLineBytes - 1U, line.length);
  // The text after the cut is not carried into the next line.
  TEST_ASSERT_FALSE(ring.pop(line));
}

void test_log_ring_pops_in_order() {
  LogLineRing ring;
  for (uint32_t index = 0; index < 3; ++index) {
    LogLine line;
    line.timestampMs = index;
    line.length = 1;
    line.text[0] = static_cast<char>('a' + index);
    ring.push(line);
  }
  LogLine line;
  for (uint32_t index = 0; index < 3; ++index) {
    TEST_ASSERT_TRUE(ring.pop(line));
    TEST_ASSERT_EQUAL_UINT32(index, line.timestampMs);
  }
  TEST_ASSERT_EQUAL_UINT32(0, ring.dropped());
}

void test_log_ring_drops_the_oldest_when_full() {
  LogLineRing ring;
  for (uint32_t index = 0; index < kLogLineSlots + 3; ++index) {
    LogLine line;
    line.timestampMs = index;
    ring.push(line);
  }
  TEST_ASSERT_EQUAL_UINT32(kLogLineSlots, ring.size());
  TEST_ASSERT_EQUAL_UINT32(3, ring.dropped());
  LogLine line;
  TEST_ASSERT_TRUE(ring.pop(line));
  TEST_ASSERT_EQUAL_UINT32(3, line.timestampMs);
}

void registerLogLineTests() {
  RUN_TEST(test_log_lines_end_at_newline_and_drop_carriage_return);
  RUN_TEST(test_log_partial_line_waits_for_its_newline);
  RUN_TEST(test_log_empty_line_is_kept);
  RUN_TEST(test_log_control_bytes_are_sent_as_dots);
  RUN_TEST(test_log_line_is_cut_at_capacity);
  RUN_TEST(test_log_ring_pops_in_order);
  RUN_TEST(test_log_ring_drops_the_oldest_when_full);
}
