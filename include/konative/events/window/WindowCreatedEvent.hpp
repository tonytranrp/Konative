#pragma once

struct ANativeWindow;

namespace konative::events::window {

// Mirrors onNativeWindowCreated (NativeActivity) / the equivalent GameActivity callback - this
// is the point at which konative::render can create its EGL/Vulkan surface (ARCHITECTURE.md \xc2\xa76.2).
struct WindowCreatedEvent {
    ANativeWindow* native_window = nullptr;
};

} // namespace konative::events::window
