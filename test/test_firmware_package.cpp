// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

// Signed update packages: each check of docs/ota-protocol.md refuses its own bad vector, and the
// image hash catches a changed, short or long image. Vectors: firmware_package_vectors.h.
#include <unity.h>

#include "InventatoryScanFirmwarePackage.h"
#include "firmware_package_vectors.h"

#include <cstring>

using namespace inventatory_scan;
using namespace firmware_vectors;

void registerFirmwarePackageTests();

void test_firmware_sha256_matches_the_fips_vector();
void test_firmware_package_accepts_the_valid_vector();
void test_firmware_package_refuses_a_changed_signature_or_header();
void test_firmware_package_refuses_an_unknown_key();
void test_firmware_package_accepts_the_second_key_slot();
void test_firmware_package_refuses_everything_without_a_key();
void test_firmware_package_refuses_the_wrong_product_or_hardware();
void test_firmware_package_refuses_a_lower_security_counter();
void test_firmware_package_refuses_an_oversized_image();
void test_firmware_package_refuses_set_reserved_bytes();
void test_firmware_package_refuses_a_truncated_or_foreign_prefix();
void test_firmware_image_verifier_accepts_the_image_in_chunks();
void test_firmware_image_verifier_refuses_a_truncated_image();
void test_firmware_image_verifier_refuses_extra_bytes();
void test_firmware_image_verifier_refuses_a_changed_byte();
void test_firmware_offer_json_parses_the_package();
void test_firmware_offer_json_refuses_malformed_offers();
void test_firmware_trusted_key_from_hex();
void test_firmware_chunk_lengths_cover_the_image();
void test_firmware_offer_request_lists_the_trusted_key_ids();

namespace {

uint8_t testImageByte(uint32_t index) { return static_cast<uint8_t>((index * 31U + 7U) & 0xffU); }

FirmwareTrustedKey keyFrom(const uint8_t (&publicKey)[kFirmwarePublicKeyBytes]) {
  FirmwareTrustedKey key;
  key.present = true;
  std::memcpy(key.publicKey, publicKey, sizeof(key.publicKey));
  return key;
}

// Static, so the tests stay small on the board's 8 KiB loop stack.
FirmwareTrustedKey gKeys[kFirmwareTrustedKeySlots];
uint8_t gPrefix[kFirmwarePackagePrefixBytes];
FirmwarePackageHeader gHeader;

FirmwareInstallPolicy policyWith(const FirmwareTrustedKey& slot0, const FirmwareTrustedKey& slot1,
                                 uint32_t minimumCounter = kVectorSecurityCounter) {
  gKeys[0] = slot0;
  gKeys[1] = slot1;
  FirmwareInstallPolicy policy;
  policy.product = "Inventatory-Scan-R1";
  policy.hardware = "waveshare-esp32s3-nano";
  policy.minimumSecurityCounter = minimumCounter;
  policy.partitionBytes = 0x190000U;
  policy.keys = gKeys;
  policy.keyCount = kFirmwareTrustedKeySlots;
  return policy;
}

FirmwareInstallPolicy policyA(uint32_t minimumCounter = kVectorSecurityCounter) {
  return policyWith(keyFrom(kVectorPublicKeyA), FirmwareTrustedKey{}, minimumCounter);
}

FirmwareCheck check(const uint8_t (&prefix)[kFirmwarePackagePrefixBytes], const FirmwareInstallPolicy& policy) {
  std::memcpy(gPrefix, prefix, sizeof(gPrefix));
  return checkFirmwarePackagePrefix(gPrefix, sizeof(gPrefix), policy, gHeader);
}

// Streams `length` image bytes in pieces of at most `piece`, with byte `changed` flipped if in range.
bool streamImage(FirmwareImageVerifier& verifier, uint32_t length, uint32_t piece, uint32_t changed = UINT32_MAX) {
  uint8_t buffer[256];
  uint32_t offset = 0;
  while (offset < length) {
    uint32_t count = length - offset;
    if (count > piece) count = piece;
    if (count > sizeof(buffer)) count = sizeof(buffer);
    for (uint32_t index = 0; index < count; ++index) {
      buffer[index] = testImageByte(offset + index);
      if (offset + index == changed) buffer[index] ^= 0x01U;
    }
    if (!verifier.accept(buffer, count)) return false;
    offset += count;
  }
  return true;
}

}  // namespace

