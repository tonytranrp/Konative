#pragma once

namespace konative::events::window {

// Mirrors onNativeWindowDestroyed - the ANativeWindow the last WindowCreatedEvent referenced is
// no longer valid; any EGL/Vulkan surface bound to it must be torn down before this handler
// returns.
struct WindowDestroyedEvent {};

} // namespace konative::events::window
