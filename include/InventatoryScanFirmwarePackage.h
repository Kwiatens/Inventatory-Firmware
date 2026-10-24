// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

// Signed update package (.ota): a 256-byte header, an ECDSA P-256 signature over it, then the app image.
// The format, the pull protocol and the order of the checks are in docs/ota-protocol.md.
#pragma once

#include "InventatoryScanFirmwareCrypto.h"
#include "InventatoryScanSecurity.h"
#include "InventatoryScanSync.h"

#include <Arduino.h>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace inventatory_scan {

constexpr char kFirmwarePackageMagic[4] = {'I', 'V', 'O', 'T'};
constexpr uint16_t kFirmwarePackageFormat = 1U;
constexpr std::size_t kFirmwareHeaderBytes = 256U;
constexpr std::size_t kFirmwarePackagePrefixBytes = kFirmwareHeaderBytes + kFirmwareSignatureBytes;
constexpr std::size_t kFirmwareKeyIdBytes = 8U;
constexpr std::size_t kFirmwareTextFieldBytes = 32U;
constexpr std::size_t kFirmwareTrustedKeySlots = 2U;
constexpr std::size_t kMaxFirmwareChunkBytes = 8192U;

namespace firmware_header_offset {
constexpr std::size_t kMagic = 0U;
constexpr std::size_t kFormat = 4U;
constexpr std::size_t kHeaderSize = 6U;
constexpr std::size_t kKeyId = 8U;
constexpr std::size_t kProduct = 16U;
constexpr std::size_t kHardware = 48U;
constexpr std::size_t kVersion = 80U;
constexpr std::size_t kSecurityCounter = 112U;
constexpr std::size_t kImageSize = 116U;
constexpr std::size_t kImageSha256 = 120U;
constexpr std::size_t kReserved = 152U;
}  // namespace firmware_header_offset

static_assert(firmware_header_offset::kReserved == firmware_header_offset::kImageSha256 + kFirmwareSha256Bytes,
              "reserved bytes follow the image hash");

struct FirmwarePackageHeader {
  uint8_t keyId[kFirmwareKeyIdBytes]{};
  char product[kFirmwareTextFieldBytes]{};
  char hardware[kFirmwareTextFieldBytes]{};
  char version[kFirmwareTextFieldBytes]{};
  uint32_t securityCounter = 0;
  uint32_t imageSize = 0;
  uint8_t imageSha256[kFirmwareSha256Bytes]{};
};

// One key slot; an empty slot has present == false.
struct FirmwareTrustedKey {
  bool present = false;
  uint8_t publicKey[kFirmwarePublicKeyBytes]{};
};

// What the running firmware accepts. The minimum counter is the larger of the running image's
// own counter and the one stored in NVS after a confirmed update.
struct FirmwareInstallPolicy {
  const char* product = "";
  const char* hardware = "";
  uint32_t minimumSecurityCounter = 0;
  uint32_t partitionBytes = 0;
  const FirmwareTrustedKey* keys = nullptr;
  std::size_t keyCount = 0;
};

enum class FirmwareCheck : uint8_t {
  Ok,
  BadHeader,
  BadSignature,
  WrongDevice,
  OlderRelease,
  TooLarge,
  DownloadFailed,
  HashMismatch,
};

// The reason word reported to the desktop in the status request.
inline const char* firmwareCheckReason(FirmwareCheck check) {
  switch (check) {
    case FirmwareCheck::Ok:
      return "";
    case FirmwareCheck::BadHeader:
      return "bad_header";
    case FirmwareCheck::BadSignature:
      return "bad_signature";
    case FirmwareCheck::WrongDevice:
      return "wrong_device";
    case FirmwareCheck::OlderRelease:
      return "older_release";
    case FirmwareCheck::TooLarge:
      return "too_large";
    case FirmwareCheck::DownloadFailed:
      return "download_failed";
    case FirmwareCheck::HashMismatch:
      return "hash_mismatch";
  }
  return "bad_header";
}

