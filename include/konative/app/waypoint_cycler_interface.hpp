#pragma once

#include "konative/ecs/registry.hpp"

#include <koreload/interface_id.hpp>

// The cross-module interface src/koreload_modules/waypoint_cycler/ provides and pointer_follow
// consumes (KoReload repo's CROSS_MODULE_CALLS_DESIGN.md section 3, dogfooded here for the first
// time outside KoReload's own synthetic test fixtures - PROMPT.md section 14 in the KoReload repo).
// Both PointerFollow's own touch-driven target and this module's autonomous one write into the
// SAME PointerFollow::target field, so only one drives a given entity at a time - pointer_follow's
// tick_thunk calls drive_targets() FIRST (when a provider is available) so approach() then chases
// whatever this call just set, that same frame.
//
// No instance parameter, matching PointerFollowKoreloadInterface::tick's own shape exactly - the
// provider's real state (its own elapsed-time accumulator) is closed over via the module's own
// anonymous-namespace global, the same reasoning module.cpp's own comment gives for tick_thunk.
struct WaypointCyclerInterface {
    void (*drive_targets)(konative::ecs::Registry* registry, float delta_seconds);
};

inline constexpr koreload::InterfaceId kWaypointCyclerInterfaceId =
    koreload::hash_interface_name("konative.app.WaypointCyclerInterface.v1");
