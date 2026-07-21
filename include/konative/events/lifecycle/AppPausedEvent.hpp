#pragma once

namespace konative::events::lifecycle {

// Historically mirrored APP_CMD_PAUSE / GameActivity's onPause - neither applies anymore
// (NativeActivity/GameActivity/android_native_app_glue are superseded, ARCHITECTURE.md section
// 6.1/6.7; the real app is a plain Activity, testapp/MainActivity.kt). Meant to fire when the app
// is backgrounded but not yet destroyed - not yet wired to anything real (see
// detail/lifecycle_bridge.hpp's own note, still a genuinely open item, not a decided design).
struct AppPausedEvent {};

} // namespace konative::events::lifecycle
