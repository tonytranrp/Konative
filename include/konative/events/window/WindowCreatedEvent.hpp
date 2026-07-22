#pragma once

#include "konative/core/type_traits.hpp"

struct ANativeWindow;

namespace konative::events::window {

// Historically mirrored onNativeWindowCreated (NativeActivity) / the equivalent GameActivity
// callback - neither applies anymore (ARCHITECTURE.md section 6.1/6.7). Historically the point at
// which konative::render created its EGL/Vulkan surface (ARCHITECTURE.md section 6.2) - that
// render/ path is itself now confirmed superseded by JVM-hosted Compose (ARCHITECTURE.md section
// 6.7), so this event currently has no live consumer; kept for a possible future non-Compose
// native-window use.
struct WindowCreatedEvent {
    ANativeWindow* native_window = nullptr;
};

static_assert(konative::core::EventType<WindowCreatedEvent>);

} // namespace konative::events::window
