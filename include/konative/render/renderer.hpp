#pragma once

#include "konative/events/window/WindowCreatedEvent.hpp"
#include "konative/events/window/WindowDestroyedEvent.hpp"
#include "konative/interop/kotlin_native_bridge.hpp"

// Konative does NOT own a C++ rendering backend (render/README.md). This class exists purely to
// translate window lifecycle events into calls across the Kotlin/Native interop boundary - every
// actual EGL/GLES/Vulkan call happens inside native/src/Renderer.kt. Do not add GLES/EGL/Vulkan
// headers or calls to this file or anywhere else under include/konative/render/.
namespace konative::render {

class Renderer {
public:
    void on_window_created(const konative::events::window::WindowCreatedEvent& event) {
        konative::interop::konative_render_on_window_created(event.native_window);
    }

    void on_window_destroyed(const konative::events::window::WindowDestroyedEvent&) {
        konative::interop::konative_render_on_window_destroyed();
    }

    void tick(double delta_ms) { konative::interop::konative_render_tick(delta_ms); }
};

} // namespace konative::render
