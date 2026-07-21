#pragma once

namespace konative::events::window {

// Mirrors onNativeWindowDestroyed - the ANativeWindow the last WindowCreatedEvent referenced is
// no longer valid. Historically the point at which any EGL/Vulkan surface bound to it had to be
// torn down before this handler returned (render/'s Kotlin/Native design, ARCHITECTURE.md section
// 6.2) - that path is itself now confirmed superseded by JVM-hosted Compose (ARCHITECTURE.md
// section 6.7), so this event currently has no live consumer either (see WindowCreatedEvent.hpp's
// own comment); kept for a possible future non-Compose native-window use.
struct WindowDestroyedEvent {};

} // namespace konative::events::window
