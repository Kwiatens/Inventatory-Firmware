// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

// A corpus of sync responses for the protocol v1 parser. Every malformed document
// must be refused as a whole (AGENTS.md invariant 5, fail-closed parsing), and every
// well-formed document must be accepted with its fields decoded exactly. The corpus
// is shared with the desktop side's round-trip checks (transport v1 specification),
// so each case states what it is for. Runs on the board and on the host.
#include <unity.h>

#include "InventatoryScanHttp.h"
#include "InventatoryScanSync.h"

using namespace inventatory_scan;

void registerSyncResponseCorpusTests();

void test_sync_corpus_rejects_structural_damage();
void test_sync_corpus_rejects_bad_field_values();
void test_sync_corpus_rejects_bad_strings_and_escapes();
void test_sync_corpus_rejects_bad_utf8();
void test_sync_corpus_rejects_bad_numbers_and_nesting();
void test_sync_corpus_rejects_oversized_bodies();
void test_sync_corpus_accepts_well_formed_documents();
void test_sync_corpus_decodes_escapes_and_unicode();
void test_sync_corpus_accepts_the_size_limits_exactly();

namespace {

// The smallest document the parser accepts.
const char kMinimal[] = "{\"protocolVersion\":1,\"requestId\":\"sync-1\",\"acceptedEventIds\":[],\"results\":[]}";

String repeat(char value, std::size_t count) {
  String out;
  for (std::size_t index = 0; index < count; ++index) out += value;
  return out;
}

// A document that is valid except for one extra member, `member`, placed before the closing brace.
String withMember(const String& member) {
  return String("{\"protocolVersion\":1,\"requestId\":\"sync-1\",\"acceptedEventIds\":[],\"results\":[],") +
         member + "}";
}

// A valid document of exactly bodyLength bytes: the padding sits in an unread member.
String documentOfLength(std::size_t bodyLength) {
  const String empty = withMember(String("\"pad\":\"\""));
  return withMember(String("\"pad\":\"") + repeat('a', bodyLength - empty.length()) + "\"");
}

String nestedArrays(unsigned depth) {
  String out;
  for (unsigned index = 0; index < depth; ++index) out += '[';
  for (unsigned index = 0; index < depth; ++index) out += ']';
  return out;
}

void expectRejected(const char* name, const String& body) {
  TEST_ASSERT_FALSE_MESSAGE(parseSyncResponseJson(body).valid, name);
}

void expectAccepted(const char* name, const String& body) {
  TEST_ASSERT_TRUE_MESSAGE(parseSyncResponseJson(body).valid, name);
}

}  // namespace

void test_sync_corpus_rejects_structural_damage() {
  expectRejected("empty body", String());
  expectRejected("top-level array", String("[]"));
  expectRejected("trailing data after the object", String(kMinimal) + "x");
  expectRejected("second object after the first", String(kMinimal) + kMinimal);
  expectRejected("trailing comma in the object", String("{\"protocolVersion\":1,\"requestId\":\"r\",\"acceptedEventIds\":[],\"results\":[],}"));
  expectRejected("missing colon", String("{\"protocolVersion\" 1,\"requestId\":\"r\",\"acceptedEventIds\":[],\"results\":[]}"));
  expectRejected("missing closing brace", String("{\"protocolVersion\":1,\"requestId\":\"r\",\"acceptedEventIds\":[],\"results\":[]"));
  expectRejected("duplicate key", String("{\"protocolVersion\":1,\"protocolVersion\":1,\"requestId\":\"r\",\"acceptedEventIds\":[],\"results\":[]}"));
  expectRejected("duplicate key in an unread member",
                 withMember(String("\"extra\":1,\"extra\":2")));
  expectRejected("byte order mark before the object", String("\xEF\xBB\xBF") + kMinimal);
  expectRejected("NaN literal in an unread member", withMember(String("\"extra\":NaN")));
  expectRejected("Infinity literal in an unread member", withMember(String("\"extra\":Infinity")));
}

