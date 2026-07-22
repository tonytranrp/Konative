#pragma once

#include <utility>
#include <variant>

#include "konative/core/assert.hpp"

namespace konative::core {

// A minimal Result<T, E>, preferred over exceptions at any interop boundary where an unwound C++
// exception cannot safely cross (ARCHITECTURE.md section 6.3's "outlives the Kotlin/Native
// context" note - applies equally to the old @CName boundary and the current JNI_OnLoad one - and
// include/konative/embed/README.md's own self-checking-loader hard rule).
// [[nodiscard]] on the class itself (not just its accessors) is deliberate: a function returning
// a fallible Result whose result gets silently dropped entirely is exactly the bug this type
// exists to prevent - a code review caught this missing on an earlier pass.
template <typename T, typename E>
class [[nodiscard]] Result {
public:
    static Result ok(T value) { return Result{std::in_place_index<0>, std::move(value)}; }
    static Result err(E error) { return Result{std::in_place_index<1>, std::move(error)}; }

    [[nodiscard]] bool has_value() const noexcept { return storage_.index() == 0; }
    explicit operator bool() const noexcept { return has_value(); }

    // Real, release-mode-safe guards (KONATIVE_ASSERT, not a raw assert()) against exactly the
    // hazard this whole type exists to prevent (see the class comment above): calling value() on an
    // error Result (or vice versa) would otherwise fall through to std::get's own std::
    // bad_variant_access - a real C++ exception, at exactly the kind of interop boundary this type
    // was built so callers never have to let one cross. [[nodiscard]] on the class only stops a
    // caller from discarding the Result entirely; it does nothing to stop a caller who kept it from
    // skipping the has_value() check before reaching in - found by a 2026-07-22 deep review. Every
    // current real call site already checks first (confirmed by grep), so this is hardening for the
    // next call site, not a fix for a live bug.
    [[nodiscard]] T& value() {
        KONATIVE_ASSERT(has_value(), "Result<T,E>::value() called on a Result holding an error");
        return std::get<0>(storage_);
    }
    [[nodiscard]] const T& value() const {
        KONATIVE_ASSERT(has_value(), "Result<T,E>::value() called on a Result holding an error");
        return std::get<0>(storage_);
    }
    [[nodiscard]] E& error() {
        KONATIVE_ASSERT(!has_value(), "Result<T,E>::error() called on a Result holding a value");
        return std::get<1>(storage_);
    }
    [[nodiscard]] const E& error() const {
        KONATIVE_ASSERT(!has_value(), "Result<T,E>::error() called on a Result holding a value");
        return std::get<1>(storage_);
    }

private:
    template <std::size_t I, typename U>
    Result(std::in_place_index_t<I> tag, U&& value) : storage_(tag, std::forward<U>(value)) {}

    std::variant<T, E> storage_;
};

} // namespace konative::core