// Short text for the LCD, under the OTA-01 code.
inline const char* firmwareCheckDisplayText(FirmwareCheck check) {
  switch (check) {
    case FirmwareCheck::BadSignature:
      return "Not signed";
    case FirmwareCheck::WrongDevice:
      return "Wrong device";
    case FirmwareCheck::OlderRelease:
      return "Older release";
    case FirmwareCheck::TooLarge:
      return "Image too large";
    case FirmwareCheck::DownloadFailed:
      return "Download failed";
    case FirmwareCheck::HashMismatch:
      return "Image corrupt";
    case FirmwareCheck::Ok:
    case FirmwareCheck::BadHeader:
      break;
  }
  return "Bad update file";
}

inline uint16_t firmwareReadU16(const uint8_t* bytes) {
  return static_cast<uint16_t>(bytes[0] | (static_cast<uint16_t>(bytes[1]) << 8U));
}

inline uint32_t firmwareReadU32(const uint8_t* bytes) {
  return static_cast<uint32_t>(bytes[0]) | (static_cast<uint32_t>(bytes[1]) << 8U) |
         (static_cast<uint32_t>(bytes[2]) << 16U) | (static_cast<uint32_t>(bytes[3]) << 24U);
}

// A text field is printable ASCII, NUL-terminated inside the field, then zero to the end. An empty
// field is refused: every field names something.
inline bool readFirmwareTextField(const uint8_t* field, char (&out)[kFirmwareTextFieldBytes]) {
  std::size_t length = 0;
  while (length < kFirmwareTextFieldBytes && field[length] != 0U) {
    if (field[length] < 0x20U || field[length] > 0x7eU) return false;
    ++length;
  }
  if (length == 0U || length == kFirmwareTextFieldBytes) return false;
  for (std::size_t index = length; index < kFirmwareTextFieldBytes; ++index) {
    if (field[index] != 0U) return false;
  }
  std::memcpy(out, field, length);
  std::memset(out + length, 0, kFirmwareTextFieldBytes - length);
  return true;
}

// First 8 bytes of SHA-256 of the 65-byte public key.
inline bool firmwareKeyId(const uint8_t (&publicKey)[kFirmwarePublicKeyBytes], uint8_t (&keyId)[kFirmwareKeyIdBytes]) {
  uint8_t digest[kFirmwareSha256Bytes]{};
  if (!firmwareSha256(publicKey, sizeof(publicKey), digest)) return false;
  std::memcpy(keyId, digest, sizeof(keyId));
  return true;
}