void test_sync_corpus_rejects_bad_field_values() {
  expectRejected("protocol version 2",
                 String("{\"protocolVersion\":2,\"requestId\":\"r\",\"acceptedEventIds\":[],\"results\":[]}"));
  expectRejected("protocol version as a string",
                 String("{\"protocolVersion\":\"1\",\"requestId\":\"r\",\"acceptedEventIds\":[],\"results\":[]}"));
  expectRejected("protocol version with a fraction",
                 String("{\"protocolVersion\":1.0,\"requestId\":\"r\",\"acceptedEventIds\":[],\"results\":[]}"));
  expectRejected("empty request id",
                 String("{\"protocolVersion\":1,\"requestId\":\"\",\"acceptedEventIds\":[],\"results\":[]}"));
  expectRejected("missing request id",
                 String("{\"protocolVersion\":1,\"acceptedEventIds\":[],\"results\":[]}"));
  expectRejected("accepted id repeated",
                 String("{\"protocolVersion\":1,\"requestId\":\"r\",\"acceptedEventIds\":[\"a\",\"a\"],\"results\":[]}"));
  expectRejected("accepted ids beyond the batch size",
                 String("{\"protocolVersion\":1,\"requestId\":\"r\",\"acceptedEventIds\":[\"a\",\"b\",\"c\",\"d\",\"e\"],\"results\":[]}"));
  expectRejected("empty accepted id",
                 String("{\"protocolVersion\":1,\"requestId\":\"r\",\"acceptedEventIds\":[\"\"],\"results\":[]}"));
  expectRejected("accepted id that is not a string",
                 String("{\"protocolVersion\":1,\"requestId\":\"r\",\"acceptedEventIds\":[7],\"results\":[]}"));
  expectRejected("results missing",
                 String("{\"protocolVersion\":1,\"requestId\":\"r\",\"acceptedEventIds\":[]}"));
  expectRejected("result status outside the allowlist",
                 String("{\"protocolVersion\":1,\"requestId\":\"r\",\"acceptedEventIds\":[],\"results\":[{\"resultId\":\"x\",\"eventId\":\"e\",\"status\":\"pending\"}]}"));
  expectRejected("result without an event id",
                 String("{\"protocolVersion\":1,\"requestId\":\"r\",\"acceptedEventIds\":[],\"results\":[{\"resultId\":\"x\",\"status\":\"completed\"}]}"));
  expectRejected("result that is not an object",
                 String("{\"protocolVersion\":1,\"requestId\":\"r\",\"acceptedEventIds\":[],\"results\":[\"completed\"]}"));
  expectRejected("lookup status outside the allowlist",
                 String("{\"protocolVersion\":1,\"requestId\":\"r\",\"acceptedEventIds\":[],\"results\":[],\"lookupResult\":{\"lookupId\":\"l\",\"status\":\"maybe\"}}"));
  expectRejected("lookup id empty",
                 String("{\"protocolVersion\":1,\"requestId\":\"r\",\"acceptedEventIds\":[],\"results\":[],\"lookupResult\":{\"lookupId\":\"\",\"status\":\"found\"}}"));
  expectRejected("quick label revision zero",
                 String("{\"protocolVersion\":1,\"requestId\":\"r\",\"acceptedEventIds\":[],\"results\":[],\"quickLabels\":{\"revision\":0,\"presets\":[]}}"));
  expectRejected("quick label print status outside the allowlist",
                 String("{\"protocolVersion\":1,\"requestId\":\"r\",\"acceptedEventIds\":[],\"results\":[],\"quickLabelPrintResult\":{\"requestId\":\"p\",\"status\":\"queued\"}}"));
  expectRejected("quick label print without a request id",
                 String("{\"protocolVersion\":1,\"requestId\":\"r\",\"acceptedEventIds\":[],\"results\":[],\"quickLabelPrintResult\":{\"status\":\"completed\"}}"));
}

