#pragma once

#include <algorithm>
#include <array>
#include <span>

#include <picosha2.h>

#include "konative/core/result.hpp"

// Runtime counterpart to cmake/modules/KonativeEmbedBlob.cmake's VERIFY_SHA256 option - see that
// file's doc comment for the full design (a build-integrity self-check, not a tamper/security
// boundary: the expected hash is linked in cleartext right next to the data it checks).
namespace konative::embed {

enum class BlobVerifyError {
    HashMismatch,
};

// Re-hashes `data` (the real <prefix>_start..<prefix>_end bytes from an embedded blob) and
// compares against `expected_sha256` (the <prefix>_expected_sha256[32] KonativeEmbedBlob.cmake
// computed at build time). Returns `data` unchanged on success, so a caller can chain straight
// into whatever consumes the verified bytes without re-threading the span through by hand:
//   auto verified = konative::embed::verify_blob(data, expected);
//   if (!verified) { /* handle BlobVerifyError::HashMismatch */ }
//   use(verified.value());
[[nodiscard]] inline core::Result<std::span<const unsigned char>, BlobVerifyError> verify_blob(
    std::span<const unsigned char> data, std::span<const unsigned char, 32> expected_sha256) {
    std::array<unsigned char, 32> actual{};
    picosha2::hash256(data.begin(), data.end(), actual.begin(), actual.end());

    if (!std::equal(actual.begin(), actual.end(), expected_sha256.begin())) {
        return core::Result<std::span<const unsigned char>, BlobVerifyError>::err(
            BlobVerifyError::HashMismatch);
    }
    return core::Result<std::span<const unsigned char>, BlobVerifyError>::ok(data);
}

} // namespace konative::embed
