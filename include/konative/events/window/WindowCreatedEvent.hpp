#pragma once

struct ANativeWindow;

namespace konative::events::window {

// Mirrors onNativeWindowCreated (NativeActivity) / the equivalent GameActivity callback. Historically
// the point at which konative::render created its EGL/Vulkan surface (ARCHITECTURE.md section 6.2) -
// that render/ path is itself now confirmed superseded by JVM-hosted Compose (ARCHITECTURE.md
// section 6.7), so this event currently has no live consumer; kept for a possible future
// non-Compose native-window use.
struct WindowCreatedEvent {
    ANativeWindow* native_window = nullptr;
};

} // namespace konative::events::window
