#pragma once

#include "konative/core/type_traits.hpp"

namespace konative::events::lifecycle {

// Historically mirrored APP_CMD_RESUME / GameActivity's onResume - neither applies anymore
// (NativeActivity/GameActivity/android_native_app_glue are superseded, ARCHITECTURE.md section
// 6.1/6.7; the real app is a plain Activity, testapp/MainActivity.kt). Real and wired since
// 2026-07-21: fires when the app becomes interactive again after AppPausedEvent, via
// detail/lifecycle_bridge.hpp's dispatch_resumed() -> Application::resume(), called from
// jni_onload.cpp's native_dispatch_lifecycle_event() on the real Activity's onActivityResumed.
struct AppResumedEvent {};

static_assert(konative::core::EventType<AppResumedEvent>);

} // namespace konative::events::lifecycle
