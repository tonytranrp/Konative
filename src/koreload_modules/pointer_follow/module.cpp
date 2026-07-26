#include <koreload/plugin_contract.hpp>

#include "konative/app/heartbeat_counter_serialize.hpp"
#include "konative/app/pointer_follow_koreload_interface.hpp"
#include "konative/app/pointer_follow_system.hpp"

#include <cereal/archives/binary.hpp>

#include <cstring>
#include <sstream>
#include <string>

// The real, hot-reloadable counterpart to jni_onload.cpp's default, statically-linked
// `world().systems().add(&move_followers_toward_targets)` registration - KONATIVE_ENABLE_KORELOAD
// only (PROMPT.md section 13 M8 in the KoReload repo). Edit this file, rebuild just this target,
// push it, and koreload_cli/the receiver reloads it into the running app with no process restart -
// see pointer_follow_koreload_interface.hpp for why PointerFollowKoreloadInterface is shared with
// the host instead of redeclared here.
//
// The FOLLOWER's state (PointerFollow::target/approach_rate, Transform::position) lives in the
// HOST's ECS registry, on the follower entity, completely untouched by a system-function reload -
// genuinely nothing to transfer there, not an oversight. But this module DOES now carry one real
// piece of its OWN internal state - `g_interface.reload_survival_ticks` - specifically so
// save_state/restore_state have a real, non-synthetic Konative payload to round-trip (PROMPT.md
// section 13 M8's own "exercise Konative's own EnTT-registry + cereal-snapshot pattern... not the
// synthetic M6 test harness" ask, in the KoReload repo). See
// pointer_follow_koreload_interface.hpp's own comment on that field for the full reasoning.

namespace {

void tick_thunk(konative::ecs::Registry* registry, float delta_seconds);

PointerFollowKoreloadInterface g_interface{&tick_thunk, {}};

void tick_thunk(konative::ecs::Registry* registry, float delta_seconds) {
    konative::app::move_followers_toward_targets(*registry, delta_seconds);
    ++g_interface.reload_survival_ticks.ticks;
}

// `instance` is always `&g_interface` (see koreload_module_create below) - real cereal
// serialization of a real, already-production Konative component type
// (konative::app::HeartbeatCounter, konative/app/heartbeat_counter_serialize.hpp - the exact
// serialize() Konative's own full-process-restart snapshot already trusts,
// ecs/snapshot_file.hpp), not a raw memcpy of a synthetic POD the way KoReload's own
// m6_state_transfer_benchmark module does. Genuinely exercises the claim that a hot-swap can
// reuse the same serialize() a full-restart snapshot already relies on.
void* save_state(void* instance, std::size_t* out_size) {
    auto* interface = static_cast<PointerFollowKoreloadInterface*>(instance);
    std::ostringstream stream(std::ios::binary);
    {
        cereal::BinaryOutputArchive archive(stream);
        archive(interface->reload_survival_ticks);
    }
    const std::string bytes = stream.str();
    *out_size = bytes.size();
    char* buffer = new char[bytes.size()];
    std::memcpy(buffer, bytes.data(), bytes.size());
    return buffer;
}

bool restore_state(void* instance, const void* data, std::size_t size) {
    auto* interface = static_cast<PointerFollowKoreloadInterface*>(instance);
    std::istringstream stream(std::string(static_cast<const char*>(data), size), std::ios::binary);
    try {
        cereal::BinaryInputArchive archive(stream);
        archive(interface->reload_survival_ticks);
    } catch (const std::exception&) {
        return false; // real, corrupt/truncated/layout-mismatched payload - never crash on it
    }
    return true;
}

void free_state(void* data) {
    delete[] static_cast<char*>(data);
}

} // namespace

// KORELOAD_MODULE_EXPORT (not plain extern "C") - real, reproduced bug this project hit directly:
// Konative's own repo-wide CMAKE_CXX_VISIBILITY_PRESET hidden (KonativeWarnings.cmake) applies to
// this MODULE target too, so a plain `extern "C"` function here compiles with hidden visibility
// and never makes it into the .so's dynamic symbol table - confirmed on-device via a real
// dlerror(): "undefined symbol: koreload_module_create". See plugin_contract.hpp's own comment on
// KORELOAD_MODULE_EXPORT for the full explanation.
KORELOAD_MODULE_EXPORT void koreload_module_create(koreload::PluginContract* out) {
    *out = koreload::PluginContract{&g_interface, &save_state, &restore_state, &free_state};
}

KORELOAD_MODULE_EXPORT unsigned koreload_abi_version() { return 1; }