// Runs checks 1 to 5 of docs/ota-protocol.md on the header and signature. The signature is checked
// before the fields are read in detail, so unsigned bytes are parsed no further than needed to find
// the key. On Ok, header holds the verified fields.
inline FirmwareCheck checkFirmwarePackagePrefix(const uint8_t* prefix, std::size_t length,
                                                const FirmwareInstallPolicy& policy, FirmwarePackageHeader& header) {
  namespace off = firmware_header_offset;
  header = FirmwarePackageHeader{};
  if (prefix == nullptr || length != kFirmwarePackagePrefixBytes ||
      std::memcmp(prefix + off::kMagic, kFirmwarePackageMagic, sizeof(kFirmwarePackageMagic)) != 0 ||
      firmwareReadU16(prefix + off::kFormat) != kFirmwarePackageFormat ||
      firmwareReadU16(prefix + off::kHeaderSize) != kFirmwareHeaderBytes) {
    return FirmwareCheck::BadHeader;
  }

  const FirmwareTrustedKey* trusted = nullptr;
  for (std::size_t slot = 0; policy.keys != nullptr && slot < policy.keyCount && trusted == nullptr; ++slot) {
    if (!policy.keys[slot].present) continue;
    uint8_t keyId[kFirmwareKeyIdBytes]{};
    if (firmwareKeyId(policy.keys[slot].publicKey, keyId) &&
        std::memcmp(keyId, prefix + off::kKeyId, sizeof(keyId)) == 0) {
      trusted = &policy.keys[slot];
    }
  }
  if (trusted == nullptr) return FirmwareCheck::BadSignature;
  uint8_t digest[kFirmwareSha256Bytes]{};
  uint8_t signature[kFirmwareSignatureBytes]{};
  std::memcpy(signature, prefix + kFirmwareHeaderBytes, sizeof(signature));
  if (!firmwareSha256(prefix, kFirmwareHeaderBytes, digest) ||
      !firmwareSignatureValid(trusted->publicKey, digest, signature)) {
    return FirmwareCheck::BadSignature;
  }

  // Signed, but a signer's tool could still have written a malformed header; refuse it whole.
  for (std::size_t index = off::kReserved; index < kFirmwareHeaderBytes; ++index) {
    if (prefix[index] != 0U) return FirmwareCheck::BadHeader;
  }
  std::memcpy(header.keyId, prefix + off::kKeyId, sizeof(header.keyId));
  if (!readFirmwareTextField(prefix + off::kProduct, header.product) ||
      !readFirmwareTextField(prefix + off::kHardware, header.hardware) ||
      !readFirmwareTextField(prefix + off::kVersion, header.version)) {
    header = FirmwarePackageHeader{};
    return FirmwareCheck::BadHeader;
  }
  header.securityCounter = firmwareReadU32(prefix + off::kSecurityCounter);
  header.imageSize = firmwareReadU32(prefix + off::kImageSize);
  std::memcpy(header.imageSha256, prefix + off::kImageSha256, sizeof(header.imageSha256));

  FirmwareCheck result = FirmwareCheck::Ok;
  if (policy.product == nullptr || policy.hardware == nullptr || std::strcmp(header.product, policy.product) != 0 ||
      std::strcmp(header.hardware, policy.hardware) != 0) {
    result = FirmwareCheck::WrongDevice;
  } else if (header.securityCounter < policy.minimumSecurityCounter) {
    result = FirmwareCheck::OlderRelease;
  } else if (header.imageSize == 0U || header.imageSize > policy.partitionBytes) {
    result = FirmwareCheck::TooLarge;
  }
  if (result != FirmwareCheck::Ok) header = FirmwarePackageHeader{};
  return result;
}

// Hashes the image as it streams into the partition (checks 8 and 9). The caller writes each chunk
// only after accept() returns true, and changes the boot partition only after finish() returns Ok.
class FirmwareImageVerifier {
 public:
  bool begin(const FirmwarePackageHeader& header) {
    std::memcpy(expected_, header.imageSha256, sizeof(expected_));
    expectedSize_ = header.imageSize;
    received_ = 0;
    failed_ = !hash_.begin();
    return !failed_;
  }

  uint32_t received() const { return received_; }
  uint32_t remaining() const { return expectedSize_ - received_; }

  // Refuses a chunk that would run past the declared size.
  bool accept(const uint8_t* data, std::size_t length) {
    if (failed_ || length > remaining()) {
      failed_ = true;
      return false;
    }
    if (!hash_.update(data, length)) {
      failed_ = true;
      return false;
    }
    received_ += static_cast<uint32_t>(length);
    return true;
  }

  FirmwareCheck finish() {
    if (failed_ || received_ != expectedSize_) {
      failed_ = true;
      return FirmwareCheck::DownloadFailed;
    }
    uint8_t digest[kFirmwareSha256Bytes]{};
    failed_ = true;  // finish() ends the stream either way
    if (!hash_.finish(digest)) return FirmwareCheck::DownloadFailed;
    uint8_t difference = 0;
    for (std::size_t index = 0; index < sizeof(digest); ++index) difference |= digest[index] ^ expected_[index];
    return difference == 0U ? FirmwareCheck::Ok : FirmwareCheck::HashMismatch;
  }

 private:
  FirmwareSha256 hash_;
  uint8_t expected_[kFirmwareSha256Bytes]{};
  uint32_t expectedSize_ = 0;
  uint32_t received_ = 0;
  bool failed_ = true;
};

// Decodes lowercase or uppercase hex of exactly sizeof(out) bytes.
template <std::size_t N>
inline bool decodeFirmwareHex(const String& text, uint8_t (&out)[N]) {
  if (text.length() != N * 2U) return false;
  for (std::size_t index = 0; index < N; ++index) {
    const int high = transportHexDigit(text[index * 2U]);
    const int low = transportHexDigit(text[index * 2U + 1U]);
    if (high < 0 || low < 0) return false;
    out[index] = static_cast<uint8_t>((high << 4U) | low);
  }
  return true;
}

