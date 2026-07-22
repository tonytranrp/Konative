#pragma once

namespace konative::events::lifecycle {

// Historically mirrored APP_CMD_PAUSE / GameActivity's onPause - neither applies anymore
// (NativeActivity/GameActivity/android_native_app_glue are superseded, ARCHITECTURE.md section
// 6.1/6.7; the real app is a plain Activity, testapp/MainActivity.kt). Real and wired since
// 2026-07-21: fires when the app is backgrounded but not yet destroyed, via
// detail/lifecycle_bridge.hpp's dispatch_paused() -> Application::pause(), called from
// jni_onload.cpp's native_dispatch_lifecycle_event() on the real Activity's onActivityPaused.
struct AppPausedEvent {};

} // namespace konative::events::lifecycle
