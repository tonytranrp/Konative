#pragma once

#include "konative/ecs/world.hpp"
#include "konative/events/lifecycle/AppDestroyedEvent.hpp"
#include "konative/events/lifecycle/AppPausedEvent.hpp"
#include "konative/events/lifecycle/AppResumedEvent.hpp"
#include "konative/events/lifecycle/AppStartedEvent.hpp"

// Translates whichever platform glue is compiled in into the platform-agnostic lifecycle events
// every Konative system actually reacts to. Real, wired, and verified on-device since 2026-07-21
// (ARCHITECTURE.md section 6.4/6.7): src/platform/android/jni_onload.cpp's KonativeAndroidApp calls
// start()/resume()/pause()/destroy() (which call the dispatch_*() functions below) from
// native_dispatch_lifecycle_event(), itself bound via RegisterNatives to
// KonativeEntryPoint.kt's ActivityLifecycleCallbacks - the previous driver (platform/android's
// APP_CMD_* pump, section 6.1) stays deleted, superseded by this JNI_OnLoad-based one, not replaced
// by nothing.
namespace konative::app::detail {

inline void dispatch_started(konative::ecs::World& world) {
    world.events().trigger(konative::events::lifecycle::AppStartedEvent{});
}

inline void dispatch_paused(konative::ecs::World& world) {
    world.events().trigger(konative::events::lifecycle::AppPausedEvent{});
}

inline void dispatch_resumed(konative::ecs::World& world) {
    world.events().trigger(konative::events::lifecycle::AppResumedEvent{});
}

inline void dispatch_destroyed(konative::ecs::World& world) {
    world.events().trigger(konative::events::lifecycle::AppDestroyedEvent{});
}

} // namespace konative::app::detail
