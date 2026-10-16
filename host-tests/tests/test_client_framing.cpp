// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

// Response framing for the sync client, run against the real InventatoryClient with
// a scripted server (see host-tests/shim/WiFiClient.h). The rules under test are the
// transport v1 limits (AGENTS.md section 8): a bounded header and body, an exact
// Content-Length, no chunked encoding, one of each framing header, and a response MAC
// that covers the status and the whole body. Every refusal must leave httpStatus at 0.
#include <unity.h>

#include <cstdlib>
#include <cstring>
#include <string>

#include "InventatoryScanHttp.h"
#include "InventatoryScanSecurity.h"
#include "net/inventatory_client.h"
#include "setup/setup_state_store.h"
#include "hostnet.h"

using namespace inventatory_scan;

void runHostClientFramingTests();

void test_client_accepts_a_signed_response();
void test_client_reports_a_signed_status_other_than_200();
void test_client_authenticates_a_body_with_a_nul_byte_by_its_full_length();
void test_client_refuses_a_repeated_content_length();
void test_client_refuses_a_repeated_mac_header();
void test_client_refuses_chunked_transfer_encoding();
void test_client_refuses_a_nul_byte_in_the_header_block();
void test_client_accepts_a_header_block_of_exactly_the_limit();
void test_client_refuses_a_header_block_one_byte_over_the_limit();
void test_client_accepts_a_body_of_exactly_the_limit();
void test_client_requires_content_length_to_match_the_body();
void test_client_refuses_a_wrong_mac();
void test_client_binds_the_status_into_the_mac();
void test_client_requires_the_request_counter_to_be_echoed();
void test_client_requires_protocol_version_one();
void test_client_refuses_a_header_block_over_the_limit();
void test_client_refuses_a_body_over_the_limit();
void test_client_refuses_a_response_without_a_blank_line();
void test_client_refuses_a_status_line_without_http();
void test_client_times_out_on_a_silent_server();
void test_client_refuses_when_no_server_is_found();
void test_client_refuses_when_the_connection_fails();
void test_client_refuses_when_wifi_is_down();
void test_client_gives_every_request_a_higher_counter();

namespace {

const char kToken[] = "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";
const char kDeviceId[] = "Inventatory-SCAN-R1";

// Converts a std::string to the firmware's String without losing NUL bytes, which the
// String(const char*) constructor would truncate.
String toFirmwareString(const std::string& text) {
  String out;
  for (const char ch : text) out += ch;
  return out;
}

std::string toStd(const String& text) {
  std::string out;
  for (std::size_t index = 0; index < text.length(); ++index) out += text[static_cast<unsigned int>(index)];
  return out;
}

std::string counterOf(const std::string& request) {
  const std::string key = "X-Inventatory-Counter: ";
  const auto start = request.find(key);
  if (start == std::string::npos) return std::string();
  const auto end = request.find('\r', start);
  return request.substr(start + key.size(), end - start - key.size());
}

// What a server sends back for one request. The defaults are a correct response, so
// each test changes only the field it is about.
struct ResponseSpec {
  int status = 200;
  std::string body = "{\"ok\":true}";
  int macStatus = 200;           // the status the MAC covers
  std::string macBody;           // the body the MAC covers, when it is not `body`
  std::string extraHeaders;      // raw "Name: value\r\n" lines
  std::string overrideCounter;   // empty: echo the request's counter
  std::string overrideProtocol;  // empty: "1"
  std::string overrideLength;    // empty: the body's real length
  std::string statusLine;        // empty: "HTTP/1.1 <status> OK"
  bool blankLine = true;
  bool repeatLength = false;     // repeat the real Content-Length line, with the same value
  bool repeatMac = false;        // repeat the real X-Inventatory-Mac line, with the same value
};

std::string makeResponse(const std::string& request, const ResponseSpec& spec) {
  const std::string counter = counterOf(request);
  const std::string macBody = spec.macBody.empty() ? spec.body : spec.macBody;
  const uint64_t counterValue = std::strtoull(counter.c_str(), nullptr, 10);
  const String mac = deviceResponseMac(String(kToken), counterValue, spec.macStatus, toFirmwareString(macBody));

  std::string text = spec.statusLine.empty() ? "HTTP/1.1 " + std::to_string(spec.status) + " OK" : spec.statusLine;
  text += "\r\nContent-Type: application/json\r\n";
  text += "Content-Length: " + (spec.overrideLength.empty() ? std::to_string(spec.body.size()) : spec.overrideLength) + "\r\n";
  text += "X-Inventatory-Protocol: " + (spec.overrideProtocol.empty() ? std::string("1") : spec.overrideProtocol) + "\r\n";
  text += "X-Inventatory-Counter: " + (spec.overrideCounter.empty() ? counter : spec.overrideCounter) + "\r\n";
  text += "X-Inventatory-Mac: " + std::string(mac.c_str()) + "\r\n";
  if (spec.repeatLength) text += "Content-Length: " + std::to_string(spec.body.size()) + "\r\n";
  if (spec.repeatMac) text += "X-Inventatory-Mac: " + std::string(mac.c_str()) + "\r\n";
  text += spec.extraHeaders;
  text += "Connection: close\r\n";
  if (spec.blankLine) text += "\r\n";
  text += spec.body;
  return text;
}

// Runs one sync exchange with the given response. Returns whether the client accepted it.
bool syncWith(const ResponseSpec& spec, int& httpStatus, std::string& body) {
  HostConnection& net = hostNetwork();
  net.responder = [spec](const std::string& request) { return makeResponse(request, spec); };
  InventatoryClient client;
  SetupStateStore store;
  store.begin();
  InventatoryClientConfig config;
  config.deviceId = kDeviceId;
  config.token = kToken;
  config.transportCounterStore = &store;
  const bool begun = client.begin(config);
  TEST_ASSERT_TRUE(begun);
  String responseBody;
  const bool ok = client.postSync(String("{\"requestId\":\"r\"}"), httpStatus, responseBody);
  body = toStd(responseBody);
  return ok;
}

// Stores a complete provisioning record, so the transport counter can be reserved.
void provisionTransport() {
  SetupStateStore store;
  TEST_ASSERT_TRUE(store.begin());
  ProvisionedSettings settings;
  settings.wifiSsid = "Shop-Net";
  settings.wifiPassword = "pw";
  settings.deviceToken = kToken;
  TEST_ASSERT_TRUE(store.save(settings));
}

void resetNetwork() {
  hostNetwork() = HostConnection{};
  hostClockMs() = 10000UL;
  provisionTransport();
}

// The caller sets up the network first (resetNetwork, then any condition), so a
// condition is never undone by this helper.
void expectRefused(const ResponseSpec& spec, const char* name) {
  int httpStatus = -1;
  std::string body;
  const bool ok = syncWith(spec, httpStatus, body);
  TEST_ASSERT_FALSE_MESSAGE(ok, name);
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, httpStatus, name);
  TEST_ASSERT_TRUE_MESSAGE(body.empty(), name);
}

}  // namespace

