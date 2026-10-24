// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

// Host mbedtls HMAC and SHA-256 stand-in, backed by OpenSSL EVP_MAC and EVP_MD (checked against RFC 4231
// and FIPS 180-2 vectors).
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

// Writes HMAC-SHA-256 of input under key. Returns 0 on success, as mbedtls does.
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

// Streaming SHA-256. Only the digest mode (hmac = 0) is provided; the firmware uses mbedtls_md_hmac for HMAC.
struct mbedtls_md_context_t {
  EVP_MD_CTX* evp;
  const mbedtls_md_info_t* info;
};

inline void mbedtls_md_init(mbedtls_md_context_t* context) {
  context->evp = nullptr;
  context->info = nullptr;
}

inline void mbedtls_md_free(mbedtls_md_context_t* context) {
  if (context == nullptr) return;
  EVP_MD_CTX_free(context->evp);
  context->evp = nullptr;
  context->info = nullptr;
}

inline int mbedtls_md_setup(mbedtls_md_context_t* context, const mbedtls_md_info_t* info, int hmac) {
  if (context == nullptr || info == nullptr || info->type != MBEDTLS_MD_SHA256 || hmac != 0) return -1;
  context->evp = EVP_MD_CTX_new();
  context->info = info;
  return context->evp == nullptr ? -1 : 0;
}

inline int mbedtls_md_starts(mbedtls_md_context_t* context) {
  if (context == nullptr || context->evp == nullptr) return -1;
  return EVP_DigestInit_ex(context->evp, EVP_sha256(), nullptr) == 1 ? 0 : -1;
}

inline int mbedtls_md_update(mbedtls_md_context_t* context, const unsigned char* input, std::size_t length) {
  if (context == nullptr || context->evp == nullptr) return -1;
  if (length == 0) return 0;
  return EVP_DigestUpdate(context->evp, input, length) == 1 ? 0 : -1;
}

inline int mbedtls_md_finish(mbedtls_md_context_t* context, unsigned char* output) {
  if (context == nullptr || context->evp == nullptr || output == nullptr) return -1;
  unsigned int written = 0;
  return EVP_DigestFinal_ex(context->evp, output, &written) == 1 && written == 32 ? 0 : -1;
}
