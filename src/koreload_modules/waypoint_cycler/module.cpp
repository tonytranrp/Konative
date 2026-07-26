#include <koreload/plugin_contract.hpp>

#include "konative/app/pointer_follow.hpp"
#include "konative/app/waypoint_cycler_interface.hpp"

#include <cereal/archives/binary.hpp>
#include <glm/glm.hpp>

#include <cstddef>
#include <cstring>
#include <sstream>
#include <string>

// A real, independently-hot-reloadable SECOND module (CROSS_MODULE_CALLS_DESIGN.md's own "genuine
// two real modules calling each other" exercise, in the KoReload repo - dogfooded here inside
// Konative for the first time, not just KoReload's own synthetic test fixtures). Drives
// PointerFollow::target autonomously through a small fixed waypoint cycle and provides
// WaypointCyclerInterface via ModuleRegistry's cross-module interface registry - pointer_follow's
// own tick_thunk (src/koreload_modules/pointer_follow/module.cpp) resolves it through the host's
// wiring (jni_onload.cpp) and calls drive_targets() straight from ITS OWN compiled code, in ITS
// OWN .so, genuinely calling into THIS module's .so - not the host calling each module separately
// and combining the results itself.
//
// g_state.elapsed_seconds is this module's one real piece of internal state - exists specifically
// so save_state/restore_state have something genuine to round-trip (mirroring pointer_follow's own
// reload_survival_ticks precedent exactly): an independent reload of THIS module must resume the
// cycle where it left off, not snap back to waypoint 0, proving the two modules' hot-reloads stay
// independent of each other in both directions (pointer_follow's own state already proved this for
// its side; this proves it for the dependency's side too).

namespace {

constexpr glm::vec3 kWaypoints[] = {
    {3.0F, 0.0F, 0.0F},
    {0.0F, 3.0F, 0.0F},
    {-3.0F, 0.0F, 0.0F},
    {0.0F, -3.0F, 0.0F},
};
constexpr std::size_t kWaypointCount = sizeof(kWaypoints) / sizeof(kWaypoints[0]);
// ~2s per waypoint - slow enough for distinct holds to show up in a periodic log, same
// "demo-tuned" framing pointer_follow.hpp's own approach_rate comment already uses.
constexpr float kHoldSeconds = 2.0F;

struct WaypointCyclerState {
    float elapsed_seconds = 0.0F;
};

WaypointCyclerState g_state{};

void drive_targets(konative::ecs::Registry* registry, float delta_seconds) {
    g_state.elapsed_seconds += delta_seconds;
    std::size_t index =
        static_cast<std::size_t>(g_state.elapsed_seconds / kHoldSeconds) % kWaypointCount;
    const glm::vec3& current_target = kWaypoints[index];
    for (auto [entity, follow] : registry->view<konative::app::PointerFollow>().each()) {
        follow.target = current_target;
    }
}

// Real cereal serialization of a plain float - no custom serialize() needed (unlike
// HeartbeatCounter's ADL one), cereal handles primitives natively. Same save/restore shape as
// pointer_follow's own module.cpp for the identical reason: a real, non-synthetic Konative payload
// round-tripping through KoReload's in-process hot-swap, not KoReload's own synthetic int-counter
// benchmark module.
void* save_state(void* instance, std::size_t* out_size) {
    auto* state = static_cast<WaypointCyclerState*>(instance);
    std::ostringstream stream(std::ios::binary);
    {
        cereal::BinaryOutputArchive archive(stream);
        archive(state->elapsed_seconds);
    }
    const std::string bytes = stream.str();
    *out_size = bytes.size();
    char* buffer = new char[bytes.size()];
    std::memcpy(buffer, bytes.data(), bytes.size());
    return buffer;
}

bool restore_state(void* instance, const void* data, std::size_t size) {
    auto* state = static_cast<WaypointCyclerState*>(instance);
    std::istringstream stream(std::string(static_cast<const char*>(data), size), std::ios::binary);
    try {
        cereal::BinaryInputArchive archive(stream);
        archive(state->elapsed_seconds);
    } catch (const std::exception&) {
        return false; // real, corrupt/truncated/layout-mismatched payload - never crash on it
    }
    return true;
}

void free_state(void* data) {
    delete[] static_cast<char*>(data);
}

WaypointCyclerInterface g_interface{&drive_targets};

koreload::ProvidedInterface g_provided_interfaces[] = {
    {kWaypointCyclerInterfaceId, &g_interface, /*version=*/1,
     koreload::interface_shape_hash<WaypointCyclerInterface>()},
};

} // namespace

// KORELOAD_MODULE_EXPORT (not plain extern "C") - Konative's own repo-wide
// CMAKE_CXX_VISIBILITY_PRESET hidden applies to this MODULE target too, the exact same real bug
// pointer_follow/module.cpp's own comment documents on a different symbol.
KORELOAD_MODULE_EXPORT void koreload_waypoint_cycler_module_create(koreload::PluginContract* out) {
    *out = koreload::PluginContract{&g_state, &save_state, &restore_state, &free_state};
    out->provided_interfaces = g_provided_interfaces;
    out->provided_interface_count = 1;
}

KORELOAD_MODULE_EXPORT unsigned koreload_abi_version() { return 1; }
