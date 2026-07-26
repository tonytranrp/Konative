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

// KORELOAD_MODULE_EXPORT (not plain extern "C") - real, reproduced bug this project hit directly:
// Konative's own repo-wide CMAKE_CXX_VISIBILITY_PRESET hidden (KonativeWarnings.cmake) applies to
// this MODULE target too, so a plain `extern "C"` function here compiles with hidden visibility
// and never makes it into the .so's dynamic symbol table - confirmed on-device via a real
// dlerror(): "undefined symbol: koreload_module_create". See plugin_contract.hpp's own comment on
// KORELOAD_MODULE_EXPORT for the full explanation.
KORELOAD_MODULE_EXPORT void koreload_module_create(koreload::PluginContract* out) {
    *out = koreload::PluginContract{&g_interface, nullptr, nullptr, nullptr};
}

KORELOAD_MODULE_EXPORT unsigned koreload_abi_version() { return 1; }
