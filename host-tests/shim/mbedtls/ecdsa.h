// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

// Host mbedtls ECDSA verification stand-in, backed by OpenSSL 3. As in mbedtls, r and s outside
// 1 to n - 1 fail, and the digest is verified as given, without hashing it again.
#pragma once

#include "mbedtls/bignum.h"
#include "mbedtls/ecp.h"

#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/evp.h>

#include <cstddef>

inline int mbedtls_ecdsa_verify(mbedtls_ecp_group* group, const unsigned char* digest, std::size_t digestLength,
                                const mbedtls_ecp_point* point, const mbedtls_mpi* r, const mbedtls_mpi* s) {
  if (group == nullptr || group->id != MBEDTLS_ECP_DP_SECP256R1 || digest == nullptr || r == nullptr ||
      s == nullptr) {
    return -1;
  }
  EVP_PKEY* key = hostEcpPublicKey(point);
  if (key == nullptr) return -1;
  BIGNUM* rValue = BN_bin2bn(r->bytes, static_cast<int>(r->length), nullptr);
  BIGNUM* sValue = BN_bin2bn(s->bytes, static_cast<int>(s->length), nullptr);
  ECDSA_SIG* signature = ECDSA_SIG_new();
  int result = -1;
  if (rValue != nullptr && sValue != nullptr && signature != nullptr && ECDSA_SIG_set0(signature, rValue, sValue) == 1) {
    rValue = nullptr;  // owned by signature now
    sValue = nullptr;
    unsigned char* der = nullptr;
    const int derLength = i2d_ECDSA_SIG(signature, &der);
    EVP_PKEY_CTX* context = EVP_PKEY_CTX_new_from_pkey(nullptr, key, nullptr);
    if (derLength > 0 && context != nullptr && EVP_PKEY_verify_init(context) == 1 &&
        EVP_PKEY_verify(context, der, static_cast<std::size_t>(derLength), digest, digestLength) == 1) {
      result = 0;
    }
    EVP_PKEY_CTX_free(context);
    OPENSSL_free(der);
  }
  BN_free(rValue);
  BN_free(sValue);
  ECDSA_SIG_free(signature);
  EVP_PKEY_free(key);
  return result;
}
