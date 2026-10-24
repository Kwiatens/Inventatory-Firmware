// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

// Host mbedtls ECP stand-in for P-256 only, backed by OpenSSL 3. Like mbedtls 2.28, a point is read
// only in uncompressed form, and mbedtls_ecp_check_pubkey rejects a point that is not on the curve.
#pragma once

#include "mbedtls/bignum.h"

#include <openssl/core_names.h>
#include <openssl/evp.h>
#include <openssl/params.h>

#include <cstddef>
#include <cstring>

#define MBEDTLS_ECP_DP_SECP256R1 3

struct mbedtls_ecp_group {
  int id;
};

struct mbedtls_ecp_point {
  unsigned char bytes[65];
  std::size_t length;
};

inline void mbedtls_ecp_group_init(mbedtls_ecp_group* group) { group->id = 0; }
inline void mbedtls_ecp_group_free(mbedtls_ecp_group* group) { group->id = 0; }

inline int mbedtls_ecp_group_load(mbedtls_ecp_group* group, int id) {
  if (group == nullptr || id != MBEDTLS_ECP_DP_SECP256R1) return -1;
  group->id = id;
  return 0;
}

inline void mbedtls_ecp_point_init(mbedtls_ecp_point* point) {
  std::memset(point->bytes, 0, sizeof(point->bytes));
  point->length = 0;
}

inline void mbedtls_ecp_point_free(mbedtls_ecp_point* point) { mbedtls_ecp_point_init(point); }

inline int mbedtls_ecp_point_read_binary(const mbedtls_ecp_group* group, mbedtls_ecp_point* point,
                                         const unsigned char* buffer, std::size_t length) {
  if (group == nullptr || group->id != MBEDTLS_ECP_DP_SECP256R1 || point == nullptr || buffer == nullptr ||
      length != sizeof(point->bytes) || buffer[0] != 0x04) {
    return -1;
  }
  std::memcpy(point->bytes, buffer, length);
  point->length = length;
  return 0;
}

// Builds an OpenSSL public key from the point; nullptr if the point is not a valid P-256 public key.
inline EVP_PKEY* hostEcpPublicKey(const mbedtls_ecp_point* point) {
  if (point == nullptr || point->length != sizeof(point->bytes)) return nullptr;
  char groupName[] = "prime256v1";
  unsigned char octets[sizeof(point->bytes)];
  std::memcpy(octets, point->bytes, sizeof(octets));
  OSSL_PARAM params[] = {OSSL_PARAM_construct_utf8_string(OSSL_PKEY_PARAM_GROUP_NAME, groupName, 0),
                         OSSL_PARAM_construct_octet_string(OSSL_PKEY_PARAM_PUB_KEY, octets, sizeof(octets)),
                         OSSL_PARAM_construct_end()};
  EVP_PKEY_CTX* context = EVP_PKEY_CTX_new_from_name(nullptr, "EC", nullptr);
  EVP_PKEY* key = nullptr;
  if (context == nullptr || EVP_PKEY_fromdata_init(context) != 1 ||
      EVP_PKEY_fromdata(context, &key, EVP_PKEY_PUBLIC_KEY, params) != 1) {
    EVP_PKEY_CTX_free(context);
    return nullptr;
  }
  EVP_PKEY_CTX_free(context);
  EVP_PKEY_CTX* check = EVP_PKEY_CTX_new_from_pkey(nullptr, key, nullptr);
  const bool onCurve = check != nullptr && EVP_PKEY_public_check(check) == 1;
  EVP_PKEY_CTX_free(check);
  if (!onCurve) {
    EVP_PKEY_free(key);
    return nullptr;
  }
  return key;
}

inline int mbedtls_ecp_check_pubkey(const mbedtls_ecp_group* group, const mbedtls_ecp_point* point) {
  if (group == nullptr || group->id != MBEDTLS_ECP_DP_SECP256R1) return -1;
  EVP_PKEY* key = hostEcpPublicKey(point);
  if (key == nullptr) return -1;
  EVP_PKEY_free(key);
  return 0;
}
