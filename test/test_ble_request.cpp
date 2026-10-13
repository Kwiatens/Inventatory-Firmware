// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

// The BLE provisioning write (protocol version 1) is untrusted input from a nearby
// central, so every malformed layout must be refused as a whole, and nothing may be
// stored from a request that fails any check. Runs on the board and on the host.
#include <unity.h>

#include <cstring>

#include "InventatoryScanBleRequest.h"

using namespace inventatory_scan;

void registerBleRequestTests();

void test_ble_request_accepts_a_well_formed_write();
void test_ble_request_accepts_an_open_network_and_the_length_limits();
void test_ble_request_rejects_bad_lengths_and_versions();
void test_ble_request_rejects_trailing_or_missing_bytes();
void test_ble_request_rejects_a_nul_byte_in_any_field();
void test_ble_request_rejects_a_bad_token();

namespace {

const char kToken[] = "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";

struct Request {
  uint8_t bytes[256];
  std::size_t size;
};

// Builds a version 1 write whose length bytes match the strings given.
Request makeRequest(const char* ssid, const char* password, const char* token) {
  Request request{};
  const std::size_t ssidLength = std::strlen(ssid);
  const std::size_t passwordLength = std::strlen(password);
  const std::size_t tokenLength = std::strlen(token);
  request.bytes[0] = kProvisioningProtocolVersion;
  request.bytes[1] = static_cast<uint8_t>(ssidLength);
  request.bytes[2] = static_cast<uint8_t>(passwordLength);
  request.bytes[3] = static_cast<uint8_t>(tokenLength);
  std::size_t offset = 4;
  std::memcpy(request.bytes + offset, ssid, ssidLength);
  offset += ssidLength;
  std::memcpy(request.bytes + offset, password, passwordLength);
  offset += passwordLength;
  std::memcpy(request.bytes + offset, token, tokenLength);
  request.size = offset + tokenLength;
  return request;
}

// A string of `count` copies of `value`, for the length limits.
struct Text {
  char data[80];
};

Text repeated(char value, std::size_t count) {
  Text text{};
  std::memset(text.data, value, count);
  text.data[count] = '\0';
  return text;
}

bool parse(const Request& request, ProvisionedSettings& settings) {
  return parseProvisioningRequest(request.bytes, request.size, settings);
}

}  // namespace

void test_ble_request_accepts_a_well_formed_write() {
  const Request request = makeRequest("Shop-Net", "correct horse", kToken);
  ProvisionedSettings settings;
  TEST_ASSERT_TRUE(parse(request, settings));
  TEST_ASSERT_EQUAL_STRING("Shop-Net", settings.wifiSsid.c_str());
  TEST_ASSERT_EQUAL_STRING("correct horse", settings.wifiPassword.c_str());
  TEST_ASSERT_EQUAL_STRING(kToken, settings.deviceToken.c_str());
}

void test_ble_request_accepts_an_open_network_and_the_length_limits() {
  ProvisionedSettings open;
  TEST_ASSERT_TRUE(parse(makeRequest("Cafe", "", kToken), open));
  TEST_ASSERT_EQUAL_STRING("", open.wifiPassword.c_str());

  const Text thirtyTwo = repeated('s', 32);
  const Text sixtyThree = repeated('p', 63);
  ProvisionedSettings limits;
  TEST_ASSERT_TRUE(parse(makeRequest(thirtyTwo.data, sixtyThree.data, kToken), limits));
  TEST_ASSERT_EQUAL_UINT32(32, limits.wifiSsid.length());
  TEST_ASSERT_EQUAL_UINT32(63, limits.wifiPassword.length());
}

void test_ble_request_rejects_bad_lengths_and_versions() {
  ProvisionedSettings settings;
  TEST_ASSERT_FALSE(parse(makeRequest("", "pw", kToken), settings));
  TEST_ASSERT_FALSE(parse(makeRequest(repeated('s', 33).data, "pw", kToken), settings));  // 33
  TEST_ASSERT_FALSE(parse(makeRequest("Net", repeated('p', 64).data, kToken),
                          settings));  // password 64
  TEST_ASSERT_FALSE(parse(makeRequest("Net", "pw", "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e"),
                          settings));  // token 63

  Request wrongVersion = makeRequest("Net", "pw", kToken);
  wrongVersion.bytes[0] = 2;
  TEST_ASSERT_FALSE(parse(wrongVersion, settings));
}

void test_ble_request_rejects_trailing_or_missing_bytes() {
  ProvisionedSettings settings;
  Request trailing = makeRequest("Net", "pw", kToken);
  trailing.bytes[trailing.size] = 0x41;
  trailing.size += 1;
  TEST_ASSERT_FALSE(parse(trailing, settings));

  Request truncated = makeRequest("Net", "pw", kToken);
  truncated.size -= 1;
  TEST_ASSERT_FALSE(parse(truncated, settings));

  // Shorter than the four-byte header, and no data at all.
  TEST_ASSERT_FALSE(parseProvisioningRequest(nullptr, 0, settings));
  const uint8_t header[] = {1, 3, 2};
  TEST_ASSERT_FALSE(parseProvisioningRequest(header, sizeof(header), settings));
}

void test_ble_request_rejects_a_nul_byte_in_any_field() {
  ProvisionedSettings settings;
  Request inSsid = makeRequest("Shop-Net", "pw", kToken);
  inSsid.bytes[4 + 4] = 0;
  TEST_ASSERT_FALSE(parse(inSsid, settings));

  Request inPassword = makeRequest("Net", "correct horse", kToken);
  inPassword.bytes[4 + 3 + 2] = 0;
  TEST_ASSERT_FALSE(parse(inPassword, settings));

  Request inToken = makeRequest("Net", "pw", kToken);
  inToken.bytes[inToken.size - 1] = 0;
  TEST_ASSERT_FALSE(parse(inToken, settings));
}

void test_ble_request_rejects_a_bad_token() {
  ProvisionedSettings settings;
  TEST_ASSERT_FALSE(parse(makeRequest("Net", "pw", "zz0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f"),
                          settings));
  TEST_ASSERT_FALSE(parse(makeRequest("Net", "pw", "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1 "),
                          settings));
  // Upper-case hexadecimal is the same token.
  TEST_ASSERT_TRUE(parse(makeRequest("Net", "pw", "000102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F"),
                         settings));
}

void registerBleRequestTests() {
  RUN_TEST(test_ble_request_accepts_a_well_formed_write);
  RUN_TEST(test_ble_request_accepts_an_open_network_and_the_length_limits);
  RUN_TEST(test_ble_request_rejects_bad_lengths_and_versions);
  RUN_TEST(test_ble_request_rejects_trailing_or_missing_bytes);
  RUN_TEST(test_ble_request_rejects_a_nul_byte_in_any_field);
  RUN_TEST(test_ble_request_rejects_a_bad_token);
}
