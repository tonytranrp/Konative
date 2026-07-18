#pragma once

#include <utility>
#include <variant>

namespace konative::core {

// A minimal Result<T, E>, preferred over exceptions at any interop boundary where an unwound C++
// exception cannot safely cross (ARCHITECTURE.md section 6.3, section 12's self-checking loader design).
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
