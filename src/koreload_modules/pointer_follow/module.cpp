#include <koreload/plugin_contract.hpp>

#include "konative/app/pointer_follow_koreload_interface.hpp"
#include "konative/app/pointer_follow_system.hpp"

// The real, hot-reloadable counterpart to jni_onload.cpp's default, statically-linked
// `world().systems().add(&move_followers_toward_targets)` registration - KONATIVE_ENABLE_KORELOAD
// only (PROMPT.md section 13 M8 in the KoReload repo). Edit this file, rebuild just this target,
// push it, and koreload_cli/the receiver reloads it into the running app with no process restart -
// see pointer_follow_koreload_interface.hpp for why PointerFollowKoreloadInterface is shared with
// the host instead of redeclared here.
//
// No save_state/restore_state: unlike KoReload's own test fixtures (which hold a counter INSIDE
// the module's own instance), this module is stateless by construction - the real, mutable state
// (PointerFollow::target/approach_rate, Transform::position) lives in the HOST's ECS registry, on
// the follower entity, completely untouched by a system-function reload. Nothing to transfer
// because reloading this module never loses anything - a genuinely different case from KoReload's
// own fixtures, not an oversight.

namespace {

void tick_thunk(konative::ecs::Registry* registry, float delta_seconds) {
    konative::app::move_followers_toward_targets(*registry, delta_seconds);
}

PointerFollowKoreloadInterface g_interface{&tick_thunk};

} // namespace

extern "C" void koreload_module_create(koreload::PluginContract* out) {
    *out = koreload::PluginContract{&g_interface, nullptr, nullptr, nullptr};
}

extern "C" unsigned koreload_abi_version() { return 1; }
