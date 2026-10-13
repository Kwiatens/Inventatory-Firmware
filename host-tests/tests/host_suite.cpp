// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

// Runs the host-only Unity session. Each test that needs the NVS fault
// injection is declared here and listed below; main.cpp calls runHostSuite().
#include <unity.h>

void test_nvs_shim_enforces_namespace_length();
void test_nvs_shim_read_only_begin_needs_existing_namespace();
void test_nvs_shim_enforces_key_length();
void test_nvs_shim_length_query_is_zero_when_buffer_is_too_small();
void test_nvs_shim_power_cut_fails_every_later_write();
void test_nvs_shim_reboot_keeps_stored_values();
void test_nvs_shim_namespaces_are_isolated();
void test_nvs_shim_free_entries_track_writes();
void runHostOutboxDurabilityTests();
void runHostOutboxRecoveryTests();
void runHostOutboxMigrationTests();
void runHostSetupStateTests();
void runHostClientFramingTests();

void runHostSuite() {
  UNITY_BEGIN();
  RUN_TEST(test_nvs_shim_enforces_namespace_length);
  RUN_TEST(test_nvs_shim_read_only_begin_needs_existing_namespace);
  RUN_TEST(test_nvs_shim_enforces_key_length);
  RUN_TEST(test_nvs_shim_length_query_is_zero_when_buffer_is_too_small);
  RUN_TEST(test_nvs_shim_power_cut_fails_every_later_write);
  RUN_TEST(test_nvs_shim_reboot_keeps_stored_values);
  RUN_TEST(test_nvs_shim_namespaces_are_isolated);
  RUN_TEST(test_nvs_shim_free_entries_track_writes);
  runHostOutboxDurabilityTests();
  runHostOutboxRecoveryTests();
  runHostOutboxMigrationTests();
  runHostSetupStateTests();
  runHostClientFramingTests();
  UNITY_END();
}