void test_firmware_sha256_matches_the_fips_vector() {
  static const uint8_t kExpected[kFirmwareSha256Bytes] = {
      0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
      0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad};
  uint8_t digest[kFirmwareSha256Bytes]{};
  TEST_ASSERT_TRUE(firmwareSha256(reinterpret_cast<const uint8_t*>("abc"), 3U, digest));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(kExpected, digest, sizeof(digest));
}

void test_firmware_package_accepts_the_valid_vector() {
  TEST_ASSERT_EQUAL(static_cast<int>(FirmwareCheck::Ok), static_cast<int>(check(kVectorValid, policyA())));
  TEST_ASSERT_EQUAL_STRING("Inventatory-Scan-R1", gHeader.product);
  TEST_ASSERT_EQUAL_STRING("waveshare-esp32s3-nano", gHeader.hardware);
  TEST_ASSERT_EQUAL_STRING(kVectorVersion, gHeader.version);
  TEST_ASSERT_EQUAL_UINT32(kVectorSecurityCounter, gHeader.securityCounter);
  TEST_ASSERT_EQUAL_UINT32(kVectorImageSize, gHeader.imageSize);
  uint8_t keyId[kFirmwareKeyIdBytes]{};
  TEST_ASSERT_TRUE(firmwareKeyId(kVectorPublicKeyA, keyId));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(keyId, gHeader.keyId, sizeof(keyId));
}

void test_firmware_package_refuses_a_changed_signature_or_header() {
  const auto policy = policyA();
  // One byte in r, one in s, one in the version text, one in the image hash.
  const std::size_t positions[] = {kFirmwareHeaderBytes, kFirmwareHeaderBytes + 40U, firmware_header_offset::kVersion,
                                   firmware_header_offset::kImageSha256 + 5U};
  for (const auto position : positions) {
    std::memcpy(gPrefix, kVectorValid, sizeof(gPrefix));
    gPrefix[position] ^= 0x01U;
    TEST_ASSERT_EQUAL(static_cast<int>(FirmwareCheck::BadSignature),
                      static_cast<int>(checkFirmwarePackagePrefix(gPrefix, sizeof(gPrefix), policy, gHeader)));
    TEST_ASSERT_EQUAL_UINT32(0U, gHeader.imageSize);
  }
}

void test_firmware_package_refuses_an_unknown_key() {
  TEST_ASSERT_EQUAL(static_cast<int>(FirmwareCheck::BadSignature), static_cast<int>(check(kVectorSignedByB, policyA())));
  const auto onlyB = policyWith(keyFrom(kVectorPublicKeyB), FirmwareTrustedKey{});
  TEST_ASSERT_EQUAL(static_cast<int>(FirmwareCheck::BadSignature), static_cast<int>(check(kVectorValid, onlyB)));
  // The key id names key A, but the signature is by key B: rewriting the id must not help.
  std::memcpy(gPrefix, kVectorSignedByB, sizeof(gPrefix));
  uint8_t idA[kFirmwareKeyIdBytes]{};
  TEST_ASSERT_TRUE(firmwareKeyId(kVectorPublicKeyA, idA));
  std::memcpy(gPrefix + firmware_header_offset::kKeyId, idA, sizeof(idA));
  TEST_ASSERT_EQUAL(static_cast<int>(FirmwareCheck::BadSignature),
                    static_cast<int>(checkFirmwarePackagePrefix(gPrefix, sizeof(gPrefix), policyA(), gHeader)));
}

void test_firmware_package_accepts_the_second_key_slot() {
  const auto both = policyWith(keyFrom(kVectorPublicKeyA), keyFrom(kVectorPublicKeyB));
  TEST_ASSERT_EQUAL(static_cast<int>(FirmwareCheck::Ok), static_cast<int>(check(kVectorSignedByB, both)));
  TEST_ASSERT_EQUAL(static_cast<int>(FirmwareCheck::Ok), static_cast<int>(check(kVectorValid, both)));
}