void test_sync_corpus_rejects_bad_strings_and_escapes() {
  expectRejected("raw control byte 0x01 in a string", withMember(String("\"extra\":\"a") + String(static_cast<char>(0x01)) + "b\""));
  expectRejected("raw newline in a string", withMember(String("\"extra\":\"a\nb\"")));
  expectRejected("unknown escape \\x", withMember(String("\"extra\":\"\\x41\"")));
  expectRejected("lone high surrogate", withMember(String("\"extra\":\"\\uD800\"")));
  expectRejected("lone low surrogate", withMember(String("\"extra\":\"\\uDC00\"")));
  expectRejected("high surrogate followed by a non-surrogate", withMember(String("\"extra\":\"\\uD800\\u0041\"")));
  expectRejected("NUL escape in a decoded field",
                 String("{\"protocolVersion\":1,\"requestId\":\"r\\u0000x\",\"acceptedEventIds\":[],\"results\":[]}"));
  expectRejected("NUL escape in an unread field", withMember(String("\"extra\":\"a\\u0000b\"")));
  expectRejected("NUL escape in an unread key", withMember(String("\"\\u0000\":1")));
  expectRejected("NUL escape inside an unread nested object", withMember(String("\"extra\":{\"\\u0000\":1}")));
  expectRejected("unterminated string", withMember(String("\"extra\":\"abc")));
  expectRejected("escape at the very end of the body", String(kMinimal).substring(0, 20) + "\\");
}

void test_sync_corpus_rejects_bad_utf8() {
  // Overlong encoding of '/' (C0 AF) and of NUL (C0 80).
  expectRejected("overlong slash", withMember(String("\"extra\":\"") + String(static_cast<char>(0xC0)) + String(static_cast<char>(0xAF)) + "\""));
  expectRejected("overlong NUL", withMember(String("\"extra\":\"") + String(static_cast<char>(0xC0)) + String(static_cast<char>(0x80)) + "\""));
  // UTF-8 encoded surrogate U+D800 (ED A0 80).
  expectRejected("encoded surrogate", withMember(String("\"extra\":\"") + String(static_cast<char>(0xED)) +
                                                    String(static_cast<char>(0xA0)) + String(static_cast<char>(0x80)) + "\""));
  // A lead byte with no continuation before the closing quote.
  expectRejected("truncated sequence", withMember(String("\"extra\":\"") + String(static_cast<char>(0xE2)) + "\""));
  // A continuation byte with no lead byte.
  expectRejected("stray continuation byte", withMember(String("\"extra\":\"") + String(static_cast<char>(0x80)) + "\""));
  // Above U+10FFFF (F4 90 80 80).
  expectRejected("code point above U+10FFFF",
                 withMember(String("\"extra\":\"") + String(static_cast<char>(0xF4)) + String(static_cast<char>(0x90)) +
                            String(static_cast<char>(0x80)) + String(static_cast<char>(0x80)) + "\""));
}

void test_sync_corpus_rejects_bad_numbers_and_nesting() {
  expectRejected("integer above the int range",
                 String("{\"protocolVersion\":1,\"requestId\":\"r\",\"acceptedEventIds\":[],\"results\":[{\"resultId\":\"x\",\"eventId\":\"e\",\"status\":\"completed\",\"quantity\":2147483648}]}"));
  expectRejected("leading zero in a number", withMember(String("\"extra\":01")));
  expectRejected("bare minus sign", withMember(String("\"extra\":-")));
  expectRejected("exponent without digits", withMember(String("\"extra\":1e")));
  expectRejected("trailing comma in an array", withMember(String("\"extra\":[1,]")));
  expectRejected("nesting one level past the limit", withMember(String("\"extra\":") + nestedArrays(17)));
}

