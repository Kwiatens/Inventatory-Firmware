// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

// Frame assembly for the GM65 scanner, driven with replayed byte streams and an
// explicit clock. The rule under test: a scan is reported only with its terminator,
// so a barcode that stalls mid-frame is dropped rather than split into two wrong
// scans (finding F7). Runs on the board and on the host.
#include <unity.h>

#include <cstring>

#include "InventatoryScanGm65Frame.h"

using namespace inventatory_scan;

void registerGm65FrameTests();

void test_gm65_crc16_has_the_xmodem_check_value();
void test_gm65_frame_ends_at_carriage_return_line_feed();
void test_gm65_line_feed_alone_also_ends_a_frame();
void test_gm65_empty_terminators_report_nothing();
void test_gm65_partial_frame_is_dropped_after_an_idle_gap();
void test_gm65_stale_fragment_cannot_join_a_later_frame();
void test_gm65_corrupt_byte_drops_the_whole_frame();
void test_gm65_oversized_frame_is_dropped_and_the_next_is_accepted();
void test_gm65_frame_holds_exactly_the_maximum_length();
void test_gm65_ready_frame_is_released_by_the_next_byte();
void test_gm65_reset_discards_a_partial_frame();
void test_gm65_payload_bytes_follow_the_printable_rule();

// One assembler shared by every test. It holds a 1 KB buffer, so it lives in static
// storage instead of on the 8 KiB loop stack. Each test calls reset() first, which
// clears every field that affects behaviour.
static Gm65FrameAssembler frames;

namespace {

void feedText(Gm65FrameAssembler& assembler, const char* text, uint32_t startMs, uint32_t stepMs) {
  uint32_t now = startMs;
  for (std::size_t index = 0; text[index] != '\0'; ++index) {
    (void)assembler.feed(static_cast<uint8_t>(text[index]), now);
    now += stepMs;
  }
}

bool frameIs(const Gm65FrameAssembler& assembler, const char* expected) {
  const std::size_t expectedLength = std::strlen(expected);
  return assembler.frameLength() == expectedLength &&
         std::memcmp(assembler.frame(), expected, expectedLength) == 0;
}

}  // namespace