void test_client_accepts_a_signed_response() {
  resetNetwork();
  int httpStatus = 0;
  std::string body;
  ResponseSpec spec;
  TEST_ASSERT_TRUE(syncWith(spec, httpStatus, body));
  TEST_ASSERT_EQUAL_INT(200, httpStatus);
  TEST_ASSERT_EQUAL_STRING("{\"ok\":true}", body.c_str());
}

void test_client_reports_a_signed_status_other_than_200() {
  // A 201 that the MAC covers is accepted and reported, so the status is both bound and used.
  resetNetwork();
  ResponseSpec spec;
  spec.status = 201;
  spec.macStatus = 201;
  int httpStatus = 0;
  std::string body;
  TEST_ASSERT_TRUE(syncWith(spec, httpStatus, body));
  TEST_ASSERT_EQUAL_INT(201, httpStatus);
}

void test_client_authenticates_a_body_with_a_nul_byte_by_its_full_length() {
  // A C-string copy stopped at the NUL and then authenticated a truncated body. The body
  // is now copied by its declared length, so the MAC over all five bytes verifies.
  resetNetwork();
  ResponseSpec spec;
  spec.body = std::string("ab") + '\0' + "cd";
  int httpStatus = 0;
  std::string body;
  TEST_ASSERT_TRUE(syncWith(spec, httpStatus, body));
  TEST_ASSERT_EQUAL_INT(200, httpStatus);
  TEST_ASSERT_EQUAL_UINT32(5, body.size());
  TEST_ASSERT_TRUE(body == spec.body);
}

void test_client_refuses_a_repeated_content_length() {
  resetNetwork();
  ResponseSpec spec;
  spec.repeatLength = true;
  expectRefused(spec, "repeated Content-Length");
}

void test_client_refuses_a_repeated_mac_header() {
  resetNetwork();
  ResponseSpec spec;
  spec.repeatMac = true;
  expectRefused(spec, "repeated X-Inventatory-Mac");
}

void test_client_refuses_chunked_transfer_encoding() {
  resetNetwork();
  ResponseSpec spec;
  spec.extraHeaders = "Transfer-Encoding: chunked\r\n";
  expectRefused(spec, "chunked transfer encoding");
}

