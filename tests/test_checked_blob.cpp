#include <doctest/doctest.h>

#include <array>
#include <vector>

#include "konative/embed/checked_blob.hpp"

namespace {

std::array<unsigned char, 32> sha256_of(std::span<const unsigned char> data) {
    std::array<unsigned char, 32> digest{};
    picosha2::hash256(data.begin(), data.end(), digest.begin(), digest.end());
    return digest;
}

} // namespace

TEST_CASE("verify_blob accepts data matching its real SHA-256") {
    const std::vector<unsigned char> data{'k', 'o', 'n', 'a', 't', 'i', 'v', 'e'};
    const auto expected = sha256_of(data);

    auto result = konative::embed::verify_blob(data, expected);

    REQUIRE(result.has_value());
    CHECK(result.value().size() == data.size());
    CHECK(result.value().data() == data.data());
}

TEST_CASE("verify_blob rejects data whose hash does not match") {
    const std::vector<unsigned char> data{'k', 'o', 'n', 'a', 't', 'i', 'v', 'e'};
    std::array<unsigned char, 32> wrong_hash{};
    wrong_hash.fill(0xAB);

    auto result = konative::embed::verify_blob(data, wrong_hash);

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error() == konative::embed::BlobVerifyError::HashMismatch);
}

TEST_CASE("verify_blob rejects data that changed by a single byte") {
    std::vector<unsigned char> data{'k', 'o', 'n', 'a', 't', 'i', 'v', 'e'};
    const auto expected = sha256_of(data);
    data[0] = 'K'; // corrupt after computing the expected hash, matching a real truncation/mismatch

    auto result = konative::embed::verify_blob(data, expected);

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error() == konative::embed::BlobVerifyError::HashMismatch);
}

TEST_CASE("verify_blob handles an empty blob") {
    const std::vector<unsigned char> data{};
    const auto expected = sha256_of(data);

    auto result = konative::embed::verify_blob(data, expected);

    REQUIRE(result.has_value());
    CHECK(result.value().empty());
}
