#pragma once

#include "konative/core/type_traits.hpp"

namespace konative::events::lifecycle {

// Historically mirrored APP_CMD_DESTROY, the android_native_app_glue lifecycle command fired as its
// dedicated glue thread tore down - that whole mechanism was deleted (commit 3618fb5) along with
// NativeActivity (ARCHITECTURE.md section 6.1/6.7); there is no glue thread anymore. Real and wired
// since 2026-07-21: fires once, via detail/lifecycle_bridge.hpp's dispatch_destroyed() ->
// Application::destroy(), called from jni_onload.cpp's native_dispatch_lifecycle_event() on the
// real Activity's onActivityDestroyed.
struct AppDestroyedEvent {};

static_assert(konative::core::EventType<AppDestroyedEvent>);

} // namespace konative::events::lifecycle
