// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

// The "Sync blocked" rule: five refusals of the queue head in a row block the queue,
// and any acknowledgement clears it. The screen is updated only on the transition, so
// these tests also pin when the caller is told to change it. Runs on the board and on the host.
#include <unity.h>

#include "InventatoryScanBlockedHead.h"

using namespace inventatory_scan;

void registerBlockedHeadTests();

void test_blocked_head_blocks_on_the_fifth_refusal_only();
void test_blocked_head_reports_the_transition_once();
void test_blocked_head_progress_clears_the_block_and_reports_it_once();
void test_blocked_head_counter_saturates_without_wrapping();

void test_blocked_head_blocks_on_the_fifth_refusal_only() {
  BlockedHeadTracker tracker;
  for (int refusal = 1; refusal <= 4; ++refusal) {
    TEST_ASSERT_FALSE(tracker.recordRefusal());
    TEST_ASSERT_FALSE(tracker.blocked());
  }
  TEST_ASSERT_TRUE(tracker.recordRefusal());
  TEST_ASSERT_TRUE(tracker.blocked());
  TEST_ASSERT_EQUAL_UINT8(BlockedHeadTracker::kThreshold, tracker.streak());
}

void test_blocked_head_reports_the_transition_once() {
  BlockedHeadTracker tracker;
  for (int refusal = 0; refusal < 5; ++refusal) (void)tracker.recordRefusal();
  // Further refusals keep the queue blocked, but they are not new transitions.
  TEST_ASSERT_FALSE(tracker.recordRefusal());
  TEST_ASSERT_FALSE(tracker.recordRefusal());
  TEST_ASSERT_TRUE(tracker.blocked());
}

void test_blocked_head_progress_clears_the_block_and_reports_it_once() {
  BlockedHeadTracker tracker;
  for (int refusal = 0; refusal < 5; ++refusal) (void)tracker.recordRefusal();

  TEST_ASSERT_TRUE(tracker.recordProgress());
  TEST_ASSERT_FALSE(tracker.blocked());
  TEST_ASSERT_EQUAL_UINT8(0, tracker.streak());
  // Acknowledging while not blocked changes nothing, so there is nothing to report.
  TEST_ASSERT_FALSE(tracker.recordProgress());

  // A new run of refusals has to start again from one.
  for (int refusal = 1; refusal <= 4; ++refusal) TEST_ASSERT_FALSE(tracker.recordRefusal());
  TEST_ASSERT_FALSE(tracker.blocked());
}

void test_blocked_head_counter_saturates_without_wrapping() {
  BlockedHeadTracker tracker;
  for (int refusal = 0; refusal < 300; ++refusal) (void)tracker.recordRefusal();
  TEST_ASSERT_TRUE(tracker.blocked());
  TEST_ASSERT_EQUAL_UINT8(BlockedHeadTracker::kThreshold, tracker.streak());
}

void registerBlockedHeadTests() {
  RUN_TEST(test_blocked_head_blocks_on_the_fifth_refusal_only);
  RUN_TEST(test_blocked_head_reports_the_transition_once);
  RUN_TEST(test_blocked_head_progress_clears_the_block_and_reports_it_once);
  RUN_TEST(test_blocked_head_counter_saturates_without_wrapping);
}