void test_firmware_package_refuses_everything_without_a_key() {
  const auto none = policyWith(FirmwareTrustedKey{}, FirmwareTrustedKey{});
  TEST_ASSERT_EQUAL(static_cast<int>(FirmwareCheck::BadSignature), static_cast<int>(check(kVectorValid, none)));
  auto noArray = policyA();
  noArray.keys = nullptr;
  TEST_ASSERT_EQUAL(static_cast<int>(FirmwareCheck::BadSignature), static_cast<int>(check(kVectorValid, noArray)));
}

void test_firmware_package_refuses_the_wrong_product_or_hardware() {
  TEST_ASSERT_EQUAL(static_cast<int>(FirmwareCheck::WrongDevice), static_cast<int>(check(kVectorWrongProduct, policyA())));
  TEST_ASSERT_EQUAL(static_cast<int>(FirmwareCheck::WrongDevice), static_cast<int>(check(kVectorWrongHardware, policyA())));
  TEST_ASSERT_EQUAL_UINT32(0U, gHeader.imageSize);
}

void test_firmware_package_refuses_a_lower_security_counter() {
  TEST_ASSERT_EQUAL(static_cast<int>(FirmwareCheck::OlderRelease), static_cast<int>(check(kVectorCounterOne, policyA(2U))));
  TEST_ASSERT_EQUAL(static_cast<int>(FirmwareCheck::Ok), static_cast<int>(check(kVectorCounterOne, policyA(1U))));
  // Equal is allowed, so an ordinary release can be reinstalled.
  TEST_ASSERT_EQUAL(static_cast<int>(FirmwareCheck::Ok), static_cast<int>(check(kVectorValid, policyA(2U))));
  TEST_ASSERT_EQUAL(static_cast<int>(FirmwareCheck::OlderRelease), static_cast<int>(check(kVectorValid, policyA(3U))));
}

void test_firmware_package_refuses_an_oversized_image() {
  TEST_ASSERT_EQUAL(static_cast<int>(FirmwareCheck::TooLarge), static_cast<int>(check(kVectorOversized, policyA())));
  auto small = policyA();
  small.partitionBytes = kVectorImageSize - 1U;
  TEST_ASSERT_EQUAL(static_cast<int>(FirmwareCheck::TooLarge), static_cast<int>(check(kVectorValid, small)));
  small.partitionBytes = kVectorImageSize;
  TEST_ASSERT_EQUAL(static_cast<int>(FirmwareCheck::Ok), static_cast<int>(check(kVectorValid, small)));
}

void test_firmware_package_refuses_set_reserved_bytes() {
  TEST_ASSERT_EQUAL(static_cast<int>(FirmwareCheck::BadHeader), static_cast<int>(check(kVectorReservedSet, policyA())));
}

void test_firmware_package_refuses_a_truncated_or_foreign_prefix() {
  const auto policy = policyA();
  std::memcpy(gPrefix, kVectorValid, sizeof(gPrefix));
  TEST_ASSERT_EQUAL(static_cast<int>(FirmwareCheck::BadHeader),
                    static_cast<int>(checkFirmwarePackagePrefix(gPrefix, sizeof(gPrefix) - 1U, policy, gHeader)));
  TEST_ASSERT_EQUAL(static_cast<int>(FirmwareCheck::BadHeader),
                    static_cast<int>(checkFirmwarePackagePrefix(nullptr, sizeof(gPrefix), policy, gHeader)));
  const std::size_t positions[] = {firmware_header_offset::kMagic, firmware_header_offset::kFormat,
                                   firmware_header_offset::kHeaderSize};
  for (const auto position : positions) {
    std::memcpy(gPrefix, kVectorValid, sizeof(gPrefix));
    gPrefix[position] ^= 0x01U;
    TEST_ASSERT_EQUAL(static_cast<int>(FirmwareCheck::BadHeader),
                      static_cast<int>(checkFirmwarePackagePrefix(gPrefix, sizeof(gPrefix), policy, gHeader)));
  }
}

