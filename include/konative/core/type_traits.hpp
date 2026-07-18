#pragma once

#include <concepts>
#include <type_traits>

// Shared C++20 concepts used across Konative's templates in place of SFINAE (ARCHITECTURE.md section 2).
namespace konative::core {

template <typename T>
concept TriviallyRelocatable = std::is_trivially_move_constructible_v<T> &&
                                std::is_trivially_destructible_v<T>;

template <typename T>
concept Aggregate = std::is_aggregate_v<T>;

// Satisfied by every Konative event type (konative/events/**) - a plain aggregate struct, no
// inherited base class required. See include/konative/events/dispatcher.hpp.
template <typename T>
concept EventType = Aggregate<T> && std::is_object_v<T>;

} // namespace konative::core