void test_sync_corpus_rejects_oversized_bodies() {
  const String oversized = documentOfLength(kMaxHttpBodyBytes + 1U);
  TEST_ASSERT_EQUAL_UINT32(kMaxHttpBodyBytes + 1U, oversized.length());
  expectRejected("body one byte over the limit", oversized);
}

void test_sync_corpus_accepts_well_formed_documents() {
  expectAccepted("minimal document", String(kMinimal));
  expectAccepted("unknown members of every kind",
                 withMember(String("\"a\":[1,2.5,-3e2,true,false,null,\"x\"],\"b\":{\"c\":{\"d\":[]}}")));
  expectAccepted("whitespace between tokens",
                 String("{ \"protocolVersion\" :\t1 ,\r\n\"requestId\" : \"r\" , \"acceptedEventIds\" : [ ] ,\n\"results\" : [ ] }\n"));
  expectAccepted("null optional members",
                 String("{\"protocolVersion\":1,\"requestId\":\"r\",\"acceptedEventIds\":[],\"results\":[],\"lookupResult\":null,\"quickLabels\":null,\"quickLabelPrintResult\":null}"));
  expectAccepted("nesting exactly at the limit", withMember(String("\"extra\":") + nestedArrays(16)));
  expectAccepted("raw UTF-8 in a string", withMember(String("\"extra\":\"za\xC5\xBC\xC3\xB3\xC5\x82\xC4\x87\"")));
  expectAccepted("surrogate pair decoded from escapes", withMember(String("\"extra\":\"\\uD83D\\uDE00\"")));
}

void test_sync_corpus_decodes_escapes_and_unicode() {
  const SyncResponse escaped = parseSyncResponseJson(
      "{\"protocolVersion\":1,\"requestId\":\"a\\\"b\\\\c\\/d\\b\\f\\n\\r\\t\",\"acceptedEventIds\":[],\"results\":[]}");
  TEST_ASSERT_TRUE(escaped.valid);
  TEST_ASSERT_EQUAL_STRING("a\"b\\c/d\b\f\n\r\t", escaped.requestId.c_str());

  // U+1F600 is four UTF-8 bytes: F0 9F 98 80.
  const SyncResponse emoji = parseSyncResponseJson(
      "{\"protocolVersion\":1,\"requestId\":\"r\",\"acceptedEventIds\":[],\"results\":[],\"lookupResult\":"
      "{\"lookupId\":\"l\",\"status\":\"found\",\"itemName\":\"\\uD83D\\uDE00\"}}");
  TEST_ASSERT_TRUE(emoji.valid);
  TEST_ASSERT_EQUAL_UINT32(4, emoji.lookupItemName.length());
  TEST_ASSERT_EQUAL_UINT8(0xF0, static_cast<uint8_t>(emoji.lookupItemName[0]));
  TEST_ASSERT_EQUAL_UINT8(0x80, static_cast<uint8_t>(emoji.lookupItemName[3]));
}

void test_sync_corpus_accepts_the_size_limits_exactly() {
  const String exact = documentOfLength(kMaxHttpBodyBytes);
  TEST_ASSERT_EQUAL_UINT32(kMaxHttpBodyBytes, exact.length());
  expectAccepted("body exactly at the limit", exact);
}

void registerSyncResponseCorpusTests() {
  RUN_TEST(test_sync_corpus_rejects_structural_damage);
  RUN_TEST(test_sync_corpus_rejects_bad_field_values);
  RUN_TEST(test_sync_corpus_rejects_bad_strings_and_escapes);
  RUN_TEST(test_sync_corpus_rejects_bad_utf8);
  RUN_TEST(test_sync_corpus_rejects_bad_numbers_and_nesting);
  RUN_TEST(test_sync_corpus_rejects_oversized_bodies);
  RUN_TEST(test_sync_corpus_accepts_well_formed_documents);
  RUN_TEST(test_sync_corpus_decodes_escapes_and_unicode);
  RUN_TEST(test_sync_corpus_accepts_the_size_limits_exactly);
}
