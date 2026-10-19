// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

// GM65 frame assembly with replayed bytes and an explicit clock. A scan is reported only
// with its terminator, so a barcode that stalls mid-frame is dropped, not split.
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
void test_gm65_frame_terminators_are_cr_lf_and_end_of_transmission();
void test_gm65_end_of_transmission_ends_a_data_matrix_without_crlf();
void test_gm65_repeated_separator_trailer_is_not_reported();
void test_gm65_zone_command_matches_the_manual_examples();
void test_gm65_deep_sleep_and_wake_are_zone_0xd9_commands();
void test_gm65_zone_read_reply_is_checked_before_its_value_is_used();
void test_gm65_lamps_off_keeps_the_other_zone_bits();
void test_gm65_lamps_are_off_only_when_both_are_off_and_restore_to_standard();

// Static, so the 1 KB buffer stays off the 8 KiB loop stack. Each test calls reset() first.
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
  // Without expire(), a byte after a long gap must not extend the old fragment.
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
  TEST_ASSERT_FALSE(gm65IsPayloadByte(0x04));
  TEST_ASSERT_FALSE(gm65IsPayloadByte(0x1F));
  TEST_ASSERT_FALSE(gm65IsPayloadByte(0x7F));
  TEST_ASSERT_FALSE(gm65IsPayloadByte(0x80));
  TEST_ASSERT_FALSE(gm65IsPayloadByte(0x00));
}

void test_gm65_frame_terminators_are_cr_lf_and_end_of_transmission() {
  TEST_ASSERT_TRUE(gm65IsFrameTerminator('\r'));
  TEST_ASSERT_TRUE(gm65IsFrameTerminator('\n'));
  TEST_ASSERT_TRUE(gm65IsFrameTerminator(0x04));
  TEST_ASSERT_FALSE(gm65IsFrameTerminator(0x1E));
  TEST_ASSERT_FALSE(gm65IsFrameTerminator('A'));
}

void test_gm65_end_of_transmission_ends_a_data_matrix_without_crlf() {
  // The GM65 sends this Data Matrix with no CR or LF: the ISO 15434 trailer RS EOT ends it.
  frames.reset();
  feedText(frames, "[)>\x1e" "06\x1d" "PABC-ND\x1e", 0, 1);
  TEST_ASSERT_TRUE(frames.feed(0x04, 15) == Gm65FrameAssembler::Event::Frame);
  TEST_ASSERT_TRUE(frameIs(frames, "[)>\x1e" "06\x1d" "PABC-ND\x1e"));
}

void test_gm65_repeated_separator_trailer_is_not_reported() {
  // A Data Matrix can end in RS EOT RS EOT. The second pair has no printable byte: dropped.
  frames.reset();
  feedText(frames, "PABC\x1e", 0, 1);
  TEST_ASSERT_TRUE(frames.feed(0x04, 5) == Gm65FrameAssembler::Event::Frame);
  TEST_ASSERT_TRUE(frames.feed(0x1E, 6) == Gm65FrameAssembler::Event::None);
  TEST_ASSERT_TRUE(frames.feed(0x04, 7) == Gm65FrameAssembler::Event::Dropped);
  TEST_ASSERT_EQUAL_UINT32(0, frames.frameLength());

  feedText(frames, "B\r", 100, 1);
  TEST_ASSERT_TRUE(frameIs(frames, "B"));
}

void test_gm65_zone_command_matches_the_manual_examples() {
  // Manual sections 9.2, 9.3 and 9.4: write 0x3E to address 0x000A, read address 0x000A, and save.
  uint8_t command[kGm65ZoneCommandLength];
  gm65ZoneCommand(0x08, 0x000A, 0x3E, command);
  const uint8_t write[] = {0x7E, 0x00, 0x08, 0x01, 0x00, 0x0A, 0x3E, 0x4C, 0xCF};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(write, command, sizeof(write));

  gm65ZoneCommand(0x07, 0x000A, 0x01, command);
  const uint8_t read[] = {0x7E, 0x00, 0x07, 0x01, 0x00, 0x0A, 0x01, 0xEE, 0x8A};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(read, command, sizeof(read));

  gm65ZoneCommand(0x09, 0x0000, 0x00, command);
  const uint8_t save[] = {0x7E, 0x00, 0x09, 0x01, 0x00, 0x00, 0x00, 0xDE, 0xC8};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(save, command, sizeof(save));
}