// Parses `trusted` key text from ota_keys.h: 130 hex characters of an uncompressed point, or empty.
inline FirmwareTrustedKey firmwareTrustedKeyFromHex(const char* hex) {
  FirmwareTrustedKey key;
  if (hex == nullptr || hex[0] == '\0') return key;
  key.present = decodeFirmwareHex(String(hex), key.publicKey) && key.publicKey[0] == 0x04U;
  if (!key.present) std::memset(key.publicKey, 0, sizeof(key.publicKey));
  return key;
}

struct FirmwareOfferResponse {
  bool valid = false;
  bool available = false;
  uint8_t prefix[kFirmwarePackagePrefixBytes]{};
};

inline FirmwareOfferResponse parseFirmwareOfferJson(const String& body) {
  FirmwareOfferResponse response;
  if (body.length() > kMaxHttpBodyBytes) return response;
  SyncJsonParser parser(body);
  JsonValueView root;
  if (!parser.parseDocument(root) || root.kind != JsonValueKind::Object) return response;
  bool available = false;
  if (!jsonBooleanField(parser, root, "available", available, true)) return response;
  String package;
  if (!jsonStringField(parser, root, "package", package, available)) return response;
  if (available && !decodeFirmwareHex(package, response.prefix)) return response;
  response.available = available;
  response.valid = true;
  return response;
}

inline String buildFirmwareOfferRequestJson(const char* product, const char* hardware, const char* version,
                                            uint32_t securityCounter, const FirmwareTrustedKey* keys,
                                            std::size_t keyCount) {
  String out;
  out.reserve(192);
  out += "{\"product\":\"";
  out += escapeJson(product);
  out += "\",\"hardware\":\"";
  out += escapeJson(hardware);
  out += "\",\"version\":\"";
  out += escapeJson(version);
  out += "\",\"securityCounter\":";
  out += String(static_cast<unsigned long>(securityCounter));
  out += ",\"keyIds\":[";
  bool first = true;
  for (std::size_t slot = 0; keys != nullptr && slot < keyCount; ++slot) {
    uint8_t keyId[kFirmwareKeyIdBytes]{};
    if (!keys[slot].present || !firmwareKeyId(keys[slot].publicKey, keyId)) continue;
    if (!first) out += ',';
    first = false;
    out += '"';
    out += transportHex(keyId, sizeof(keyId));
    out += '"';
  }
  out += "]}";
  return out;
}

inline String buildFirmwareChunkRequestJson(const uint8_t (&imageSha256)[kFirmwareSha256Bytes], uint32_t offset,
                                            uint32_t length) {
  String out;
  out.reserve(128);
  out += "{\"imageSha256\":\"";
  out += transportHex(imageSha256, sizeof(imageSha256));
  out += "\",\"offset\":";
  out += String(static_cast<unsigned long>(offset));
  out += ",\"length\":";
  out += String(static_cast<unsigned long>(length));
  out += '}';
  return out;
}

inline String buildFirmwareStatusJson(const uint8_t (&imageSha256)[kFirmwareSha256Bytes], const char* state,
                                      const char* error, const char* reason) {
  String out;
  out.reserve(160);
  out += "{\"imageSha256\":\"";
  out += transportHex(imageSha256, sizeof(imageSha256));
  out += "\",\"state\":\"";
  out += escapeJson(state);
  out += "\",\"error\":\"";
  out += escapeJson(error);
  out += "\",\"reason\":\"";
  out += escapeJson(reason);
  out += "\"}";
  return out;
}

// The size of the next chunk to ask for, at most kMaxFirmwareChunkBytes.
inline uint32_t nextFirmwareChunkLength(uint32_t imageSize, uint32_t offset) {
  if (offset >= imageSize) return 0U;
  const uint32_t left = imageSize - offset;
  return left < kMaxFirmwareChunkBytes ? left : static_cast<uint32_t>(kMaxFirmwareChunkBytes);
}

}  // namespace inventatory_scan
