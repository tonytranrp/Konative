#pragma once

#include "konative/ecs/world.hpp"
#include "konative/events/lifecycle/AppDestroyedEvent.hpp"
#include "konative/events/lifecycle/AppPausedEvent.hpp"
#include "konative/events/lifecycle/AppResumedEvent.hpp"
#include "konative/events/lifecycle/AppStartedEvent.hpp"

// Translates whichever platform glue is compiled in into the platform-agnostic lifecycle events
// every Konative system actually reacts to. Not currently wired to anything - the previous driver
// (platform/android's APP_CMD_* pump, ARCHITECTURE.md section 6.1) was deleted with the
// NativeActivity design; the current JNI_OnLoad design's relationship to these events (e.g. driven
// by the embedded dex's own ActivityLifecycleCallbacks calling back into native code) is a real
// open item, not yet decided (ARCHITECTURE.md section 6.4).
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