void test_gm65_deep_sleep_and_wake_are_zone_0xd9_commands() {
  // Manual section 9.7. Sleep is not yet checked on a board.
  uint8_t command[kGm65ZoneCommandLength];
  gm65ZoneCommand(0x08, kGm65FunctionZone, kGm65DeepSleep, command);
  const uint8_t sleep[] = {0x7E, 0x00, 0x08, 0x01, 0x00, 0xD9, 0xA5, 0x3E, 0x69};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(sleep, command, sizeof(sleep));

  gm65ZoneCommand(0x08, kGm65FunctionZone, kGm65Wake, command);
  const uint8_t wake[] = {0x7E, 0x00, 0x08, 0x01, 0x00, 0xD9, 0x00, 0xDB, 0x26};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(wake, command, sizeof(wake));
}

void test_gm65_zone_read_reply_is_checked_before_its_value_is_used() {
  // Manual section 9.2: reading address 0x000A returns 0x3E with CRC E4 AC.
  uint8_t value = 0;
  const uint8_t reply[] = {0x02, 0x00, 0x00, 0x01, 0x3E, 0xE4, 0xAC};
  TEST_ASSERT_TRUE(gm65ZoneReadValue(reply, value));
  TEST_ASSERT_EQUAL_HEX8(0x3E, value);

  const uint8_t badCrc[] = {0x02, 0x00, 0x00, 0x01, 0x3E, 0xE4, 0xAD};
  TEST_ASSERT_FALSE(gm65ZoneReadValue(badCrc, value));
  const uint8_t badLength[] = {0x02, 0x00, 0x00, 0x02, 0x3E, 0xE4, 0xAC};
  TEST_ASSERT_FALSE(gm65ZoneReadValue(badLength, value));
  const uint8_t badType[] = {0x02, 0x00, 0x01, 0x01, 0x3E, 0xE4, 0xAC};
  TEST_ASSERT_FALSE(gm65ZoneReadValue(badType, value));
}

void test_gm65_lamps_off_keeps_the_other_zone_bits() {
  // Zone 0x0000: bits 5-4 (head lamp) and 3-2 (pointing light) go to 00; the rest stay.
  TEST_ASSERT_EQUAL_HEX8(0xC3, gm65LampsOffZoneValue(0xFF));
  TEST_ASSERT_EQUAL_HEX8(0x43, gm65LampsOffZoneValue(0x7F));
  TEST_ASSERT_EQUAL_HEX8(0x40, gm65LampsOffZoneValue(0x40));
  TEST_ASSERT_EQUAL_HEX8(0x03, gm65LampsOffZoneValue(0x13));
}

void test_gm65_lamps_are_off_only_when_both_are_off_and_restore_to_standard() {
  TEST_ASSERT_TRUE(gm65LampsAreOff(0x00));
  TEST_ASSERT_TRUE(gm65LampsAreOff(0x43));
  TEST_ASSERT_FALSE(gm65LampsAreOff(0x14));
  TEST_ASSERT_FALSE(gm65LampsAreOff(0x10));
  TEST_ASSERT_FALSE(gm65LampsAreOff(0x04));

  TEST_ASSERT_EQUAL_HEX8(0xD7, gm65LampsStandardZoneValue(0xC3));
  TEST_ASSERT_EQUAL_HEX8(0x17, gm65LampsStandardZoneValue(0x03));
}

void test_gm65_sleep_turns_the_lamps_off_and_sets_manual_read_mode() {
  // Lamps (bits 5-2) and read mode (bits 1-0) go to 00. The other bits are kept.
  TEST_ASSERT_EQUAL_HEX8(0xC0, gm65SleepZoneValue(0xD6));
  TEST_ASSERT_EQUAL_HEX8(0x40, gm65SleepZoneValue(0x7F));
  TEST_ASSERT_EQUAL_HEX8(0xC0, gm65SleepZoneValue(0xFF));
}

void registerGm65FrameTests() {
  RUN_TEST(test_gm65_sleep_turns_the_lamps_off_and_sets_manual_read_mode);
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
  RUN_TEST(test_gm65_frame_terminators_are_cr_lf_and_end_of_transmission);
  RUN_TEST(test_gm65_end_of_transmission_ends_a_data_matrix_without_crlf);
  RUN_TEST(test_gm65_repeated_separator_trailer_is_not_reported);
  RUN_TEST(test_gm65_zone_command_matches_the_manual_examples);
  RUN_TEST(test_gm65_deep_sleep_and_wake_are_zone_0xd9_commands);
  RUN_TEST(test_gm65_zone_read_reply_is_checked_before_its_value_is_used);
  RUN_TEST(test_gm65_lamps_off_keeps_the_other_zone_bits);
  RUN_TEST(test_gm65_lamps_are_off_only_when_both_are_off_and_restore_to_standard);
}
