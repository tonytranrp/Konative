#include <doctest/doctest.h>

#include "konative/core/result.hpp"

TEST_CASE("Result<T,E> carries a value on ok()") {
    auto result = konative::core::Result<int, const char*>::ok(42);
    CHECK(result.has_value());
    CHECK(result.value() == 42);
}

TEST_CASE("Result<T,E> carries an error on err()") {
    auto result = konative::core::Result<int, const char*>::err("failed");
    CHECK_FALSE(result.has_value());
    CHECK(std::string(result.error()) == "failed");
}