void test_client_refuses_a_nul_byte_in_the_header_block() {
  resetNetwork();
  // A NUL before a later header line could hide that line from C-string header reads on the device.
  ResponseSpec spec;
  spec.extraHeaders = std::string("Server: x") + '\0' + "y\r\nTransfer-Encoding: chunked\r\n";
  expectRefused(spec, "NUL byte in the header block");
}

void test_client_requires_content_length_to_match_the_body() {
  resetNetwork();
  ResponseSpec longer;
  longer.overrideLength = std::to_string(std::string("{\"ok\":true}").size() + 1U);
  expectRefused(longer, "Content-Length longer than the body");

  ResponseSpec shorter;
  shorter.overrideLength = std::to_string(std::string("{\"ok\":true}").size() - 1U);
  expectRefused(shorter, "Content-Length shorter than the body");
}

void test_client_refuses_a_wrong_mac() {
  resetNetwork();
  ResponseSpec spec;
  spec.macBody = "{\"ok\":false}";
  expectRefused(spec, "MAC over a different body");
}

void test_client_binds_the_status_into_the_mac() {
  resetNetwork();
  ResponseSpec spec;
  spec.macStatus = 201;  // the status line still says 200
  expectRefused(spec, "MAC over a different status");
}

void test_client_requires_the_request_counter_to_be_echoed() {
  resetNetwork();
  ResponseSpec spec;
  spec.overrideCounter = "999999999";
  expectRefused(spec, "counter that was not requested");
}

void test_client_requires_protocol_version_one() {
  resetNetwork();
  ResponseSpec spec;
  spec.overrideProtocol = "2";
  expectRefused(spec, "protocol version 2");
}

void test_client_refuses_a_header_block_over_the_limit() {
  resetNetwork();
  ResponseSpec spec;
  spec.extraHeaders = "X-Pad: " + std::string(kMaxHttpHeaderBytes, 'a') + "\r\n";
  expectRefused(spec, "header block over the limit");
}

// A response whose header block (everything before the blank line) is exactly `headerBytes` long. The
// probe response has the same fields and a counter of 1, which is what the first request of a fresh store uses.
ResponseSpec headerBlockSpec(std::size_t headerBytes) {
  const std::string probe = makeResponse("X-Inventatory-Counter: 1\r\n", ResponseSpec());
  const std::size_t baseHeader = probe.find("\r\n\r\n");
  const std::string padName = "X-Pad: ";
  const std::size_t fixed = padName.size() + 2U;  // the name and its CRLF
  ResponseSpec spec;
  spec.extraHeaders = padName + std::string(headerBytes - baseHeader - fixed, 'a') + "\r\n";
  return spec;
}

void test_client_accepts_a_header_block_of_exactly_the_limit() {
  resetNetwork();
  int httpStatus = 0;
  std::string body;
  TEST_ASSERT_TRUE(syncWith(headerBlockSpec(kMaxHttpHeaderBytes), httpStatus, body));
  TEST_ASSERT_EQUAL_INT(200, httpStatus);
  TEST_ASSERT_EQUAL_STRING("{\"ok\":true}", body.c_str());
}

void test_client_refuses_a_header_block_one_byte_over_the_limit() {
  resetNetwork();
  expectRefused(headerBlockSpec(kMaxHttpHeaderBytes + 1U), "header block one byte over the limit");
}

void test_client_accepts_a_body_of_exactly_the_limit() {
  resetNetwork();
  int httpStatus = 0;
  std::string body;
  ResponseSpec spec;
  spec.body = std::string(kMaxHttpBodyBytes, 'b');
  TEST_ASSERT_TRUE(syncWith(spec, httpStatus, body));
  TEST_ASSERT_EQUAL_INT(200, httpStatus);
  TEST_ASSERT_EQUAL_UINT32(kMaxHttpBodyBytes, body.size());
}

void test_client_refuses_a_body_over_the_limit() {
  resetNetwork();
  ResponseSpec spec;
  spec.body = std::string(kMaxHttpBodyBytes + 1U, 'x');
  expectRefused(spec, "body over the limit");
}

void test_client_refuses_a_response_without_a_blank_line() {
  resetNetwork();
  ResponseSpec spec;
  spec.blankLine = false;
  expectRefused(spec, "no blank line before the body");
}

void test_client_refuses_a_status_line_without_http() {
  resetNetwork();
  // The status cannot be read, so it is 0, and the MAC is over 0. A status below 100 is refused.
  ResponseSpec spec;
  spec.statusLine = "200 OK";
  spec.macStatus = 0;
  expectRefused(spec, "status line without HTTP/");
}

