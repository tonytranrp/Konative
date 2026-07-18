#pragma once

#include <entt/entt.hpp>

// A thin, intentional alias rather than a wrapping class - konative::ecs::Registry IS
// entt::registry (ARCHITECTURE.md \xc2\xa73: "don't hand-roll what a good library already solves").
// Konative code should still spell konative::ecs::Registry, not entt::registry directly, so the
// indirection point exists if it's ever needed.
namespace konative::ecs {

using Registry = entt::registry;
using Entity = entt::entity;

inline constexpr Entity kNullEntity = entt::null;

} // namespace konative::ecs
