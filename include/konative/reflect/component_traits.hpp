#pragma once

#include <entt/entt.hpp>

#include "konative/core/type_traits.hpp"

namespace konative::reflect {

// A component that opts into generic, reflection-driven tooling (editor inspectors,
// serialization) must be default-constructible and an aggregate - kept as an explicit concept
// rather than an implicit assumption, so a failing static_assert names the real requirement.
template <typename T>
concept ReflectableComponent = konative::core::Aggregate<T> && std::is_default_constructible_v<T>;

} // namespace konative::reflect
