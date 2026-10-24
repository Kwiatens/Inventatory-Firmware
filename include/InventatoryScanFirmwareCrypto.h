// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

// SHA-256 and ECDSA P-256 verification for signed updates, on mbedtls. Header-only, so the shared
// test suite runs the same code on the board and, through host-tests/shim, on the host.
#pragma once

#include <cstddef>
#include <cstdint>
#include <mbedtls/bignum.h>
#include <mbedtls/ecdsa.h>
#include <mbedtls/ecp.h>
#include <mbedtls/md.h>

namespace inventatory_scan {

constexpr std::size_t kFirmwareSha256Bytes = 32U;
// An uncompressed P-256 point: 0x04, then X and Y, 32 bytes each.
constexpr std::size_t kFirmwarePublicKeyBytes = 65U;
// Raw r and s, 32 bytes each.
constexpr std::size_t kFirmwareSignatureBytes = 64U;

class FirmwareSha256 {
 public:
  FirmwareSha256() { mbedtls_md_init(&context_); }
  ~FirmwareSha256() { mbedtls_md_free(&context_); }
  FirmwareSha256(const FirmwareSha256&) = delete;
  FirmwareSha256& operator=(const FirmwareSha256&) = delete;

  bool begin() {
    mbedtls_md_free(&context_);
    mbedtls_md_init(&context_);
    const auto* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    started_ = info != nullptr && mbedtls_md_setup(&context_, info, 0) == 0 && mbedtls_md_starts(&context_) == 0;
    return started_;
  }

  bool update(const uint8_t* data, std::size_t length) {
    if (!started_) return false;
    if (length == 0U) return true;
    started_ = data != nullptr && mbedtls_md_update(&context_, data, length) == 0;
    return started_;
  }

  bool finish(uint8_t (&digest)[kFirmwareSha256Bytes]) {
    const bool finished = started_ && mbedtls_md_finish(&context_, digest) == 0;
    started_ = false;
    return finished;
  }

 private:
  mbedtls_md_context_t context_;
  bool started_ = false;
};

inline bool firmwareSha256(const uint8_t* data, std::size_t length, uint8_t (&digest)[kFirmwareSha256Bytes]) {
  FirmwareSha256 hash;
  return hash.begin() && hash.update(data, length) && hash.finish(digest);
}

// Verifies a raw r ‖ s signature over a SHA-256 digest. Any parse failure counts as a bad signature.
inline bool firmwareSignatureValid(const uint8_t (&publicKey)[kFirmwarePublicKeyBytes],
                                   const uint8_t (&digest)[kFirmwareSha256Bytes],
                                   const uint8_t (&signature)[kFirmwareSignatureBytes]) {
  if (publicKey[0] != 0x04U) return false;
  mbedtls_ecp_group group;
  mbedtls_ecp_point point;
  mbedtls_mpi r;
  mbedtls_mpi s;
  mbedtls_ecp_group_init(&group);
  mbedtls_ecp_point_init(&point);
  mbedtls_mpi_init(&r);
  mbedtls_mpi_init(&s);
  const bool valid = mbedtls_ecp_group_load(&group, MBEDTLS_ECP_DP_SECP256R1) == 0 &&
                     mbedtls_ecp_point_read_binary(&group, &point, publicKey, kFirmwarePublicKeyBytes) == 0 &&
                     mbedtls_ecp_check_pubkey(&group, &point) == 0 &&
                     mbedtls_mpi_read_binary(&r, signature, 32U) == 0 &&
                     mbedtls_mpi_read_binary(&s, signature + 32U, 32U) == 0 &&
                     mbedtls_ecdsa_verify(&group, digest, kFirmwareSha256Bytes, &point, &r, &s) == 0;
  mbedtls_mpi_free(&s);
  mbedtls_mpi_free(&r);
  mbedtls_ecp_point_free(&point);
  mbedtls_ecp_group_free(&group);
  return valid;
}

}  // namespace inventatory_scan
