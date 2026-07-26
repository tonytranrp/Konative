#pragma once

#include <cstdint>

// HeartbeatCounter pairs with a real ECS entity in src/platform/android/jni_onload.cpp's
// increment_heartbeat_counters() system - the first real, non-synthetic ECS component this app
// ever exercised end to end (see that file's own comment for the fuller history). Moved out of
// jni_onload.cpp's own anonymous namespace into a real, shared header for the same reason
// pointer_follow.hpp was: src/koreload_modules/pointer_follow/module.cpp (KONATIVE_ENABLE_KORELOAD,
// PROMPT.md section 13 M8 in the KoReload repo) now ALSO uses this exact type - not for the host's
// own per-entity heartbeat counting (that stays exactly as it was: statically linked, ECS-resident,
// untouched by this move), but as the real, already-production Konative component type +
// serialize() pairing that module's own save_state/restore_state hooks round-trip via cereal,
// proving Konative's own EnTT+cereal-snapshot pattern (already trusted for full-process-restart
// persistence, ecs/snapshot_file.hpp) also survives KoReload's in-process hot-swap path with a
// genuine Konative payload - not KoReload's own synthetic int-counter benchmark module.
namespace konative::app {

struct HeartbeatCounter {
    std::uint64_t ticks = 0;
};

} // namespace konative::app
