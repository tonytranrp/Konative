#pragma once

#include "konative/events/window/WindowCreatedEvent.hpp"
#include "konative/events/window/WindowDestroyedEvent.hpp"
#include "konative/interop/kotlin_native_bridge.hpp"

// SUPERSEDED FOR RENDERING, confirmed landed (ARCHITECTURE.md section 6.7, render/README.md) -
// nothing under src/ constructs or calls this class; kept frozen/historical. Konative does NOT own
// a C++ rendering backend (render/README.md) - this class only ever translated window lifecycle
// events into calls across the Kotlin/Native interop boundary, with every actual EGL/GLES/Vulkan
// call happening inside the now-frozen native/src/Renderer.kt. Regardless of live/frozen status,
// the rule stands forever: do not add GLES/EGL/Vulkan headers or calls to this file or anywhere
// else under include/konative/render/ - real rendering is JVM-hosted Compose now (embedded_kotlin/).
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