void test_client_times_out_on_a_silent_server() {
  resetNetwork();
  int httpStatus = -1;
  std::string body;
  const unsigned long before = millis();
  HostConnection& net = hostNetwork();
  net.responder = nullptr;
  InventatoryClient client;
  SetupStateStore store;
  store.begin();
  InventatoryClientConfig config;
  config.deviceId = kDeviceId;
  config.token = kToken;
  config.transportCounterStore = &store;
  TEST_ASSERT_TRUE(client.begin(config));
  String responseBody;
  TEST_ASSERT_FALSE(client.postSync(String("{}"), httpStatus, responseBody));
  TEST_ASSERT_EQUAL_INT(0, httpStatus);
  // The response wait is 600 ms on the fake clock.
  TEST_ASSERT_TRUE(millis() - before >= 600UL);
}

void test_client_refuses_when_no_server_is_found() {
  resetNetwork();
  hostNetwork().mdnsFindsServer = false;
  int httpStatus = -1;
  std::string body;
  InventatoryClient client;
  SetupStateStore store;
  store.begin();
  InventatoryClientConfig config;
  config.deviceId = kDeviceId;
  config.token = kToken;
  config.transportCounterStore = &store;
  TEST_ASSERT_TRUE(client.begin(config));
  String responseBody;
  TEST_ASSERT_FALSE(client.postSync(String("{}"), httpStatus, responseBody));
  TEST_ASSERT_EQUAL_INT(0, httpStatus);
}

void test_client_refuses_when_the_connection_fails() {
  resetNetwork();
  hostNetwork().connectSucceeds = false;
  ResponseSpec spec;
  expectRefused(spec, "TCP connection refused");
}

void test_client_refuses_when_wifi_is_down() {
  resetNetwork();
  hostNetwork().wifiConnected = false;
  ResponseSpec spec;
  expectRefused(spec, "Wi-Fi not connected");
  TEST_ASSERT_TRUE(hostNetwork().request.empty());
}

void test_client_gives_every_request_a_higher_counter() {
  resetNetwork();
  InventatoryClient client;
  SetupStateStore store;
  store.begin();
  InventatoryClientConfig config;
  config.deviceId = kDeviceId;
  config.token = kToken;
  config.transportCounterStore = &store;
  TEST_ASSERT_TRUE(client.begin(config));

  unsigned long long previous = 0;
  for (int attempt = 0; attempt < 3; ++attempt) {
    hostNetwork().responded = false;
    hostNetwork().readPos = 0;
    hostNetwork().responder = [](const std::string& request) {
      ResponseSpec spec;
      return makeResponse(request, spec);
    };
    int httpStatus = 0;
    String responseBody;
    TEST_ASSERT_TRUE(client.postSync(String("{}"), httpStatus, responseBody));
    const unsigned long long counter = std::strtoull(counterOf(hostNetwork().request).c_str(), nullptr, 10);
    TEST_ASSERT_TRUE(counter > previous);
    previous = counter;
  }
}

void runHostClientFramingTests() {
  RUN_TEST(test_client_accepts_a_signed_response);
  RUN_TEST(test_client_reports_a_signed_status_other_than_200);
  RUN_TEST(test_client_authenticates_a_body_with_a_nul_byte_by_its_full_length);
  RUN_TEST(test_client_refuses_a_repeated_content_length);
  RUN_TEST(test_client_refuses_a_repeated_mac_header);
  RUN_TEST(test_client_refuses_chunked_transfer_encoding);
  RUN_TEST(test_client_refuses_a_nul_byte_in_the_header_block);
  RUN_TEST(test_client_accepts_a_header_block_of_exactly_the_limit);
  RUN_TEST(test_client_refuses_a_header_block_one_byte_over_the_limit);
  RUN_TEST(test_client_accepts_a_body_of_exactly_the_limit);
  RUN_TEST(test_client_requires_content_length_to_match_the_body);
  RUN_TEST(test_client_refuses_a_wrong_mac);
  RUN_TEST(test_client_binds_the_status_into_the_mac);
  RUN_TEST(test_client_requires_the_request_counter_to_be_echoed);
  RUN_TEST(test_client_requires_protocol_version_one);
  RUN_TEST(test_client_refuses_a_header_block_over_the_limit);
  RUN_TEST(test_client_refuses_a_body_over_the_limit);
  RUN_TEST(test_client_refuses_a_response_without_a_blank_line);
  RUN_TEST(test_client_refuses_a_status_line_without_http);
  RUN_TEST(test_client_times_out_on_a_silent_server);
  RUN_TEST(test_client_refuses_when_no_server_is_found);
  RUN_TEST(test_client_refuses_when_the_connection_fails);
  RUN_TEST(test_client_refuses_when_wifi_is_down);
  RUN_TEST(test_client_gives_every_request_a_higher_counter);
}
