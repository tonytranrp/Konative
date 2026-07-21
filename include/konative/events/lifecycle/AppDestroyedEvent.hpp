#pragma once

namespace konative::events::lifecycle {

// Historically mirrored APP_CMD_DESTROY, the android_native_app_glue lifecycle command fired as its
// dedicated glue thread tore down - that whole mechanism was deleted (commit 3618fb5) along with
// NativeActivity (ARCHITECTURE.md section 6.1/6.7); there is no glue thread anymore. Meant to fire
// once, right before the process actually goes away, however the current JNI_OnLoad design ends up
// triggering that - not yet wired to anything real (see detail/lifecycle_bridge.hpp's own note,
// still a genuinely open item, not a decided design).
struct AppDestroyedEvent {};

} // namespace konative::events::lifecycle
