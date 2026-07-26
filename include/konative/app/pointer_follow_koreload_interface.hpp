#pragma once

#include "konative/app/heartbeat_counter.hpp"
#include "konative/app/waypoint_cycler_interface.hpp"
#include "konative/ecs/registry.hpp"

// The interface KoReload's src/koreload_modules/pointer_follow/module.cpp hands back through
// koreload::PluginContract::instance, and src/platform/android/jni_onload.cpp casts that void*
// back to (KONATIVE_ENABLE_KORELOAD only - PROMPT.md section 13 M8 in the KoReload repo). Shared
// here, not defined independently on each side, so both the module and the host agree on the
// exact same struct layout from the exact same header - the same reasoning
// pointer_follow.hpp/pointer_follow_system.hpp are already split out for.
//
// `tick` matches move_followers_toward_targets' own (Registry&, float) system shape, plus a
// nullable `waypoint_cycler` parameter (PROMPT.md section 14's cross-module-calls mechanism,
// dogfooded here for the first time) - the HOST resolves waypoint_cycler's own
// WaypointCyclerInterface (jni_onload.cpp, via ModuleRegistry::subscribe_interface(), never a
// back-channel from this module to the registry - CROSS_MODULE_CALLS_DESIGN.md section 3.5's own
// "no back-channel needed" shape) and passes the CURRENT resolved pointer in fresh every call, so
// an independent reload of waypoint_cycler is picked up the very next tick with zero staleness.
// tick_thunk's own implementation is what actually CALLS waypoint_cycler->drive_targets() - a real
// cross-.so call made from INSIDE this module's own compiled code, not the host calling both
// modules separately and combining the results. Null when waypoint_cycler isn't loaded (yet, or at
// all) - tick_thunk degrades gracefully to whatever touch-driven target was already set, the same
// "clean, visible absence, never a crash" every other guarded call in this codebase falls back to.
// Bumped koreload_abi_version() 1->2 in module.cpp when this parameter was added - a real,
// intentional breaking change to this specific host<->module contract, refused cleanly
// (NeedsFullRestart) by any old-ABI build rather than silently misinterpreting the new call site.
// The host wraps this in ONE koreload::InterfaceHandle<PointerFollowKoreloadInterface>, repointed
// on every successful load/reload; world().systems().add() itself is called exactly once for the
// life of the process (SystemSequence has no removal API - see jni_onload.cpp's own setup comment
// for why calling add() again per reload would be wrong).
//
// `reload_survival_ticks` is this module's one piece of genuine INTERNAL state (unlike
// PointerFollow's own target/approach_rate, which live in the host's ECS registry, untouched by a
// system-function reload - see module.cpp's own comment on why this module is otherwise
// stateless by design). It exists specifically so this module's save_state/restore_state hooks
// have real, non-synthetic Konative state to round-trip via cereal through HeartbeatCounter's own
// real, already-production serialize() - proving Konative's EnTT+cereal-snapshot pattern (already
// trusted for full-process-restart persistence) also survives KoReload's in-process hot-swap path
// with a genuine Konative payload, not KoReload's own synthetic int-counter benchmark module
// (PROMPT.md section 13 M8's own "not the synthetic M6 test harness" ask, in the KoReload repo).
struct PointerFollowKoreloadInterface {
    void (*tick)(konative::ecs::Registry* registry, float delta_seconds,
                 const WaypointCyclerInterface* waypoint_cycler);
    konative::app::HeartbeatCounter reload_survival_ticks{};
};
