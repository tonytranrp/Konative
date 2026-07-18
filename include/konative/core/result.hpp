#pragma once

#include <utility>
#include <variant>

namespace konative::core {

// A minimal Result<T, E>, preferred over exceptions at the Kotlin/Native <-> C++ boundary
// (ARCHITECTURE.md section 6.3) where an unwound C++ exception cannot safely cross a plain-C-ABI call.
template <typename T, typename E>
class Result {
public:
    static Result ok(T value) { return Result{std::in_place_index<0>, std::move(value)}; }
    static Result err(E error) { return Result{std::in_place_index<1>, std::move(error)}; }

    [[nodiscard]] bool has_value() const noexcept { return storage_.index() == 0; }
    explicit operator bool() const noexcept { return has_value(); }

    [[nodiscard]] T& value() { return std::get<0>(storage_); }
    [[nodiscard]] const T& value() const { return std::get<0>(storage_); }
    [[nodiscard]] E& error() { return std::get<1>(storage_); }
    [[nodiscard]] const E& error() const { return std::get<1>(storage_); }

private:
    template <std::size_t I, typename U>
    Result(std::in_place_index_t<I> tag, U&& value) : storage_(tag, std::forward<U>(value)) {}

    std::variant<T, E> storage_;
};

} // namespace konative::core
