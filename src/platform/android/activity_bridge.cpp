// The real android_native_app_glue event loop (ARCHITECTURE.md \xc2\xa76.1) - dispatches APP_CMD_* to
// konative::app::Application and window lifecycle to konative::render::Renderer, which forwards
// into Kotlin/Native across the interop boundary (render/README.md - this file must never call
// EGL/GLES/Vulkan directly, only konative::render::Renderer's three events).

#include "konative/platform/android/activity_bridge.hpp"

#include <android_native_app_glue.h>

#include "konative/app/application.hpp"
#include "konative/app/entry_point.hpp"
#include "konative/events/window/WindowCreatedEvent.hpp"
#include "konative/events/window/WindowDestroyedEvent.hpp"
#include "konative/platform/android/detail/app_cmd_translation.hpp"
#include "konative/platform/android/looper_pump.hpp"
#include "konative/render/renderer.hpp"

namespace konative::platform::android {

namespace {

struct EngineState {
    konative::app::Application* app = nullptr;
    konative::render::Renderer renderer;
};

void handle_cmd(struct android_app* app_handle, int32_t cmd) {
    auto* state = static_cast<EngineState*>(app_handle->userData);
    const auto app_cmd = static_cast<detail::AppCmd>(cmd);

    switch (app_cmd) {
        case detail::AppCmd::InitWindow:
            if (app_handle->window != nullptr) {
                state->renderer.on_window_created(
                    konative::events::window::WindowCreatedEvent{app_handle->window});
            }
            break;
        case detail::AppCmd::TermWindow:
            state->renderer.on_window_destroyed(konative::events::window::WindowDestroyedEvent{});
            break;
        default:
            detail::handle_app_cmd(*state->app, app_cmd);
            break;
    }
}

} // namespace

void run_application(NativeAppHandle app_handle) {
    EngineState state;
    state.app = &konative::app::create_application();

    app_handle->userData = &state;
    app_handle->onAppCmd = handle_cmd;

    // Fixed-step placeholder (ARCHITECTURE.md \xc2\xa79 - real frame timing belongs in a later pass,
    // not this skeleton). timeout_millis=0 -> never blocks, so this is a busy poll loop for now.
    constexpr double kFixedDeltaMs = 16.0;
    bool running = true;
    while (running) {
        running = pump_once(app_handle, /*timeout_millis=*/0);
        if (app_handle->destroyRequested != 0) {
            running = false;
        }
        state.renderer.tick(kFixedDeltaMs);
    }
}

} // namespace konative::platform::android
