#pragma once

#include <concepts>

#include "konative/ecs/registry.hpp"

namespace konative::ecs::detail {

// A System is any callable taking (Registry&, float deltaSeconds) - deliberately NOT an
// inherited base class (CRTP or virtual), so a system can be a plain free function, a lambda, or
// a member function bound via a capture - see konative/ecs/system.hpp.
template <typename F>
concept SystemLike = std::invocable<F, Registry&, float>;

} // namespace konative::ecs::detail