void test_firmware_image_verifier_accepts_the_image_in_chunks() {
  TEST_ASSERT_EQUAL(static_cast<int>(FirmwareCheck::Ok), static_cast<int>(check(kVectorValid, policyA())));
  FirmwareImageVerifier verifier;
  TEST_ASSERT_TRUE(verifier.begin(gHeader));
  TEST_ASSERT_TRUE(streamImage(verifier, kVectorImageSize, 200U));
  TEST_ASSERT_EQUAL_UINT32(0U, verifier.remaining());
  TEST_ASSERT_EQUAL(static_cast<int>(FirmwareCheck::Ok), static_cast<int>(verifier.finish()));
  // finish() ends the stream; a second call cannot pass.
  TEST_ASSERT_EQUAL(static_cast<int>(FirmwareCheck::DownloadFailed), static_cast<int>(verifier.finish()));
}

void test_firmware_image_verifier_refuses_a_truncated_image() {
  TEST_ASSERT_EQUAL(static_cast<int>(FirmwareCheck::Ok), static_cast<int>(check(kVectorValid, policyA())));
  FirmwareImageVerifier verifier;
  TEST_ASSERT_TRUE(verifier.begin(gHeader));
  TEST_ASSERT_TRUE(streamImage(verifier, kVectorImageSize - 1U, 256U));
  TEST_ASSERT_EQUAL(static_cast<int>(FirmwareCheck::DownloadFailed), static_cast<int>(verifier.finish()));
}

void test_firmware_image_verifier_refuses_extra_bytes() {
  TEST_ASSERT_EQUAL(static_cast<int>(FirmwareCheck::Ok), static_cast<int>(check(kVectorValid, policyA())));
  FirmwareImageVerifier verifier;
  TEST_ASSERT_TRUE(verifier.begin(gHeader));
  TEST_ASSERT_TRUE(streamImage(verifier, kVectorImageSize, 256U));
  const uint8_t extra = 0;
  TEST_ASSERT_FALSE(verifier.accept(&extra, 1U));
  TEST_ASSERT_EQUAL(static_cast<int>(FirmwareCheck::DownloadFailed), static_cast<int>(verifier.finish()));
}

void test_firmware_image_verifier_refuses_a_changed_byte() {
  TEST_ASSERT_EQUAL(static_cast<int>(FirmwareCheck::Ok), static_cast<int>(check(kVectorValid, policyA())));
  FirmwareImageVerifier verifier;
  TEST_ASSERT_TRUE(verifier.begin(gHeader));
  TEST_ASSERT_TRUE(streamImage(verifier, kVectorImageSize, 256U, kVectorImageSize - 1U));
  TEST_ASSERT_EQUAL(static_cast<int>(FirmwareCheck::HashMismatch), static_cast<int>(verifier.finish()));
}

void test_firmware_offer_json_parses_the_package() {
  const String body = String("{\"available\":true,\"package\":\"") +
                      transportHex(kVectorValid, sizeof(kVectorValid)) + "\"}";
  const auto offer = parseFirmwareOfferJson(body);
  TEST_ASSERT_TRUE(offer.valid);
  TEST_ASSERT_TRUE(offer.available);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(kVectorValid, offer.prefix, sizeof(kVectorValid));

  const auto none = parseFirmwareOfferJson("{\"available\":false}");
  TEST_ASSERT_TRUE(none.valid);
  TEST_ASSERT_FALSE(none.available);
}

void test_firmware_offer_json_refuses_malformed_offers() {
  const String shortHex = transportHex(kVectorValid, sizeof(kVectorValid) - 1U);
  String badHex = transportHex(kVectorValid, sizeof(kVectorValid));
  badHex.setCharAt(10, 'g');
  const String bodies[] = {
      "",
      "[]",
      "{}",
      "{\"available\":\"yes\"}",
      "{\"available\":true}",
      String("{\"available\":true,\"package\":\"") + shortHex + "\"}",
      String("{\"available\":true,\"package\":\"") + badHex + "\"}",
      "{\"available\":true,\"available\":false}",
  };
  for (const auto& body : bodies) {
    const auto offer = parseFirmwareOfferJson(body);
    TEST_ASSERT_FALSE_MESSAGE(offer.valid, body.c_str());
    TEST_ASSERT_FALSE(offer.available);
  }
}

