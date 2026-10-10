// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

// Host stand-in for the mbedtls message-digest HMAC calls used by the transport
// security code. It is backed by OpenSSL's EVP_MAC API, a reference
// implementation that the RFC 4231 vectors in the test suite check against.
#pragma once

#include <openssl/core_names.h>
#include <openssl/evp.h>
#include <openssl/params.h>

#include <cstddef>

#define MBEDTLS_MD_SHA256 1

struct mbedtls_md_info_t {
  int type;
};

inline const mbedtls_md_info_t* mbedtls_md_info_from_type(int type) {
  static const mbedtls_md_info_t sha256{MBEDTLS_MD_SHA256};
  return type == MBEDTLS_MD_SHA256 ? &sha256 : nullptr;
}

// Writes the 32-byte HMAC-SHA-256 of input under key. Returns 0 on success, as mbedtls does.
inline int mbedtls_md_hmac(const mbedtls_md_info_t* info, const unsigned char* key, std::size_t keyLength,
                           const unsigned char* input, std::size_t inputLength, unsigned char* output) {
  if (info == nullptr || info->type != MBEDTLS_MD_SHA256 || key == nullptr || output == nullptr) return -1;
  EVP_MAC* mac = EVP_MAC_fetch(nullptr, "HMAC", nullptr);
  if (mac == nullptr) return -1;
  EVP_MAC_CTX* context = EVP_MAC_CTX_new(mac);
  char digest[] = "SHA256";
  OSSL_PARAM params[] = {OSSL_PARAM_construct_utf8_string(OSSL_MAC_PARAM_DIGEST, digest, 0),
                         OSSL_PARAM_construct_end()};
  std::size_t written = 0;
  int result = -1;
  if (context != nullptr && EVP_MAC_init(context, key, keyLength, params) == 1 &&
      (inputLength == 0 || EVP_MAC_update(context, input, inputLength) == 1) &&
      EVP_MAC_final(context, output, &written, 32) == 1 && written == 32) {
    result = 0;
  }
  EVP_MAC_CTX_free(context);
  EVP_MAC_free(mac);
  return result;
}