void test_gm65_crc16_has_the_xmodem_check_value() {
  const uint8_t check[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
  TEST_ASSERT_EQUAL_HEX16(0x31C3, gm65Crc16(check, sizeof(check)));
  TEST_ASSERT_EQUAL_HEX16(0x0000, gm65Crc16(check, 0));
}

void test_gm65_frame_ends_at_carriage_return_line_feed() {
  frames.reset();
  feedText(frames, "ABC", 0, 1);
  TEST_ASSERT_TRUE(frames.feed('\r', 4) == Gm65FrameAssembler::Event::Frame);
  TEST_ASSERT_TRUE(frameIs(frames, "ABC"));
}

void test_gm65_line_feed_alone_also_ends_a_frame() {
  frames.reset();
  feedText(frames, "XY", 0, 1);
  TEST_ASSERT_TRUE(frames.feed('\n', 2) == Gm65FrameAssembler::Event::Frame);
  TEST_ASSERT_TRUE(frameIs(frames, "XY"));
}

void test_gm65_empty_terminators_report_nothing() {
  frames.reset();
  TEST_ASSERT_TRUE(frames.feed('\r', 0) == Gm65FrameAssembler::Event::None);
  TEST_ASSERT_TRUE(frames.feed('\n', 1) == Gm65FrameAssembler::Event::None);
  TEST_ASSERT_TRUE(frames.feed('\r', 2) == Gm65FrameAssembler::Event::None);
  TEST_ASSERT_EQUAL_UINT32(0, frames.frameLength());
}

void test_gm65_partial_frame_is_dropped_after_an_idle_gap() {
  frames.reset();
  feedText(frames, "12345", 0, 1);
  // The module stalls mid-barcode for longer than the idle timeout.
  TEST_ASSERT_TRUE(frames.expire(4U + kGm65FrameIdleTimeoutMs) == Gm65FrameAssembler::Event::Dropped);
  TEST_ASSERT_EQUAL_UINT32(0, frames.frameLength());

  // The rest of the barcode then arrives. It must not come out as the scan.
  feedText(frames, "678", 60, 1);
  TEST_ASSERT_TRUE(frames.feed('\r', 64) == Gm65FrameAssembler::Event::Frame);
  TEST_ASSERT_TRUE(frameIs(frames, "678"));
}

void test_gm65_stale_fragment_cannot_join_a_later_frame() {
  // Even when the caller does not run expire(), the next byte after a long gap must
  // not extend the old fragment.
  frames.reset();
  feedText(frames, "12345", 0, 1);
  TEST_ASSERT_TRUE(frames.feed('6', 100) == Gm65FrameAssembler::Event::Dropped);
  feedText(frames, "78", 101, 1);
  TEST_ASSERT_TRUE(frames.feed('\r', 103) == Gm65FrameAssembler::Event::Frame);
  TEST_ASSERT_TRUE(frameIs(frames, "678"));
}

void test_gm65_corrupt_byte_drops_the_whole_frame() {
  frames.reset();
  feedText(frames, "AB", 0, 1);
  TEST_ASSERT_TRUE(frames.feed(0x01, 2) == Gm65FrameAssembler::Event::None);
  feedText(frames, "CD", 3, 1);
  TEST_ASSERT_TRUE(frames.feed('\r', 5) == Gm65FrameAssembler::Event::Dropped);

  feedText(frames, "EF", 6, 1);
  TEST_ASSERT_TRUE(frames.feed('\r', 8) == Gm65FrameAssembler::Event::Frame);
  TEST_ASSERT_TRUE(frameIs(frames, "EF"));
}

void test_gm65_oversized_frame_is_dropped_and_the_next_is_accepted() {
  frames.reset();
  uint32_t now = 0;
  for (std::size_t index = 0; index < kGm65MaxFrameBytes + 10U; ++index) {
    TEST_ASSERT_TRUE(frames.feed('a', now++) == Gm65FrameAssembler::Event::None);
  }
  TEST_ASSERT_TRUE(frames.feed('\r', now++) == Gm65FrameAssembler::Event::Dropped);

  feedText(frames, "OK", now, 1);
  TEST_ASSERT_TRUE(frames.feed('\r', now + 2) == Gm65FrameAssembler::Event::Frame);
  TEST_ASSERT_TRUE(frameIs(frames, "OK"));
}

void test_gm65_frame_holds_exactly_the_maximum_length() {
  frames.reset();
  uint32_t now = 0;
  for (std::size_t index = 0; index < kGm65MaxFrameBytes; ++index) {
    TEST_ASSERT_TRUE(frames.feed('b', now++) == Gm65FrameAssembler::Event::None);
  }
  TEST_ASSERT_TRUE(frames.feed('\r', now) == Gm65FrameAssembler::Event::Frame);
  TEST_ASSERT_EQUAL_UINT32(kGm65MaxFrameBytes, frames.frameLength());
}

void test_gm65_ready_frame_is_released_by_the_next_byte() {
  frames.reset();
  feedText(frames, "A", 0, 1);
  TEST_ASSERT_TRUE(frames.feed('\r', 1) == Gm65FrameAssembler::Event::Frame);
  TEST_ASSERT_TRUE(frameIs(frames, "A"));

  // The frame stays readable until the next byte, then it is released.
  TEST_ASSERT_TRUE(frames.feed('B', 2) == Gm65FrameAssembler::Event::None);
  TEST_ASSERT_EQUAL_UINT32(0, frames.frameLength());
  TEST_ASSERT_TRUE(frames.feed('\r', 3) == Gm65FrameAssembler::Event::Frame);
  TEST_ASSERT_TRUE(frameIs(frames, "B"));
}

void test_gm65_reset_discards_a_partial_frame() {
  frames.reset();
  feedText(frames, "half", 0, 1);
  frames.reset();
  TEST_ASSERT_TRUE(frames.expire(1000) == Gm65FrameAssembler::Event::None);
  feedText(frames, "whole", 1000, 1);
  TEST_ASSERT_TRUE(frames.feed('\r', 1010) == Gm65FrameAssembler::Event::Frame);
  TEST_ASSERT_TRUE(frameIs(frames, "whole"));
}

void test_gm65_payload_bytes_follow_the_printable_rule() {
  TEST_ASSERT_TRUE(gm65IsPayloadByte(0x20));
  TEST_ASSERT_TRUE(gm65IsPayloadByte(0x7E));
  TEST_ASSERT_TRUE(gm65IsPayloadByte(0x1D));
  TEST_ASSERT_TRUE(gm65IsPayloadByte(0x1E));
  TEST_ASSERT_TRUE(gm65IsPayloadByte(0x04));
  TEST_ASSERT_FALSE(gm65IsPayloadByte(0x1F));
  TEST_ASSERT_FALSE(gm65IsPayloadByte(0x7F));
  TEST_ASSERT_FALSE(gm65IsPayloadByte(0x80));
  TEST_ASSERT_FALSE(gm65IsPayloadByte(0x00));
}

void registerGm65FrameTests() {
  RUN_TEST(test_gm65_crc16_has_the_xmodem_check_value);
  RUN_TEST(test_gm65_frame_ends_at_carriage_return_line_feed);
  RUN_TEST(test_gm65_line_feed_alone_also_ends_a_frame);
  RUN_TEST(test_gm65_empty_terminators_report_nothing);
  RUN_TEST(test_gm65_partial_frame_is_dropped_after_an_idle_gap);
  RUN_TEST(test_gm65_stale_fragment_cannot_join_a_later_frame);
  RUN_TEST(test_gm65_corrupt_byte_drops_the_whole_frame);
  RUN_TEST(test_gm65_oversized_frame_is_dropped_and_the_next_is_accepted);
  RUN_TEST(test_gm65_frame_holds_exactly_the_maximum_length);
  RUN_TEST(test_gm65_ready_frame_is_released_by_the_next_byte);
  RUN_TEST(test_gm65_reset_discards_a_partial_frame);
  RUN_TEST(test_gm65_payload_bytes_follow_the_printable_rule);
}