void test_firmware_trusted_key_from_hex() {
  const String hex = transportHex(kVectorPublicKeyA, sizeof(kVectorPublicKeyA));
  const auto key = firmwareTrustedKeyFromHex(hex.c_str());
  TEST_ASSERT_TRUE(key.present);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(kVectorPublicKeyA, key.publicKey, sizeof(kVectorPublicKeyA));
  TEST_ASSERT_FALSE(firmwareTrustedKeyFromHex("").present);
  TEST_ASSERT_FALSE(firmwareTrustedKeyFromHex(nullptr).present);
  TEST_ASSERT_FALSE(firmwareTrustedKeyFromHex(hex.substring(2).c_str()).present);
  String compressed = hex;
  compressed.setCharAt(1, '2');
  TEST_ASSERT_FALSE(firmwareTrustedKeyFromHex(compressed.c_str()).present);
}

void test_firmware_chunk_lengths_cover_the_image() {
  uint32_t offset = 0;
  uint32_t requests = 0;
  while (const uint32_t length = nextFirmwareChunkLength(kVectorImageSize, offset)) {
    TEST_ASSERT_TRUE(length <= kMaxFirmwareChunkBytes);
    offset += length;
    ++requests;
  }
  TEST_ASSERT_EQUAL_UINT32(kVectorImageSize, offset);
  TEST_ASSERT_EQUAL_UINT32(3U, requests);
  TEST_ASSERT_EQUAL_UINT32(0U, nextFirmwareChunkLength(kVectorImageSize, kVectorImageSize + 1U));
}

void test_firmware_offer_request_lists_the_trusted_key_ids() {
  gKeys[0] = FirmwareTrustedKey{};
  gKeys[1] = keyFrom(kVectorPublicKeyB);
  uint8_t idB[kFirmwareKeyIdBytes]{};
  TEST_ASSERT_TRUE(firmwareKeyId(kVectorPublicKeyB, idB));
  const auto body = buildFirmwareOfferRequestJson("Inventatory-Scan-R1", "waveshare-esp32s3-nano", "0.1.0", 1U, gKeys,
                                                  kFirmwareTrustedKeySlots);
  const String expected = String("{\"product\":\"Inventatory-Scan-R1\",\"hardware\":\"waveshare-esp32s3-nano\","
                                 "\"version\":\"0.1.0\",\"securityCounter\":1,\"keyIds\":[\"") +
                          transportHex(idB, sizeof(idB)) + "\"]}";
  TEST_ASSERT_EQUAL_STRING(expected.c_str(), body.c_str());
}

void registerFirmwarePackageTests() {
  RUN_TEST(test_firmware_sha256_matches_the_fips_vector);
  RUN_TEST(test_firmware_package_accepts_the_valid_vector);
  RUN_TEST(test_firmware_package_refuses_a_changed_signature_or_header);
  RUN_TEST(test_firmware_package_refuses_an_unknown_key);
  RUN_TEST(test_firmware_package_accepts_the_second_key_slot);
  RUN_TEST(test_firmware_package_refuses_everything_without_a_key);
  RUN_TEST(test_firmware_package_refuses_the_wrong_product_or_hardware);
  RUN_TEST(test_firmware_package_refuses_a_lower_security_counter);
  RUN_TEST(test_firmware_package_refuses_an_oversized_image);
  RUN_TEST(test_firmware_package_refuses_set_reserved_bytes);
  RUN_TEST(test_firmware_package_refuses_a_truncated_or_foreign_prefix);
  RUN_TEST(test_firmware_image_verifier_accepts_the_image_in_chunks);
  RUN_TEST(test_firmware_image_verifier_refuses_a_truncated_image);
  RUN_TEST(test_firmware_image_verifier_refuses_extra_bytes);
  RUN_TEST(test_firmware_image_verifier_refuses_a_changed_byte);
  RUN_TEST(test_firmware_offer_json_parses_the_package);
  RUN_TEST(test_firmware_offer_json_refuses_malformed_offers);
  RUN_TEST(test_firmware_trusted_key_from_hex);
  RUN_TEST(test_firmware_chunk_lengths_cover_the_image);
  RUN_TEST(test_firmware_offer_request_lists_the_trusted_key_ids);
}
