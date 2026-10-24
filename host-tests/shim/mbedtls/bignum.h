// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

// Host mbedtls bignum stand-in: holds the big-endian bytes that ecdsa.h hands to OpenSSL.
#pragma once

#include <cstddef>
#include <cstring>

struct mbedtls_mpi {
  unsigned char bytes[66];
  std::size_t length;
};

inline void mbedtls_mpi_init(mbedtls_mpi* value) {
  std::memset(value->bytes, 0, sizeof(value->bytes));
  value->length = 0;
}

inline void mbedtls_mpi_free(mbedtls_mpi* value) { mbedtls_mpi_init(value); }

inline int mbedtls_mpi_read_binary(mbedtls_mpi* value, const unsigned char* buffer, std::size_t length) {
  if (value == nullptr || (buffer == nullptr && length != 0) || length > sizeof(value->bytes)) return -1;
  if (length > 0) std::memcpy(value->bytes, buffer, length);
  value->length = length;
  return 0;
}
